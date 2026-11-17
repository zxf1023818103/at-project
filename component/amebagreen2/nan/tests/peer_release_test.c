#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void green2_nan_peer_storage_free(void *);
void green2_nan_peer_ndp_free(void *);
static void *locks[4];
static unsigned deleted, storage_frees;
static unsigned cluster_refs, cluster_frees, ndp_frees;

/* Model the native selected-NDC guard and shared reference semantics. The
 * data cleanup erases its lookup ID/flag; ordering is essential here. */
void rtw_nan_ndc_info_del(void *opaque)
{
    uint8_t *peer = opaque;
    if (!(peer[176] & 1)) return;
    assert(peer[96] == 0x42 && cluster_refs);
    if (!--cluster_refs) ++cluster_frees;
    peer[176] &= ~1u;
}

void rtw_nan_ndp_data_free(void *opaque)
{
    uint8_t *peer = opaque;
    assert(!(peer[176] & 1));
    memset(peer + 93, 0, 10);
    peer[176] = 0;
    ++ndp_frees;
}

static void test_ndc_release(void)
{
    uint8_t first[544] = {0}, second[544] = {0}, partial[544] = {0};
    first[96] = second[96] = 0x42;
    first[176] = second[176] = 1;
    cluster_refs = 2;
    green2_nan_peer_ndp_free(first);  /* Timeout/aging without NDP-empty. */
    assert(cluster_refs == 1 && cluster_frees == 0 && !first[96]);
    rtw_nan_ndc_info_del(second);    /* Normal NDP-empty already released. */
    assert(cluster_refs == 0 && cluster_frees == 1);
    green2_nan_peer_ndp_free(second);  /* Later retirement must not double-drop. */
    green2_nan_peer_ndp_free(first);   /* Repeated cleanup stays idempotent. */
    green2_nan_peer_ndp_free(partial); /* Failed allocation has no NDC. */
    green2_nan_peer_ndp_free(NULL);
    assert(cluster_refs == 0 && cluster_frees == 1 && ndp_frees == 4);
}

int rtos_mutex_delete_static(void *handle)
{
    uintptr_t id = (uintptr_t)handle;
    assert(id > 0 && id < 4 && locks[id]);
    free(locks[id]); locks[id] = NULL; ++deleted;
    return 0;
}

void rtos_mem_free(void *peer)
{
    /* The destructor must finish both lock releases before freeing storage. */
    assert(!locks[1] && !locks[2]);
    free(peer);
    ++storage_frees;
}

static void destroy(uint32_t service, uint32_t ndi)
{
    uint8_t *peer = calloc(1, 544);
    assert(peer);
    if (service) { locks[service] = malloc(96); assert(locks[service]); }
    if (ndi && ndi != service) { locks[ndi] = malloc(96); assert(locks[ndi]); }
    memcpy(peer + 20, &service, sizeof(service));
    memcpy(peer + 32, &ndi, sizeof(ndi));
    /* Native teardown already deleted this third mutex. Any second deletion
     * would trip the mock's ownership check (and ASan for real heap objects). */
    uint32_t scheduler = 3;
    memcpy(peer + 44, &scheduler, sizeof(scheduler));
    unsigned before = deleted;
    green2_nan_peer_storage_free(peer);
    assert(deleted - before == (service != 0) + (ndi != 0 && ndi != service));
}

int main(void)
{
    test_ndc_release();
    /* Ordinary retirement and repeated failed-allocation rollback share the
     * same destructor. Exercise enough lifetimes to expose lost ownership. */
    for (unsigned i = 0; i < 1000; ++i) destroy(1, 2);
    destroy(0, 2); destroy(1, 0); destroy(0, 0); destroy(1, 1);
    green2_nan_peer_storage_free(NULL);
    assert(storage_frees == 1005 && deleted == 2003);
    return 0;
}
