/* Dplus secure TRNG port. Register definitions: SDK ameba_trng.h/hal_platform.h.
 * Health-test failures and timeouts fail closed; there is no software fallback. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "tfm_hal_device_header.h"
#define REG32(a) (*(volatile uint32_t *)(a))
int dplus_secure_random(unsigned char *out, size_t len)
{
    static int initialized;
    if (!initialized) {
        ((void (*)(void))0xc929)();
        /* SDK BOOT_TRNG_ParaSet overwrites RCT1 with 0x30f (cutoff 3).
         * Restore ameba_trng.h's documented reset cutoff 5 for 8-bit samples;
         * keep RCT0, RCT1 and APT enabled. The boot override otherwise leaves
         * frequent sticky RCT1 failures during continuous generation. */
        REG32(0x51101114) = (REG32(0x51101114) & ~0xff00u) | 0x500u;
        /* Start a new health-test run, discarding ROM-boot FIFO contents and
         * sticky health flags. Never clear a failure after returning entropy. */
        REG32(0x51101008) = 0x1f;
        REG32(0x51101038) = 1;
        __DSB();
        /* Reset crosses into the oscillator clock domain. Do not inspect the
         * previous run's sticky status before that asynchronous reset settles. */
        uint32_t reset_tick = ((uint32_t (*)(void))0xc479)();
        while ((uint32_t)(((uint32_t (*)(void))0xc479)()-reset_tick) < 33) {}
        initialized = 1;
    }
    uint32_t started = ((uint32_t (*)(void))0xc479)(); /* 32768 Hz AON timer */
    while (len) {
        unsigned level;
        do {
            level = REG32(0x5110100c) & 15;
            if (level == 15 || (REG32(0x51101030) & 0x07000000)) return -1;
            if ((uint32_t)(((uint32_t (*)(void))0xc479)() - started) > 32768) return -1;
        } while (!level);
        uint32_t word = REG32(0x51101010);
        for (unsigned i = 0; i < 4 && len; ++i, --len) { *out++ = word; word >>= 8; }
    }
    return 0;
}
int mbedtls_hardware_poll(void *ctx, unsigned char *out, size_t len, size_t *olen)
{
    (void)ctx; *olen = 0;
    if (dplus_secure_random(out, len)) {
        printf("[TF-M] TRNG failed len=%u fifo=%x health=%x\n", (unsigned)len,
               (unsigned)REG32(0x5110100c), (unsigned)REG32(0x51101030));
        return -0x003c;
    }
    *olen = len; return 0;
}
