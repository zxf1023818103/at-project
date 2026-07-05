/*
 * mbedtls_bench.h — cross-platform upstream Mbed TLS microbenchmark ([MCB] scores).
 */
#ifndef MBEDTLS_BENCH_H
#define MBEDTLS_BENCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Run the benchmark (prints [MCB] cyc/B + KB/s + op/s over at_log). Blocks for several
 * seconds — the RSA-2048 keygen is slow on an MCU. */
void mbedtls_bench( void );

/* Register the `bench` FreeRTOS-CLI console command (runs mbedtls_bench()). */
void bench_cli_register( void );

/* ---- Cycle-timer seam. The shared bench provides a WEAK ARM Cortex-M SysTick
 *      implementation (any ARMv7-M/ARMv8-M core); a RISC-V platform (bl602) supplies a
 *      STRONG override from its port (mcycle CSR), since the SCS block at 0xE000E0xx is
 *      ARM-only. ---- */
void               bench_timer_init( void );   /* latch the counter frequency */
unsigned long long bench_timer_now( void );    /* free-running cycle count */
unsigned int       bench_timer_hz( void );     /* that counter's frequency (Hz) */

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_BENCH_H */
