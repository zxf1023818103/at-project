/*
 * libc_compat.c — libc/BSD-socket glue the KM4 image needs, consolidated here
 * (moved out of component/amebad/ngtcp2/ngtcp2_port.c so it's shared by every
 * KM4 program, not just QUIC). Two independent groups:
 *
 *   1. malloc family  — a strong malloc/free/calloc/realloc backed by the
 *      FreeRTOS heap (heap_4). The image is built -nostdlib with no newlib
 *      _sbrk-backed app heap, and code that wants C malloc (ngtcp2's default
 *      ngtcp2_mem, newlib snprintf, ...) must resolve it. Defining a STRONG
 *      malloc here also pre-empts newlib's own malloc (which would otherwise
 *      grow through _sbrk). heap_4 has no realloc, so each block is prefixed
 *      with its size to support copy-on-realloc.
 *
 *   2. byte-order helpers — bare htonl/htons/ntohl/ntohs, which BSD-socket code
 *      (ngtcp2_net.h, and anything expecting <arpa/inet.h>) calls. We do NOT
 *      re-implement the swap: these delegate to FreeRTOS+TCP's own
 *      FreeRTOS_htonl/htons macros so there is a single source of truth for the
 *      project's network byte order.
 *
 * Compiled into the amebad_syscalls OBJECT lib and force-linked into the KM4
 * image (its strong defs beat libc's), alongside newlib_syscalls.c.
 */
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"        /* pvPortMalloc / vPortFree (heap_4) */
#include "FreeRTOS_IP.h"     /* FreeRTOS_htonl / FreeRTOS_htons (network byte order) */

/* ===== 1. malloc family over the FreeRTOS heap ============================ *
 * 8-byte header keeps the returned payload 8-byte aligned (FreeRTOS heap_4
 * returns 8-byte-aligned blocks; portBYTE_ALIGNMENT is 8 on Cortex-M). */
typedef struct {
    size_t size;
    size_t _pad;
} mem_hdr_t;

void *malloc(size_t size)
{
    mem_hdr_t *h;

    if (size == 0) {
        size = 1;
    }
    h = (mem_hdr_t *)pvPortMalloc(sizeof(mem_hdr_t) + size);
    if (h == NULL) {
        return NULL;
    }
    h->size = size;
    return (void *)(h + 1);
}

void free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    vPortFree((void *)(((mem_hdr_t *)ptr) - 1));
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p;

    /* overflow guard */
    if (nmemb != 0 && total / nmemb != size) {
        return NULL;
    }
    p = malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size)
{
    mem_hdr_t *h;
    size_t old;
    void *np;

    if (ptr == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    h = ((mem_hdr_t *)ptr) - 1;
    old = h->size;
    np = malloc(size);
    if (np == NULL) {
        return NULL;
    }
    memcpy(np, ptr, old < size ? old : size);
    free(ptr);
    return np;
}

/* ===== 2. BSD byte-order helpers (delegate to FreeRTOS+TCP) =============== *
 * ngtcp2_net.h's non-WIN32 path expands ngtcp2_htonl(N) -> htonl(N) etc., and
 * it does not include <arpa/inet.h> (HAVE_ARPA_INET_H is unset), so it needs
 * these as real symbols. Bodies reuse FreeRTOS+TCP's FreeRTOS_htonl/htons macros
 * rather than open-coding the swap. */
uint32_t htonl(uint32_t x) { return FreeRTOS_htonl(x); }
uint16_t htons(uint16_t x) { return FreeRTOS_htons(x); }
uint32_t ntohl(uint32_t x) { return FreeRTOS_ntohl(x); }
uint16_t ntohs(uint16_t x) { return FreeRTOS_ntohs(x); }
