/* Read-only telemetry for the hash-pinned lib_wifi_whc_np.a (68248834...).
 * rtw_dynamic_check_work_hdl calls linked_status_chk on the NP command task
 * every 2 s. Native aging and all peer allocation/freeing stay unchanged.
 */
#include "rtw_inic_common.h"
#include <stdint.h>

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
#ifdef AT_NAN_NP_HEAP_TRACE
    green2_nan_heap_snapshot(used);
#endif
}
