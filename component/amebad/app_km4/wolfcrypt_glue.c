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

extern void DiagPrintf(const char *fmt, ...);

#define SYST_CVR (*(volatile unsigned int *)0xE000E018u)   /* SysTick current value */

/* wolfSSL XMALLOC_USER hooks -> FreeRTOS heap. */
void *XMALLOC(size_t n, void *heap, int type)
{ (void)heap; (void)type; return pvPortMalloc(n); }
void  XFREE(void *p, void *heap, int type)
{ (void)heap; (void)type; if (p) vPortFree(p); }
void *XREALLOC(void *p, size_t n, void *heap, int type)
{
    /* heap_4 has no realloc; the wolfCrypt/TLS configs here never hit it.
     * (ngtcp2's realloc is the separate size-prefixed shim in ngtcp2_port.c.) */
    (void)p; (void)n; (void)heap; (void)type;
    DiagPrintf("[WGL] XREALLOC unexpected\n\r");
    return NULL;
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
