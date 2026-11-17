/* Opt-in NP diagnostics. Fixed metadata only: never retain/log payloads or
 * keys, allocate memory, or print from an allocation/free hot path. Normal
 * builds do not compile this file or reserve its tables. */
#include "rtw_inic_common.h"
#include "os_wrapper_static_functions.h"
#include "mm.h"
#include <stdint.h>

struct allocation { void *ptr; uintptr_t caller; size_t bytes; };
struct group { uintptr_t caller; size_t bytes; unsigned count; };
static struct allocation live[512];
static struct group groups[96];
static unsigned dropped;

static void forget(void *ptr)
{
    if (!ptr) return;
    uintptr_t flags = mm_lock_save();
    for (unsigned i = 0; i < 512; ++i) {
        if (live[i].ptr == ptr) { live[i].ptr = NULL; break; }
    }
    mm_unlock_restore(flags);
}

static void remember(void *ptr, uintptr_t caller)
{
    if (!ptr) return;
    uintptr_t flags = mm_lock_save();
    unsigned empty = 512;
    for (unsigned i = 0; i < 512; ++i) {
        if (live[i].ptr == ptr) { empty = i; break; }
        if (!live[i].ptr && empty == 512) empty = i;
    }
    if (empty < 512)
        live[empty] = (struct allocation){ptr, caller, kmalloc_size(ptr)};
    else
        ++dropped;
    mm_unlock_restore(flags);
}

#define CALLER ((uintptr_t)__builtin_return_address(0))
extern void *__real_kmalloc(size_t size, uint32_t flags);
extern void __real_kfree(void *ptr);
extern void *__real_krealloc(void *ptr, size_t size);
void *__wrap_kmalloc(size_t size, uint32_t flags)
{
    void *ptr = __real_kmalloc(size, flags);
    remember(ptr, CALLER);
    return ptr;
}
void __wrap_kfree(void *ptr) { forget(ptr); __real_kfree(ptr); }
void *__wrap_krealloc(void *ptr, size_t size)
{
    void *next = __real_krealloc(ptr, size);
    if (next || !size) { forget(ptr); remember(next, CALLER); }
    return next;
}
extern void *__real_rtos_mem_malloc(uint32_t size);
extern void *__real_rtos_mem_zmalloc(uint32_t size);
extern void *__real_rtos_mem_calloc(uint32_t n, uint32_t size);
extern void *__real_rtos_mem_realloc(void *ptr, uint32_t size);
void *__wrap_rtos_mem_malloc(uint32_t size)
{
    void *ptr = __real_rtos_mem_malloc(size); remember(ptr, CALLER); return ptr;
}
void *__wrap_rtos_mem_zmalloc(uint32_t size)
{
    void *ptr = __real_rtos_mem_zmalloc(size); remember(ptr, CALLER); return ptr;
}
void *__wrap_rtos_mem_calloc(uint32_t n, uint32_t size)
{
    void *ptr = __real_rtos_mem_calloc(n, size); remember(ptr, CALLER); return ptr;
}
void *__wrap_rtos_mem_realloc(void *ptr, uint32_t size)
{
    void *next = __real_rtos_mem_realloc(ptr, size);
    if (next || !size) { forget(ptr); remember(next, CALLER); }
    return next;
}

void green2_nan_heap_snapshot(unsigned used)
{
    if (used) return;
    struct component_status os;
    struct mm_usage_info totals = {0};
    unsigned count = 0, nodes = 0, omitted = 0;
    uintptr_t flags = mm_lock_save();
    rtos_static_get_component_status(&os);
    memset(groups, 0, sizeof(groups));
    for (unsigned i = 0; i < 512; ++i) {
        if (!live[i].ptr) continue;
        ++nodes;
        unsigned j;
        for (j = 0; j < count; ++j)
            if (groups[j].caller == live[i].caller && groups[j].bytes == live[i].bytes) break;
        if (j == 96) { ++omitted; continue; }
        if (j == count) groups[count++] = (struct group){live[i].caller, live[i].bytes, 0};
        ++groups[j].count;
    }
    for (unsigned i = 0; i < CONFIG_MM_HEAP_COUNT; ++i) {
        mm_heap_t *heap = g_mem_manager.heaps[i];
        if (!heap || !heap->is_active) continue;
        const mm_allocator_t *a = g_mem_manager.allocators[heap->allocator_id];
        struct mm_usage_info usage;
        if (!a || !a->get_usage_info) continue;
        a->get_usage_info(heap, &usage);
        totals.used_size += usage.used_size; totals.free_size += usage.free_size;
        totals.alloc_node += usage.alloc_node; totals.free_node += usage.free_node;
    }
    unsigned lost = dropped;
    mm_unlock_restore(flags);
    RTK_LOGS(NOTAG, RTK_LOG_INFO,
        "[NANHEAP] free=%u used=%u allocated=%u free_nodes=%u tracked=%u lost=%u omitted=%u\n",
        totals.free_size, totals.used_size, totals.alloc_node, totals.free_node, nodes, lost, omitted);
    RTK_LOGS(NOTAG, RTK_LOG_INFO,
        "[NANHEAP] mutex=%u+%u sema=%u+%u timer=%u+%u\n",
        os.mutex_buf_used_num, os.mutex_dynamic_num, os.sema_buf_used_num,
        os.sema_dynamic_num, os.timer_buf_used_num, os.timer_dynamic_num);
    for (unsigned i = 0; i < count; ++i)
        RTK_LOGS(NOTAG, RTK_LOG_INFO, "[NANHEAP] pc=%x bytes=%u count=%u\n",
            (unsigned)groups[i].caller, (unsigned)groups[i].bytes, groups[i].count);
    RTK_LOGS(NOTAG, RTK_LOG_INFO, "[NANHEAP] end\n");
}
