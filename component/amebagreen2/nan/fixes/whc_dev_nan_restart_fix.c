/* Private ABI of lib_wifi_whc_np.a 68248834... (the archive builder pins it).
 * rtw_nan.o, rtw_nan_dw.o, rtw_nan_cluster.o and rtw_nan_scheduler.o are
 * byte-identical to the reference fullmac archive used by this fix.
 * Starting NAN creates the software DW timer, but the native path arms it
 * only after a cluster-operation update. During some stop/start sequences
 * that update never occurs: NAN is enabled, while TWT1 remains parked at
 * the warmup target 0xefffffffffffffff and no discovery frames are sent.
 * Restore native schedule events and arm DW once after a new start. Leave its
 * TSF calculation, band selection, warmup and periodic scheduling native.
 */
#include "rtw_inic_common.h"
#include <stdint.h>

extern uint8_t *g_nanpriv;
extern uint8_t *g_nanctrl;
extern void rtw_nan_dw_timer_reset(uint8_t enable);
extern uint32_t rtw_nan_event_enqueue(uint32_t event, const void *data, uint32_t len);
extern int __real_rtw_nan_intfs_init(void);
extern void __real_rtw_nan_start_api(uint8_t, uint8_t, uint8_t);

void green2_nan_reset_discovery_controller(void)
{
    /* intfs_deinit has stopped NAN, deleted the warmup/DW timers and drained
     * the interface. intfs_init clears g_nanpriv and the scheduler, but keeps
     * this 1784-byte controller allocation. Reusing its cached schedule after
     * a host reboot can leave TWT1 at the warmup sentinel indefinitely.
     * Match the zeroed allocation used by the first rtw_nan_init; never clear
     * a live controller or its timers. Size is from the pinned archive.
     */
    if (g_nanctrl && g_nanpriv && !(g_nanpriv[2350] & 3)) {
        memset(g_nanctrl, 0, 1784);
        RTK_LOGS(NOTAG, RTK_LOG_INFO, "[NANSTART] retired discovery controller reset\n");
    }
}

void green2_nan_arm_discovery_timer(void)
{
    if (g_nanpriv && (g_nanpriv[2350] & 3) == 3) {
        rtos_timer_t timer;
        /* A stop/start retains the selected cluster in g_nanpriv. The new
         * controller adopts it during warmup, so cluster_operation(1) sees
         * equal IDs and skips its entire change branch, including initial
         * schedule creation. Replay that branch's native events once per
         * new start, without changing the cluster identity or TSF. */
        if (g_nanctrl && !memcmp(g_nanpriv + 6, g_nanctrl + 1668, 6)) {
            uint32_t ok = rtw_nan_event_enqueue(21, NULL, 0);
            ok &= rtw_nan_event_enqueue(11, NULL, 0);
            ok &= rtw_nan_event_enqueue(5, NULL, 0);
            g_nanpriv[2350] |= 4;
            rtw_nan_dw_timer_reset(1);
            RTK_LOGS(NOTAG, RTK_LOG_INFO,
                     "[NANSTART] restored same-cluster schedule events=%u\n", (unsigned)ok);
            return;
        }
        /* _rtw_nan_init_priv: DW timer at +2260, OS handle at timer+8. */
        memcpy(&timer, g_nanpriv + 2268, sizeof(timer));
        if (timer && rtos_timer_is_timer_active(timer) == 0) {
            rtw_nan_dw_timer_reset(1);
            RTK_LOGS(NOTAG, RTK_LOG_INFO, "[NANSTART] armed native discovery timer\n");
        }
    }
}

int __wrap_rtw_nan_intfs_init(void)
{
    rtw_nan_intfs_deinit();
    green2_nan_reset_discovery_controller();
    return __real_rtw_nan_intfs_init();
}

void __wrap_rtw_nan_start_api(uint8_t pref, uint8_t bands, uint8_t cmd_type)
{
    int was_enabled = g_nanpriv && (g_nanpriv[2350] & 2);
    __real_rtw_nan_start_api(pref, bands, cmd_type);
    if (!was_enabled) green2_nan_arm_discovery_timer();
}
