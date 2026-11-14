/* SPDX-License-Identifier: Apache-2.0
 * Vendor LL only. NimBLE uses standard Core 6.0 HCI packets at this boundary.
 * In this SDK the procedure parameters omit the two SNR-control bytes.
 * Translate that ABI difference
 * here, never in the host, and call the LL only from the controller task.
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "FreeRTOS.h"
#include "task.h"
#include "controller.h"
#include "cs_hci.h"

/* The custom LL added this indirection; platform V4.1 still exposes the
 * underlying erase function. Keep the established board driver. */
void (*flash_erase_by_sector)(unsigned long) = flash_erase_sector;

#define CS_BUFFER __attribute__((section(".iram_bss"), aligned(16)))
_Static_assert(CS_SUBEVENT_BUFF_LEN_MAX - 16 <= TL3238_CS_RAW_SUBEVENT_CAPACITY,
               "CS diagnostic capacity must cover the controller's raw subevent");
static uint8_t cs_config[CS_PARAM_LENGTH] CS_BUFFER;
static uint8_t cs_phy[CHANNEL_SOUNDING_RX_FIFO_SIZE_ALIGN16(1, 40, 10)] CS_BUFFER;
static uint8_t cs_transport[4 * CHANNEL_SOUNDING_TRANSPORT_RX_FIFO_SIZE_WITHOUT_MODE1_ALIGN16(1, 40, 10)] CS_BUFFER;
static uint8_t cs_hci[2 * CS_SUBEVENT_BUFF_LEN_MAX] CS_BUFFER;
static uint8_t cs_pct[2 * STEP_NUM_PER_SUBEVENT * (PHASE_ITEM_LEN + AMP_ITEM_LEN)] CS_BUFFER;
static uint8_t cs_drbg[CS_STEP_DRBG_INFO_SIZE * STEP_NUM_PER_SUBEVENT] CS_BUFFER;
static volatile uint8_t cs_pending;

void tl3238_controller_cs_init(void)
{
    /* SDK startup does not clear .iram_bss. Initialize every buffer explicitly. */
    memset(cs_config, 0, sizeof cs_config);
    memset(cs_phy, 0, sizeof cs_phy);
    memset(cs_transport, 0, sizeof cs_transport);
    memset(cs_hci, 0, sizeof cs_hci);
    memset(cs_pct, 0, sizeof cs_pct);
    memset(cs_drbg, 0, sizeof cs_drbg);
    configASSERT(blc_ll_initCsInitiatorModule() == BLE_SUCCESS);
    configASSERT(blc_ll_initCsReflectorModule() == BLE_SUCCESS);
    configASSERT(blc_ll_initCsConfigParam(cs_config, 1) == BLE_SUCCESS);
    configASSERT(blc_ll_initCsPhyRxFifo(cs_phy, sizeof cs_phy, 1) == BLE_SUCCESS);
    configASSERT(blc_ll_initCsTransportRxFifo(cs_transport, sizeof cs_transport / 4, 4) == BLE_SUCCESS);
    configASSERT(blc_cs_initCsHciRxFifo(cs_hci, sizeof cs_hci / 2, 2) == BLE_SUCCESS);
    configASSERT(blc_ll_initCsPctDataBuff(cs_pct, sizeof cs_pct, 1) == BLE_SUCCESS);
    /* The API takes a step COUNT, not CS_STEP_DRBG_INFO_SIZE (bytes/step). */
    configASSERT(blc_cs_initStepDRBGInfo(cs_drbg, STEP_NUM_PER_SUBEVENT, 1) == BLE_SUCCESS);
    static cs_ant_switch_config_t antenna = {
        .Num_Antennas_Supported = 1, .Max_Antenna_Paths_Supported = 1,
    };
    blc_cs_antenna_switch_config_init(&antenna);
    blc_cs_set_tx_power_level(RF_POWER_P3dBm);
    tl3238_controller_stats.cs_calibration =
        blc_loadCsCali_table(flash_sector_calibration + CALIB_OFFSET_CALI_TABLE_HEADER_INFO);
    timer_stop(TIMER0);
    timer_set_mode(TIMER0, TIMER_MODE_SYSCLK);
    timer_clr_irq_status(FLD_TMR0_MODE_IRQ);
    timer_set_irq_mask(FLD_TMR0_MODE_IRQ);
    plic_set_priority(IRQ_TIMER0, 1);
    plic_interrupt_enable(IRQ_TIMER0);
    tl3238_controller_stats.cs_ready = 1;
}

void tl3238_controller_cs_irq(void)
{
    if (timer_get_irq_status(FLD_TMR0_MODE_IRQ)) {
        timer_stop(TIMER0);
        timer_clr_irq_status(FLD_TMR0_MODE_IRQ);
        tl3238_controller_stats.cs_irqs++;
        cs_pending = 1;
        tl3238_controller_wake_from_isr();
    }
}

void tl3238_controller_cs_poll(void)
{
    if (!cs_pending) return;
    unsigned irq_state = core_interrupt_disable();
    cs_pending = 0;
    core_restore_interrupt(irq_state);
    /* The vendor example uses a preemptible low-priority TIMER0 ISR. Our RTOS
     * deliberately forbids nested ISR context saves. Run DSP/reporting in the
     * high-priority LL task instead, allowing RF/STIMER to interrupt it. */
    if (ll_cs_rawData_process_cb) ll_cs_rawData_process_cb();
    if (ll_cs_hci_subevent_report_cb) ll_cs_hci_subevent_report_cb();
}

static uint16_t read16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }

int tl3238_controller_cs_command(uint8_t *h4)
{
    if (h4[0] != 1 || h4[2] != 0x20 || h4[1] < 0x89 || h4[1] > 0x94) return 0;
    uint8_t ocf = h4[1], len = h4[3], *p = h4 + 4;
    static const uint8_t lengths[] = {0, 2, 30, 2, 5, 2, 74, 28, 3, 10, 23, 4};
    uint8_t ret[40] = {1, ocf, 0x20, 0x12}; /* Num packets, opcode, status */
    unsigned retlen = 4;
    int async = ocf == 0x8a || ocf == 0x8c || ocf == 0x8e || ocf == 0x90 || ocf == 0x91 || ocf == 0x94;
    uint8_t status = 0x12; /* Invalid HCI Command Parameters */
    if (len == lengths[ocf - 0x89]) {
        switch (ocf) {
        case 0x89: {
            hci_le_cs_readLocalSupportedCap_retParam_t caps = {0};
            status = blc_hci_le_cs_readLocalSupportedCap(&caps);
            memcpy(ret + 3, &caps, sizeof caps);
            retlen = 3 + sizeof caps;
            break;
        }
        case 0x8a: status = blc_hci_le_cs_readRemoteSupportedCap(read16(p)); break;
        case 0x8b: {
            hci_le_cs_writeCachedRemoteSupportedCap_cmdParam_t cmd = {0};
            memcpy(&cmd, p, len);
            status = blc_hci_le_cs_writeCachedRemoteSupportedCap(&cmd, (void *)(ret + 3));
            retlen = 6;
            break;
        }
        case 0x8c: status = blc_hci_le_cs_security_enable(read16(p)); break;
        case 0x8d:
            status = blc_hci_le_cs_setDefaultSettings((void *)p, (void *)(ret + 3));
            retlen = 6; break;
        case 0x8e: status = blc_hci_le_cs_readRemoteFAE_table(read16(p)); break;
        case 0x8f:
            status = blc_hci_le_cs_writeCachedRemoteFAE_table(read16(p), p + 2, (void *)(ret + 3));
            retlen = 6; break;
        case 0x90: status = blc_hci_le_cs_createConfig((void *)p); break;
        case 0x91: status = blc_hci_le_cs_removeConfig(read16(p), p[2]); break;
        case 0x92: status = blc_hci_le_cs_setChannelClassification(p); break;
        case 0x93:
            /* This controller has no SNR control; never silently accept it. */
            status = (p[21] == 0xff && p[22] == 0xff) ?
                blc_hci_le_cs_setProcedureParam((void *)p, (void *)(ret + 3)) : 0x11;
            retlen = 6; break;
        case 0x94: status = blc_hci_le_cs_procedureEnable((void *)p); break;
        }
    }
    if (async) {
        uint8_t ack[] = {status, 1, ocf, 0x20};
        blc_hci_send_data(HCI_FLAG_EVENT_BT_STD | 0x0f, ack, sizeof ack);
    } else {
        ret[3] = status;
        if (retlen == 6) { ret[4] = len >= 2 ? p[0] : 0; ret[5] = len >= 2 ? p[1] : 0; }
        blc_hci_send_data(HCI_FLAG_EVENT_BT_STD | 0x0e, ret, retlen);
    }
    return 1;
}

static void emit_cs_packet(const uint8_t *p, unsigned n, void *arg)
{
    (void)arg;
    blc_hci_send_data(HCI_FLAG_EVENT_BT_STD | 0x3e, (uint8_t *)p, n);
}

int tl3238_controller_event(unsigned int h, uint8_t *p, int n)
{
    if ((h & 0xff) == 0x05 && n >= 4 && p[0] == 0) blc_cs_resetByHandle(read16(p + 1));
    if ((h & 0xff) == 0xff && n == 7 && p[0] == 0x80) {
        /* Telink reports automatic/peer termination with a vendor event. */
        uint8_t disabled[22] = {0x30, 0, p[1], p[2], p[5], 0};
        disabled[18] = p[3]; disabled[19] = p[4];
        return blc_hci_send_data(HCI_FLAG_EVENT_BT_STD | 0x3e, disabled, sizeof disabled);
    }
    if ((h & 0xff) != 0x3e || n < 1) return blc_hci_send_data(h, p, n);
    if (p[0] == 0x38 && n >= 32) {
        uint8_t v1[32]; memcpy(v1, p, sizeof v1); v1[0] = 0x2c;
        return blc_hci_send_data(h, v1, sizeof v1);
    }
    if (p[0] != 0x31 && p[0] != 0x32) return blc_hci_send_data(h, p, n);
    tl3238_controller_stats.cs_events++;
    unsigned irq_state = core_interrupt_disable();
    unsigned slots = bltHci_txfifo.num - (uint8_t)(bltHci_txfifo.wptr - bltHci_txfifo.rptr);
    int rc = tl3238_cs_hci_split(p, n, slots, emit_cs_packet, NULL);
    if (rc < 0) tl3238_controller_stats.cs_drops++;
    core_restore_interrupt(irq_state);
    return rc < 0 ? -1 : 0;
}
