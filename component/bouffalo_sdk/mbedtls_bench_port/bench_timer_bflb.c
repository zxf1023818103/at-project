/*
 * bench_timer_bflb.c — RISC-V (bl602/bl616, T-Head RV32) strong override of the mbedtls_bench
 * cycle-timer seam (component/mbedtls_bench_config). The shared bench carries an ARM Cortex-M
 * SysTick default; RISC-V has no SCS block at 0xE000E0xx, so read the 64-bit mcycle counter
 * (CSR 0xB00 lo / 0xB80 hi — runs by default in M-mode, mcountinhibit reset = 0) and take the
 * frequency from the bflb clock API.
 */
#include "mbedtls_bench.h"
#include "bflb_clock.h"

static unsigned int g_hz;

void bench_timer_init( void )
{
    g_hz = (unsigned int) bflb_clk_get_system_clock( BFLB_SYSTEM_CPU_CLK );
}

unsigned int bench_timer_hz( void ) { return g_hz; }

unsigned long long bench_timer_now( void )
{
    /* RV32: mcycle is 64-bit, read as {mcycleh, mcycle}. Re-read the high word to guard the
     * low-word wrap between the two CSR reads. */
    unsigned int hi, lo, hi2;
    do {
        __asm__ volatile ( "csrr %0, 0xB80" : "=r"( hi ) );   /* mcycleh */
        __asm__ volatile ( "csrr %0, 0xB00" : "=r"( lo ) );   /* mcycle  */
        __asm__ volatile ( "csrr %0, 0xB80" : "=r"( hi2 ) );
    } while ( hi != hi2 );
    return ( (unsigned long long) hi << 32 ) | lo;
}
