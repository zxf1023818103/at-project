/* NDC ownership for the SHA-pinned Green2 radio (68248834...). Native
 * reply_accept_event adds a reference for every accepted schedule, including
 * refreshes of the same peer/NDC. Teardown releases only one. Track one native
 * reference per peer, independently of its mutable negotiation ID/flags.
 * The SDK caps peer capacity at 15. No tasks/timers or per-peer allocations.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void __real_rtw_nan_ndc_info_add_new(void *peer);
extern void __real_rtw_nan_ndc_info_del(void *peer);
extern void __real_rtw_nan_schdlr_priv_free(void);
extern void *rtw_nan_ndc_info_exist(const void *id);
extern int rtos_mutex_create_static(void **handle);
extern int rtos_mutex_take(void *handle, uint32_t wait_ms);
extern int rtos_mutex_give(void *handle);
extern void __rtos_critical_enter_os(void);
extern void __rtos_critical_exit_os(void);

enum { OWNER_LIMIT = 15, NDC_ID_OFFSET = 96, NDC_SELECTED_OFFSET = 176 };
struct owner { void *peer; uint8_t id[6]; };
static struct owner owners[OWNER_LIMIT];
/* Created once, retained across NAN stop/start. Serialize before entering
 * the native NDC-list mutex; no native path takes these locks in reverse. */
static void *owner_lock;

static int lock_owners(void)
{
    __rtos_critical_enter_os();
    if (!owner_lock) (void)rtos_mutex_create_static(&owner_lock);
    __rtos_critical_exit_os();
    if (owner_lock && rtos_mutex_take(owner_lock, UINT32_MAX) == 0) return 1;
    printf("[NANFIX] NDC owner lock unavailable\n");
    return 0;
}

static void release_owner(struct owner *owner)
{
    /* The pinned native delete reads only the selected bit at +176 and the
     * six-byte ID at +96, then updates its own locked list/refcount. A small,
     * aligned cleanup view preserves that ABI without temporarily rewriting
     * a live peer's negotiation fields, which may already contain a new ID.
     */
    uint32_t view_words[45] = {0};
    uint8_t *view = (uint8_t *)view_words;
    memcpy(view + NDC_ID_OFFSET, owner->id, sizeof(owner->id));
    view[NDC_SELECTED_OFFSET] = 1;
    __real_rtw_nan_ndc_info_del(view);
#ifdef AT_NAN_NP_HEAP_TRACE
    printf("[NDCREF] released owner=%u\n", (unsigned)(owner - owners));
#endif
    memset(owner, 0, sizeof(*owner));
}

void __wrap_rtw_nan_ndc_info_add_new(void *opaque)
{
    uint8_t *peer = opaque;
    struct owner *slot = NULL;
    if (!peer || !lock_owners()) return;
    for (unsigned i = 0; i < OWNER_LIMIT; ++i) {
        if (owners[i].peer == peer) { slot = &owners[i]; break; }
        if (!owners[i].peer && !slot) slot = &owners[i];
    }
    if (!slot) {
        /* Same failure semantics as native allocation failure: register no
         * reference. Never add one that cannot subsequently be released. */
        printf("[NANFIX] NDC owner table full\n");
        goto done;
    }
    if (slot->peer) {
        if (!memcmp(slot->id, peer + NDC_ID_OFFSET, sizeof(slot->id))) {
#ifdef AT_NAN_NP_HEAP_TRACE
            printf("[NDCREF] duplicate skipped owner=%u\n", (unsigned)(slot - owners));
#endif
            goto done;
        }
        release_owner(slot);
    }
    __real_rtw_nan_ndc_info_add_new(peer);
    if (rtw_nan_ndc_info_exist(peer + NDC_ID_OFFSET)) {
        memcpy(slot->id, peer + NDC_ID_OFFSET, sizeof(slot->id));
        slot->peer = peer;
#ifdef AT_NAN_NP_HEAP_TRACE
        printf("[NDCREF] acquired owner=%u\n", (unsigned)(slot - owners));
#endif
    }
done:
    (void)rtos_mutex_give(owner_lock);
}

void __wrap_rtw_nan_ndc_info_del(void *opaque)
{
    uint8_t *peer = opaque;
    if (!peer || !lock_owners()) return;
    for (unsigned i = 0; i < OWNER_LIMIT; ++i) {
        if (owners[i].peer == peer) { release_owner(&owners[i]); break; }
    }
    /* Repeated cleanup or a failed add owns no native reference. Clearing
     * this one bit matches successful native delete, retaining other flags. */
    peer[NDC_SELECTED_OFFSET] &= (uint8_t)~1u;
    (void)rtos_mutex_give(owner_lock);
}

void __wrap_rtw_nan_schdlr_priv_free(void)
{
    /* NAN stop disables discovery, then native scheduler teardown frees ALL
     * NDC entries and deletes their list mutex BEFORE peer queue teardown.
     * Relinquish ownership to that bulk cleanup; later peer delete must not
     * look up an NDC using the already-deleted native mutex. The native stop
     * sequence serializes scheduler destruction; do not hold our lock across
     * its scheduler callbacks. The retained owner mutex serves the next start.
     */
    if (lock_owners()) {
        memset(owners, 0, sizeof(owners));
        (void)rtos_mutex_give(owner_lock);
    }
    __real_rtw_nan_schdlr_priv_free();
}
