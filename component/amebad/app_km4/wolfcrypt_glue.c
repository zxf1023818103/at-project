/*
 * wolfcrypt_glue.c — wolfSSL platform glue for the AmebaD KM4 image.
 *
 * The pieces wolfSSL needs whenever it is linked, independent of the
 * (optional, heavy) wolfCrypt benchmark in wolfcrypt_bench.c:
 *   - XMALLOC/XFREE/XREALLOC (WOLFSSL_USER_SETTINGS -> XMALLOC_USER) routed to
 *     the FreeRTOS heap (this -nostdlib image has no newlib _sbrk).
 *   - amebad_bench_rng_block (CUSTOM_RAND_GENERATE_BLOCK) + a reseed hook.
 *
 * Split out so the QUIC image (AMEBAD_ENABLE_QUIC) can drop wolfcrypt_bench.c
 * (and the ROM-mbedTLS / iperf diagnostics) — there is no --gc-sections on this
 * link, so unused code is only removed by excluding the .c file. Keeping just
 * this small TU shrinks the KM4 image2 .text, which also shifts the XIP boot
 * path (see doc/amebad_xip_layout_boot_hang.md).
 */
#include "FreeRTOS.h"

#include <stddef.h>
#include <string.h>

extern void DiagPrintf(const char *fmt, ...);

#define SYST_CVR (*(volatile unsigned int *)0xE000E018u)   /* SysTick current value */

/* wolfSSL XMALLOC_USER hooks -> FreeRTOS heap (heap_4, which has no realloc).
 *
 * We prefix every block with an 8-byte header holding the payload size so
 * XREALLOC can copy the old contents (TLS 1.3 grows dynamic buffers — the
 * handshake actually exercises realloc, unlike the wolfCrypt-only build). The
 * 8-byte header keeps the returned pointer 8-byte aligned (== portBYTE_ALIGNMENT).
 * All three hooks are consistent, and wolfSSL routes every allocation through
 * them, so there is no cross-allocator free. (ngtcp2 core has its own separate
 * size-prefixed shim in ngtcp2_port.c.) */
#define WGL_HDR 8u

void *XMALLOC(size_t n, void *heap, int type)
{
    unsigned char *base;
    (void)heap; (void)type;
    base = pvPortMalloc(n + WGL_HDR);
    if (base == NULL) return NULL;
    *(size_t *)(void *)base = n;
    return base + WGL_HDR;
}

void XFREE(void *p, void *heap, int type)
{
    (void)heap; (void)type;
    if (p) vPortFree((unsigned char *)p - WGL_HDR);
}

void *XREALLOC(void *p, size_t n, void *heap, int type)
{
    unsigned char *base;
    size_t oldn;
    void *np;

    if (p == NULL) return XMALLOC(n, heap, type);

    base = (unsigned char *)p - WGL_HDR;
    oldn = *(size_t *)(void *)base;
    if (n <= oldn) return p;                 /* shrink/same: reuse in place */

    np = XMALLOC(n, heap, type);
    if (np == NULL) return NULL;
    memcpy(np, p, oldn);
    vPortFree(base);
    return np;
}

/* ---- RNG (CUSTOM_RAND_GENERATE_BLOCK). NOT crypto-grade — a fast xorshift so
 *      keygen/blinding have entropy. A real TRNG is a follow-up. ---- */
static unsigned int s_rng = 0xA5C3F19Du;

int amebad_bench_rng_block(unsigned char *out, unsigned int sz)
{
    unsigned int i;
    for (i = 0; i < sz; i++) {
        s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
        out[i] = (unsigned char)(s_rng >> 24);
    }
    return 0;
}

/* Mix extra entropy into the xorshift state so the per-boot key/RNG stream
 * differs run to run (used by the QUIC self-test to vary client randoms/CIDs). */
void amebad_bench_rng_reseed(unsigned int seed)
{
    s_rng ^= seed * 2654435761u;       /* Knuth multiplicative mix */
    s_rng ^= (unsigned int)SYST_CVR;   /* SysTick current value (free-running) */
    if (s_rng == 0u) {
        s_rng = 0xA5C3F19Du;           /* xorshift must not be seeded to 0 */
    }
}
