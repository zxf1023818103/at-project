/* Peer teardown repairs for lib_wifi_whc_np.a 68248834... / peer object
 * df936272... . The derived object redirects NDP cleanup before it erases
 * selected-NDC ownership, and final storage release after queue teardown.
 * Queue locks come from the vendor static pool with dynamic-heap fallback;
 * use its matching delete API for both allocation kinds.
 */
#include <stdint.h>
#include <string.h>

extern int rtos_mutex_delete_static(void *handle);
extern void rtos_mem_free(void *pointer);
extern void rtw_nan_ndc_info_del(void *peer);
extern void rtw_nan_ndp_data_free(void *peer);

void green2_nan_peer_ndp_free(void *peer)
{
    if (!peer) return;
    /* Aging bypasses ndp_empty_event. The NDC ownership wrapper releases
     * this peer's recorded reference, even if a prior negotiation cleared
     * or replaced its ID. Release before data_free tears down the NDP;
     * repeated cleanup is harmless because ownership has been removed. */
    rtw_nan_ndc_info_del(peer);
    rtw_nan_ndp_data_free(peer);
}

void green2_nan_peer_storage_free(void *peer)
{
    if (peer) {
        /* Fixed 32-bit pointer fields in the pinned NP ABI. Do not read the
         * already deleted scheduler mutex at +44, or access peer after free. */
        uint32_t service_lock, ndi_lock;
        memcpy(&service_lock, (const uint8_t *)peer + 20, sizeof(service_lock));
        memcpy(&ndi_lock, (const uint8_t *)peer + 32, sizeof(ndi_lock));
        if (service_lock)
            rtos_mutex_delete_static((void *)(uintptr_t)service_lock);
        if (ndi_lock && ndi_lock != service_lock)
            rtos_mutex_delete_static((void *)(uintptr_t)ndi_lock);
    }
    rtos_mem_free(peer);
}
