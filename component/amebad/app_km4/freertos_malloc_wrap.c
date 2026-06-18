/*
 * freertos_malloc_wrap.c — route libc malloc/free/calloc/realloc to the project's FreeRTOS/TLSF
 * heap, via -Wl,--wrap=malloc/free/calloc/realloc on this image.
 *
 * Why: newlib's malloc draws from the tiny 4 KB _sbrk arena (_nl_heap in syscall_config/
 * newlib_syscalls.c — deliberately small, "enough for stdio buffering"). zlib deflate/inflate
 * (~tens of KB) and minizip's 64 KB zip buffer allocate via malloc and would fail/overrun there
 * (a Hard Fault inside zipOpen3/deflate). --wrap is a link-time redirect that catches EVERY libc
 * allocation in the image — including zlib's zcalloc and minizip's ALLOC, which both bottom out in
 * malloc. Linked only into the minizip-enabled (non-QUIC, non-AWS) images.
 *
 * Route through pvPortMalloc/pvPortCalloc (NOT kmalloc(n,0)!): the TLSF heaps are registered with
 * ids 1..N by os_heap_init()->vPortDefineHeapRegions() (freertos_heap_tlsf.c), and id 0 is invalid
 * — kmalloc(n,0) returns NULL. pvPortMalloc passes MM_FLAG_HEAP_ANY (spans all heaps) + 8-byte
 * align, which is exactly what FreeRTOS itself uses; krealloc locates the owning heap from the ptr.
 */
#include <stddef.h>

extern void *pvPortMalloc(size_t size);
extern void  vPortFree(void *ptr);
extern void *pvPortCalloc(size_t nmemb, size_t size);
extern void *krealloc(void *ptr, size_t newsize);

void *__wrap_malloc(size_t n)
{
    return pvPortMalloc(n);
}

void __wrap_free(void *p)
{
    vPortFree(p);
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
    return pvPortCalloc(nmemb, size);   /* MM_FLAG_HEAP_ANY + zero-fill */
}

void *__wrap_realloc(void *p, size_t n)
{
    if (p == NULL) {
        return pvPortMalloc(n);          /* realloc(NULL, n) == malloc(n) */
    }
    if (n == 0) {
        vPortFree(p);                    /* realloc(p, 0) == free(p) */
        return NULL;
    }
    return krealloc(p, n);               /* finds p's owning TLSF heap, reallocs in place/moves */
}
