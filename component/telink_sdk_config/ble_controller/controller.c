/* SPDX-License-Identifier: Apache-2.0
 * HCI bring-up follows Telink's vendor/ble_controller example. The H4 packets
 * stay in memory; UART0 remains the FreeRTOS CLI. Only this task calls the LL.
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "stack/ble/os_sup/os_sup.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "controller.h"

volatile Tl3238BleControllerStats tl3238_controller_stats;
volatile uint32_t tl3238_boot_trace[8] __attribute__((section(".iram_noinit_data")));
static TaskHandle_t controller_task_handle;
static SemaphoreHandle_t controller_mutex;
#ifdef TL3238_ENABLE_CS
#define CONTROLLER_BUFFER __attribute__((section(".iram_bss"), aligned(16)))
#else
#define CONTROLLER_BUFFER __attribute__((aligned(16)))
#endif
static uint8_t acl_rx[CAL_LL_ACL_RX_FIFO_SIZE(251) * 4] CONTROLLER_BUFFER;
static uint8_t acl_tx[CAL_LL_ACL_TX_FIFO_SIZE(251) * 9] CONTROLLER_BUFFER;
#ifdef TL3238_ENABLE_CS
static uint8_t central_tx[CAL_LL_ACL_TX_FIFO_SIZE(251) * 9] CONTROLLER_BUFFER;
#endif
static uint8_t hci_rx[HCI_ACL_IN_FIFO_SIZE(255) * 4] CONTROLLER_BUFFER;
#ifdef TL3238_ENABLE_CS
#define HCI_TX_COUNT 8
#else
#define HCI_TX_COUNT 4
#endif
static uint8_t hci_tx[HCI_ACL_IN_FIFO_SIZE(255) * HCI_TX_COUNT] CONTROLLER_BUFFER;
#ifdef TL3238_ENABLE_CS
static uint8_t hci_acl[CALCULATE_HCI_ACL_DATA_FIFO_SIZE(251) * 4 * 2] CONTROLLER_BUFFER;
#else
static uint8_t hci_acl[CALCULATE_HCI_ACL_DATA_FIFO_SIZE(251) * 4] CONTROLLER_BUFFER;
#endif

void tl3238_ble_board_init(void)
{
    if (tl3238_boot_trace[0] != 0x3238b007) {
        for (unsigned i = 1; i < 8; i++) tl3238_boot_trace[i] = 0;
        tl3238_boot_trace[0] = 0x3238b007;
    }
    tl3238_boot_trace[1]++;
    blc_pm_select_internal_32k_crystal();
    sys_init(LDO_1P25_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M);
    tl3238_boot_trace[2]++;
    pm_update_status_info(1);
    wd_stop();
    wd_32k_stop();
    tl3238_boot_trace[3]++;
#ifdef TL3238_ENABLE_CS
    /* CS IQ processing uses the clock configuration of the vendor CS demo. */
    PLL_192M_CCLK_96M_HCLK_48M_PCLK_48M_MSPI_64M;
#else
    PLL_192M_CCLK_48M_HCLK_24M_PCLK_12M_MSPI_48M;
#endif
    tl3238_boot_trace[4]++;
    gpio_init(1);
    gpio_set_up_down_res(GPIO_SWS, GPIO_PIN_PULLUP_1M);
    tl3238_boot_trace[5]++;
}

void tl3238_controller_wake_from_isr(void)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(controller_task_handle, &woken);
    portYIELD_FROM_ISR(woken);
}

static void wake_from_task(void) { xTaskNotifyGive(controller_task_handle); }
static void take_mutex(void) { configASSERT(xSemaphoreTake(controller_mutex, portMAX_DELAY) == pdPASS); }
static void give_mutex(void) { configASSERT(xSemaphoreGive(controller_mutex) == pdPASS); }

void tl3238_controller_irq(void)
{
    tl3238_controller_stats.irqs++;
    blc_sdk_irq_handler();
    tl3238_controller_wake_from_isr();
}

static void controller_init(void)
{
    uint8_t mac[6], random_mac[6];
    memset(acl_rx, 0, sizeof acl_rx);
    memset(acl_tx, 0, sizeof acl_tx);
#ifdef TL3238_ENABLE_CS
    memset(central_tx, 0, sizeof central_tx);
#endif
    memset(hci_rx, 0, sizeof hci_rx);
    memset(hci_tx, 0, sizeof hci_tx);
    memset(hci_acl, 0, sizeof hci_acl);
    rf_drv_ble_init();
    random_generator_init();
    blc_readFlashSize_autoConfigCustomFlashSector();
    blc_app_loadCustomizedParameters_normal();
    blc_initMacAddress(flash_sector_mac_address, mac, random_mac);
    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(mac);
    blc_ll_initLegacyAdvertising_module();
#ifdef TL3238_ENABLE_CS
    blc_ll_initLegacyScanning_module();
    blc_ll_initLegacyInitiating_module();
#endif
    blc_ll_initAclConnection_module();
    blc_ll_initAclPeriphrRole_module();
#ifdef TL3238_ENABLE_CS
    blc_ll_initAclCentralRole_module();
#endif
    blc_ll_init2MPhyCodedPhy_feature();
    blc_ll_initChannelSelectionAlgorithm_2_feature();
    blc_ll_configLegacyAdvEnableStrategy(LEG_ADV_EN_STRATEGY_3);
    blc_ll_setAclConnMaxOctetsNumber(251, 251, 251);
    blc_ll_initAclConnRxFifo(acl_rx, CAL_LL_ACL_RX_FIFO_SIZE(251), 4);
    blc_ll_initAclPeriphrTxFifo(acl_tx, CAL_LL_ACL_TX_FIFO_SIZE(251), 9, 1);
#ifdef TL3238_ENABLE_CS
    blc_ll_initAclCentralTxFifo(central_tx, CAL_LL_ACL_TX_FIFO_SIZE(251), 9, 1);
    blc_ll_setMaxConnectionNumber(1, 1);
#else
    blc_ll_setMaxConnectionNumber(0, 1);
#endif
    configASSERT(blc_ll_initHciRxFifo(hci_rx, HCI_ACL_IN_FIFO_SIZE(255), 4) == BLE_SUCCESS);
    configASSERT(blc_ll_initHciTxFifo(hci_tx, HCI_ACL_IN_FIFO_SIZE(255), HCI_TX_COUNT) == BLE_SUCCESS);
    configASSERT(blc_ll_initHciAclDataFifo(hci_acl, CALCULATE_HCI_ACL_DATA_FIFO_SIZE(251), 4) == BLE_SUCCESS);
    blc_hci_registerControllerDataHandler(blc_hci_sendACLData2Host);
#ifdef TL3238_ENABLE_CS
    blc_hci_registerControllerEventHandler(tl3238_controller_event);
    tl3238_controller_cs_init();
#else
    blc_hci_registerControllerEventHandler(blc_hci_send_data);
#endif
    blc_ll_setAutoExchangeDataLengthEnable(0);
    rf_set_power_level_index(RF_POWER_P3dBm);
    tl3238_controller_stats.init_error = blc_contr_checkControllerInitialization();
    configASSERT(tl3238_controller_stats.init_error == INIT_SUCCESS);
    blc_ll_registerGiveSemCb(tl3238_controller_wake_from_isr, wake_from_task);
    blc_ll_registerMutexSemCb(take_mutex, give_mutex);
    blc_setOsSupEnable(true);
    /* blc_ll_initBasicMCU re-enables SDK interrupt nesting AFTER the RTOS
     * scheduler has disabled it. Restore the port's non-nesting contract;
     * its ISR wrapper and scheduler callees also reside in XIP flash. */
    plic_preempt_feature_dis();
    flash_plic_preempt_config(0, 1);
    plic_interrupt_enable(IRQ_ZB_RT);
    plic_interrupt_enable(IRQ_SYSTIMER);
    tl3238_controller_stats.ready = 1;
}

int tl3238_controller_can_send(void)
{
    taskENTER_CRITICAL();
    int ready = tl3238_controller_stats.ready &&
        (uint8_t)(bltHci_rxfifo.wptr - bltHci_rxfifo.rptr) < bltHci_rxfifo.num;
    taskEXIT_CRITICAL();
    return ready;
}

int tl3238_controller_send(uint8_t type, const void *data, size_t len)
{
    if (!tl3238_controller_stats.ready || len + 1 > bltHci_rxfifo.size) {
        return -1;
    }
    taskENTER_CRITICAL();
    if ((uint8_t)(bltHci_rxfifo.wptr - bltHci_rxfifo.rptr) >= bltHci_rxfifo.num) {
        taskEXIT_CRITICAL();
        tl3238_controller_stats.errors++;
        return -1;
    }
    uint8_t *p = bltHci_rxfifo.p + (bltHci_rxfifo.wptr & bltHci_rxfifo.mask) * bltHci_rxfifo.size;
    p[0] = type;
    memcpy(p + 1, data, len);
    bltHci_rxfifo.wptr++;
    taskEXIT_CRITICAL();
    if (type == 1) tl3238_controller_stats.commands++;
    else tl3238_controller_stats.tx_acl++;
    wake_from_task();
    return 0;
}

static void controller_task(void *arg)
{
    (void)arg;
    controller_init();
    for (;;) {
        tl3238_controller_stats.loops++;
        while (bltHci_rxfifo.rptr != bltHci_rxfifo.wptr) {
            uint8_t *p = bltHci_rxfifo.p + (bltHci_rxfifo.rptr & bltHci_rxfifo.mask) * bltHci_rxfifo.size;
#ifdef TL3238_ENABLE_CS
            if (!tl3238_controller_cs_command(p))
#endif
                blc_hci_handler(p, 0);
            bltHci_rxfifo.rptr++;
        }
#ifdef TL3238_ENABLE_CS
        tl3238_controller_cs_poll();
#endif
        blc_sdk_main_loop();
        while (bltHci_txfifo.rptr != bltHci_txfifo.wptr) {
            uint8_t *p = bltHci_txfifo.p + (bltHci_txfifo.rptr & bltHci_txfifo.mask) * bltHci_txfifo.size;
            size_t len = p[0] | ((size_t)p[1] << 8);
            configASSERT(len > 0 && len <= (size_t)bltHci_txfifo.size - 2);
            if (tl3238_host_receive(p + 2, len) != 0) {
                break; /* Retry without dropping a controller packet. */
            }
            if (p[2] == 4) tl3238_controller_stats.events++;
            else tl3238_controller_stats.rx_acl++;
            bltHci_txfifo.rptr++;
        }
        /* RF/STIMER interrupts or host commands wake us. A short retry is only
         * needed when host mbufs temporarily apply backpressure. */
        ulTaskNotifyTake(pdTRUE, bltHci_txfifo.rptr != bltHci_txfifo.wptr ? 1 : portMAX_DELAY);
    }
}

void tl3238_controller_start(void)
{
    controller_mutex = xSemaphoreCreateMutex();
    configASSERT(controller_mutex);
    configASSERT(xTaskCreate(controller_task, "ble_ll", 1024, NULL, configMAX_PRIORITIES - 1, &controller_task_handle) == pdPASS);
    configASSERT(tl3238_controller_stats.ready);
}
