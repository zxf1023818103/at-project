#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void __wrap_rtw_nan_ndc_info_add_new(void *);
void __wrap_rtw_nan_ndc_info_del(void *);
void __wrap_rtw_nan_schdlr_priv_free(void);
struct cluster { uint8_t id[6]; unsigned refs; };
static struct cluster *clusters[16];
static unsigned allocations, frees, native_adds, native_dels, locked, critical;
static int fail_lock = 1, fail_allocation, scheduler_alive = 1;

void __rtos_critical_enter_os(void) { assert(!critical); critical = 1; }
void __rtos_critical_exit_os(void) { assert(critical); critical = 0; }
int rtos_mutex_create_static(void **handle)
{
    assert(critical);
    if (fail_lock) { fail_lock = 0; return -1; }
    *handle = &locked;
    return 0;
}
int rtos_mutex_take(void *handle, uint32_t wait)
{
    assert(handle == &locked && !locked && !critical && wait == UINT32_MAX);
    locked = 1; return 0;
}
int rtos_mutex_give(void *handle)
{
    assert(handle == &locked && locked); locked = 0; return 0;
}
static struct cluster **find(const uint8_t *id)
{
    for (unsigned i = 0; i < 16; ++i)
        if (clusters[i] && !memcmp(clusters[i]->id, id, 6)) return &clusters[i];
    return NULL;
}
void *rtw_nan_ndc_info_exist(const void *id)
{
    assert(locked && scheduler_alive);
    struct cluster **slot = find(id); return slot ? *slot : NULL;
}
void __real_rtw_nan_ndc_info_add_new(void *opaque)
{
    uint8_t *peer = opaque; assert(locked && scheduler_alive); ++native_adds;
    struct cluster **slot = find(peer + 96);
    if (slot) { ++(*slot)->refs; return; }
    if (fail_allocation) { fail_allocation = 0; return; }
    for (unsigned i = 0; i < 16; ++i) if (!clusters[i]) {
        clusters[i] = calloc(1, sizeof(*clusters[i])); assert(clusters[i]);
        memcpy(clusters[i]->id, peer + 96, 6); clusters[i]->refs = 1;
        ++allocations; return;
    }
    assert(0);
}
void __real_rtw_nan_ndc_info_del(void *opaque)
{
    uint8_t *peer = opaque;
    assert(locked && scheduler_alive && (peer[176] & 1)); ++native_dels;
    struct cluster **slot = find(peer + 96); assert(slot && (*slot)->refs);
    if (!--(*slot)->refs) { free(*slot); *slot = NULL; ++frees; }
    peer[176] &= (uint8_t)~1u;
}
void __real_rtw_nan_schdlr_priv_free(void)
{
    assert(!locked && scheduler_alive);
    for (unsigned i = 0; i < 16; ++i) if (clusters[i]) {
        free(clusters[i]); clusters[i] = NULL; ++frees;
    }
    scheduler_alive = 0; /* Native NDC-list mutex has also been deleted. */
}
static unsigned refs(unsigned id)
{
    uint8_t key[6] = {0}; key[0] = id;
    struct cluster **slot = find(key); return slot ? (*slot)->refs : 0;
}
static void select_id(uint8_t *peer, unsigned id)
{
    memset(peer + 96, 0, 6); peer[96] = id; peer[176] = 0xa1;
}
int main(void)
{
    uint8_t peers[16][544] = {{0}};
    __wrap_rtw_nan_ndc_info_add_new(NULL); __wrap_rtw_nan_ndc_info_del(NULL);
    select_id(peers[0], 1);
    __wrap_rtw_nan_ndc_info_add_new(peers[0]); /* Lock allocation fails. */
    assert(native_adds == 0);
    fail_allocation = 1;
    __wrap_rtw_nan_ndc_info_add_new(peers[0]); /* Native node allocation fails. */
    assert(refs(1) == 0);
    __wrap_rtw_nan_ndc_info_del(peers[0]); assert(native_dels == 0);
    select_id(peers[0], 1); __wrap_rtw_nan_ndc_info_add_new(peers[0]);
    unsigned before = native_adds;
    for (unsigned i = 0; i < 100; ++i) __wrap_rtw_nan_ndc_info_add_new(peers[0]);
    assert(native_adds == before && refs(1) == 1);
    select_id(peers[1], 1); __wrap_rtw_nan_ndc_info_add_new(peers[1]);
    assert(refs(1) == 2); /* Different peers really share this NDC. */
    select_id(peers[0], 2); __wrap_rtw_nan_ndc_info_add_new(peers[0]);
    assert(refs(1) == 1 && refs(2) == 1); /* Transfer only this peer's ownership. */
    memset(peers[0] + 96, 0, 6); peers[0][176] = 0xa0;
    __wrap_rtw_nan_ndc_info_del(peers[0]); /* Mutable ID/flag already cleared. */
    assert(refs(2) == 0 && refs(1) == 1 && peers[0][176] == 0xa0);
    __wrap_rtw_nan_ndc_info_del(peers[0]);
    __wrap_rtw_nan_ndc_info_del(peers[1]); assert(refs(1) == 0);
    for (unsigned i = 0; i < 15; ++i) {
        select_id(peers[i], 3); __wrap_rtw_nan_ndc_info_add_new(peers[i]);
    }
    assert(refs(3) == 15); before = native_adds;
    select_id(peers[15], 3); __wrap_rtw_nan_ndc_info_add_new(peers[15]);
    assert(native_adds == before && refs(3) == 15);
    __wrap_rtw_nan_ndc_info_del(peers[15]); assert(refs(3) == 15);
    for (unsigned i = 0; i < 15; ++i) __wrap_rtw_nan_ndc_info_del(peers[i]);
    select_id(peers[0], 5); __wrap_rtw_nan_ndc_info_add_new(peers[0]);
    select_id(peers[1], 5); __wrap_rtw_nan_ndc_info_add_new(peers[1]);
    __wrap_rtw_nan_schdlr_priv_free(); /* Stop with two live owners. */
    assert(refs(5) == 0); before = native_dels;
    __wrap_rtw_nan_ndc_info_del(peers[0]);
    __wrap_rtw_nan_ndc_info_del(peers[1]);
    assert(native_dels == before); /* No lookup on the deleted list mutex. */
    scheduler_alive = 1; /* Start again, reusing the peer storage and NDC ID. */
    select_id(peers[0], 5); __wrap_rtw_nan_ndc_info_add_new(peers[0]);
    assert(refs(5) == 1);
    __wrap_rtw_nan_ndc_info_del(peers[0]);
    for (unsigned i = 0; i < 1000; ++i) {
        select_id(peers[0], 4); __wrap_rtw_nan_ndc_info_add_new(peers[0]);
        __wrap_rtw_nan_ndc_info_add_new(peers[0]);
        __wrap_rtw_nan_ndc_info_del(peers[0]);
    }
    assert(allocations == frees && !locked && !critical);
    return 0;
}
