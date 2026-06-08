/*
 * ngtcp2 heap shim for the AmebaD KM4 image.
 *
 * ngtcp2's default allocator (lib/ngtcp2_mem.c) routes through the C library
 * malloc/free/calloc/realloc, and it is referenced unconditionally via
 * ngtcp2_mem_default() (ngtcp2_conn.c falls back to it when mem == NULL), so the
 * symbols must resolve at link even though we pass a custom mem. This image is
 * built -nostdlib with no newlib _sbrk, so we provide a strong malloc family
 * backed by the FreeRTOS heap (heap_4). heap_4 has no realloc, so we prefix each
 * block with its size to support copy-on-realloc.
 *
 * Defining a strong `malloc` here also pre-empts newlib's malloc (which would
 * drag in _sbrk), and gives newlib's snprintf etc. a working heap.
 */
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

/* 8-byte header keeps the returned payload 8-byte aligned (FreeRTOS heap_4
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

/*
 * Byte-order helpers. ngtcp2_net.h falls back to the system htonl/htons/
 * ntohl/ntohs (it expects <arpa/inet.h>), which this newlib has no <sys/socket.h>
 * to provide. The target is little-endian (Cortex-M33), so these are plain
 * swaps. Strong defs here resolve ngtcp2_conv.c / ngtcp2_crypto.c references.
 */
uint32_t htonl(uint32_t x)
{
    return ((x & 0x000000ffu) << 24) | ((x & 0x0000ff00u) << 8) |
           ((x & 0x00ff0000u) >> 8) | ((x & 0xff000000u) >> 24);
}

uint16_t htons(uint16_t x)
{
    return (uint16_t)(((x & 0x00ffu) << 8) | ((x & 0xff00u) >> 8));
}

uint32_t ntohl(uint32_t x) { return htonl(x); }
uint16_t ntohs(uint16_t x) { return htons(x); }
