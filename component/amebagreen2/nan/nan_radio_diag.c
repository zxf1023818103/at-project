/* Read-only RF snapshots for lib_wifi_whc_np.a SHA256 682488343c85376b...
 * make_nan_fixed_library.py verifies the complete archive hash before linking.
 * The private hal_data offsets below were checked against this archive's
 * channel-switch, TPC and halrf_get_tx_power_by_user implementations.
 *
 * Capture after a channel switch so short discovery windows are observable.
 * Print only from the existing NP status task, never in the switching path.
 * Powers are targets for the named rates, not measurements or the peer's
 * current rate: wifi_get_traffic_stats(NAN_WLAN_INDEX) returns empty statistics.
 */
#include "rtw_inic_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>

extern uint8_t hal_data[];
extern uint8_t *g_nanpriv;
extern void __real_wifi_hal_set_channel_bw(uint8_t channel, uint8_t offset,
                                          uint8_t bandwidth);

enum {
    RF_BAND = 0x22c,
    RF_BW = 0x22d,
    RF_PRIMARY_CH = 0x22f,
    RF_CENTER_CH = 0x230,
    RF_WRITE_GUARD = 0x2a9,
    RF_TABLE_FLAGS = 0x2aa,
    RF_FORCE_POWER = 0x2ac,
    RF_FORCE_FLAGS = 0x2ad,
    RF_REGULATION = 0x2d3,
    RF_STATUS = 0x2d6,
    RF_EFUSE_TABLE_SELECT = 0x61,
    RF_DIAG_CHANNELS = 8,
    RF_DIAG_RATES = 8,
    RF_POWER_WORDS = 7,
};

static const uint8_t rates[RF_DIAG_RATES] = {
    RTW_RATE_6M, RTW_RATE_54M, RTW_RATE_MCS0, RTW_RATE_MCS7,
    RTW_RATE_HE1SS_MCS0, RTW_RATE_HE1SS_MCS7,
    RTW_RATE_HE1SS_MCS8, RTW_RATE_HE1SS_MCS9,
};

struct radio_snapshot {
    uint32_t samples;
    uint8_t channel, center, bandwidth, band, regulation;
    uint8_t table_config, efuse_select, table_flags;
    uint8_t forced, tpc_config, tpc_active, valid;
    int8_t forced_power, power[RF_DIAG_RATES];
    uint8_t write_guard;
    uint32_t mac_power[RF_POWER_WORDS], mac_mode, mac_limit, mac_ru_limit;
};

static struct radio_snapshot snapshots[RF_DIAG_CHANNELS];
static uint32_t overflow;
static TickType_t last_print;
static int printed;

static void capture(void)
{
    /* Current NAN switching is deferred from IRQ to the driver task. Keep this
     * diagnostic inert if a future caller invokes the HAL from an interrupt. */
    if (rtos_critical_is_in_interrupt()) return;
    rtos_critical_enter(RTOS_CRITICAL_WIFI);
    if (!g_nanpriv || !(g_nanpriv[2350] & 2)) goto out;
    uint8_t channel = hal_data[RF_PRIMARY_CH];
    if (!channel) goto out;
    unsigned slot;
    for (slot = 0; slot < RF_DIAG_CHANNELS; ++slot)
        if (!snapshots[slot].samples || snapshots[slot].channel == channel) break;
    if (slot == RF_DIAG_CHANNELS) { ++overflow; goto out; }

    struct radio_snapshot *s = &snapshots[slot];
    ++s->samples;
    s->channel = channel;
    s->center = hal_data[RF_CENTER_CH];
    s->bandwidth = hal_data[RF_BW];
    s->band = hal_data[RF_BAND];
    s->regulation = hal_data[RF_REGULATION];
    s->table_config = wifi_user_config.tx_pwr_table_selection;
    s->efuse_select = hal_data[RF_EFUSE_TABLE_SELECT] & 3;
    s->table_flags = hal_data[RF_TABLE_FLAGS] & 3;
    s->forced = hal_data[RF_FORCE_FLAGS] & 1;
    s->forced_power = (int8_t)hal_data[RF_FORCE_POWER];
    s->write_guard = hal_data[RF_WRITE_GUARD];
    s->tpc_config = wifi_user_config.tpc_enable;
    s->tpc_active = !!(hal_data[RF_STATUS] & 0x40);
    s->valid = 0;
    /* On NP this getter only reads the current in-memory RF tables, calls the
     * ROM rate conversion, and returns. It does not allocate, block or use IPC.
     * Keep one snapshot atomic with respect to switching to another channel. */
    for (unsigned i = 0; i < RF_DIAG_RATES; ++i)
        if (wifi_get_tx_power(rates[i], &s->power[i]) == RTK_SUCCESS)
            s->valid |= (uint8_t)(1u << i);
    /* Same hash-pinned NP implementation writes these non-destructive MAC
     * registers in rtw_hal_mac_write_pwr_by_rate_reg/ofst_mode/limit_en.
     * Read hardware, not just the software target returned by the getter.
     * 0xd2c0..0xd2d8 hold 28 per-rate half-dBm entries. */
    volatile const uint32_t *rate_reg = (volatile const uint32_t *)0x4000d2c0u;
    for (unsigned i = 0; i < RF_POWER_WORDS; ++i)
        s->mac_power[i] = rate_reg[i];
    s->mac_mode = *(volatile const uint32_t *)0x4000d204u;
    s->mac_limit = *(volatile const uint32_t *)0x4000d208u;
    s->mac_ru_limit = *(volatile const uint32_t *)0x4000d21cu;
out:
    rtos_critical_exit(RTOS_CRITICAL_WIFI);
}

void __wrap_wifi_hal_set_channel_bw(uint8_t channel, uint8_t offset,
                                    uint8_t bandwidth)
{
    __real_wifi_hal_set_channel_bw(channel, offset, bandwidth);
    capture();
}

void green2_nan_radio_diag_poll(void)
{
    capture();
    TickType_t now = xTaskGetTickCount();
    if (printed && (TickType_t)(now - last_print) < pdMS_TO_TICKS(10000)) return;
    /* The NP command task is the sole poll caller. Keep the enlarged batch
     * off its stack; channel-switch capture writes only snapshots[]. */
    static struct radio_snapshot batch[RF_DIAG_CHANNELS];
    rtos_critical_enter(RTOS_CRITICAL_WIFI);
    memcpy(batch, snapshots, sizeof(batch));
    memset(snapshots, 0, sizeof(snapshots));
    uint32_t missed = overflow;
    overflow = 0;
    rtos_critical_exit(RTOS_CRITICAL_WIFI);
    last_print = now;
    printed = 1;

    for (unsigned i = 0; i < RF_DIAG_CHANNELS; ++i) {
        const struct radio_snapshot *s = &batch[i];
        if (!s->samples) continue;
        RTK_LOGS(NOTAG, RTK_LOG_INFO,
                 "[NAN-RF] ch=%u center=%u bw_code=%u band=%u reg=%u table=%u efuse_sel=%u byrate=%u limit=%u force=%u force_qdbm=%d tpc=%u/%u samples=%u\n",
                 (unsigned)s->channel, (unsigned)s->center,
                 (unsigned)s->bandwidth, (unsigned)s->band,
                 (unsigned)s->regulation, (unsigned)s->table_config,
                 (unsigned)s->efuse_select, (unsigned)!!(s->table_flags & 2),
                 (unsigned)(s->table_flags & 1), (unsigned)s->forced,
                 (int)s->forced_power, (unsigned)s->tpc_config,
                 (unsigned)s->tpc_active, (unsigned)s->samples);
        RTK_LOGS(NOTAG, RTK_LOG_INFO,
                 "[NAN-RF] ch=%u target_qdbm valid=0x%02x OFDM6=%d OFDM54=%d HT0=%d HT7=%d HE0=%d HE7=%d HE8=%d HE9=%d\n",
                 (unsigned)s->channel, (unsigned)s->valid,
                 (int)s->power[0], (int)s->power[1], (int)s->power[2],
                 (int)s->power[3], (int)s->power[4], (int)s->power[5],
                 (int)s->power[6], (int)s->power[7]);
        RTK_LOGS(NOTAG, RTK_LOG_INFO,
                 "[NAN-RF] ch=%u mac_power_halfdbm=%08x,%08x,%08x,%08x,%08x,%08x,%08x write_guard=%u mode=%08x limit=%08x ru_limit=%08x\n",
                 (unsigned)s->channel,
                 (unsigned)s->mac_power[0], (unsigned)s->mac_power[1],
                 (unsigned)s->mac_power[2], (unsigned)s->mac_power[3],
                 (unsigned)s->mac_power[4], (unsigned)s->mac_power[5],
                 (unsigned)s->mac_power[6], (unsigned)s->write_guard,
                 (unsigned)s->mac_mode, (unsigned)s->mac_limit,
                 (unsigned)s->mac_ru_limit);
    }
    RTK_LOGS(NOTAG, RTK_LOG_INFO,
             "[NAN-CLK] cpu_hz=%u check=%d cldo_active=%u cldo_normal=%u cldo_sleep=%u regu_state=%u\n",
             (unsigned)CPU_ClkGet(), CPU_Clk_Check(),
             (unsigned)LDO_GET_VOADJ_L_1209(LDO_BASE->LDO_RFAFE_1209),
             (unsigned)LDO_GET_VOLT_CODE2_CLDO(LDO_BASE->LDO_CLDO_VOLT_CTRL),
             (unsigned)LDO_GET_VOLT_CODE1_CLDO(LDO_BASE->LDO_CLDO_VOLT_CTRL),
             (unsigned)LDO_GET_CUR_STS_VAL(LDO_BASE->LDO_APMC_STA));
    if (missed)
        RTK_LOGS(NOTAG, RTK_LOG_INFO, "[NAN-RF] channel snapshot overflow=%u\n",
                 (unsigned)missed);
}
