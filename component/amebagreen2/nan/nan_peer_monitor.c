/* Read-only telemetry for the hash-pinned lib_wifi_whc_np.a (68248834...).
 * rtw_dynamic_check_work_hdl calls linked_status_chk on the NP command task
 * every 2 s. Native aging and all peer allocation/freeing stay unchanged.
 */
#include "rtw_inic_common.h"
#include <stdint.h>
#ifdef AT_GREEN2_NAN_TICKLESS
#include "FreeRTOS.h"
#endif

extern uint8_t *g_nanpriv;
extern void __real_rtw_nan_linked_status_chk(void);
#ifdef AT_NAN_NP_HEAP_TRACE
extern void green2_nan_heap_snapshot(unsigned used);
#endif

void __wrap_rtw_nan_linked_status_chk(void)
{
    static unsigned ticks;
    static unsigned previous_used = 0xff;
    __real_rtw_nan_linked_status_chk();
#ifdef AT_NAN_RADIO_DIAG
    extern void green2_nan_radio_diag_poll(void);
    green2_nan_radio_diag_poll();
#endif
    if (!g_nanpriv || !(g_nanpriv[2350] & 2)) {
        ticks = 0;
        previous_used = 0xff;
        return;
    }

    /* Native rtw_nan_get_avail_mac_id/stainfo_free use this 16-bit bitmap.
     * It contains peer slots, excluding the two reserved NAN MACIDs. This
     * is a snapshot, not a replacement for the driver's allocation lock. */
    uint16_t slots;
    memcpy(&slots, g_nanpriv + 2324, sizeof(slots));
    unsigned used = 0;
    for (; slots; slots >>= 1) used += slots & 1;
    if (++ticks < 5 && used == previous_used) return;
    ticks = 0;
    previous_used = used;
    RTK_LOGS(NOTAG, RTK_LOG_INFO,
             "[NAN] NP Available Heap Size: %u bytes, peer MACID %u/%u\n",
             (unsigned)rtos_mem_get_free_heap_size(), used,
             (unsigned)wifi_user_config.nan_max_peer_num);
#ifdef AT_GREEN2_NAN_TICKLESS
    extern volatile uint32_t green2_sleep_count, green2_sleep_ms;
    extern volatile uint32_t green2_idle_wfi_count, green2_idle_wfi_ms;
    RTK_LOGS(NOTAG, RTK_LOG_INFO,
             "[NAN-PM] NP tickless=%u sleep=%u sleep_ms=%u idle_wfi=%u idle_ms=%u locks=%08x\n",
             (unsigned)configUSE_TICKLESS_IDLE, (unsigned)green2_sleep_count,
             (unsigned)green2_sleep_ms, (unsigned)green2_idle_wfi_count,
             (unsigned)green2_idle_wfi_ms, (unsigned)pmu_get_wakelock_status());
#endif
#ifdef AT_NAN_NP_HEAP_TRACE
    green2_nan_heap_snapshot(used);
#endif
}
