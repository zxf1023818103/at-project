/*
 * secure_demo.c — the demo Secure-world function for the FreeRTOS TrustZone
 * secure-context feature test. Compiled into the standalone secure image with
 * -mcmse; the cmse_nonsecure_entry functions become SG veneers in .gnu.sgstubs
 * (at CMSE_VENEER_REGION_START 0x0E400400) and are exported to the NS link via
 * --cmse-implib (s_veneers_freertos.o).
 *
 * The veneers run on the calling task's *secure* stack (PSP_S, set up by the
 * FreeRTOS secure context the kernel loaded for that task). secure_demo_compute
 * intentionally keeps a sizeable buffer live on that secure stack across a long
 * busy-spin, so that when a higher-priority NS task preempts mid-call — itself
 * making secure calls on ITS own secure stack — the kernel's save/restore of the
 * per-task secure PSP is what keeps this buffer intact.
 */
#include <stdint.h>
#include <arm_cmse.h>
#include "secure_demo.h"

__attribute__( ( cmse_nonsecure_entry ) ) __attribute__( ( used ) )
uint32_t secure_demo_compute( uint32_t seed, uint32_t tag, uint32_t spin )
{
    /* volatile so the fill/checksum aren't optimised away and the buffer really
     * occupies secure stack across the spin. */
    volatile uint32_t buf[ SECURE_DEMO_BUF_WORDS ];
    uint32_t i;
    uint32_t sum = 0u;
    volatile uint32_t s;

    for( i = 0u; i < SECURE_DEMO_BUF_WORDS; i++ )
    {
        buf[ i ] = SECURE_DEMO_PATTERN( seed, tag, i );
    }

    /* Burn time so an NS SysTick tick (1 kHz) lands inside this call and the
     * kernel preempts to the higher-priority task while buf[] is live here. */
    for( s = 0u; s < spin; s++ )
    {
        __asm volatile ( "nop" );
    }

    for( i = 0u; i < SECURE_DEMO_BUF_WORDS; i++ )
    {
        sum += buf[ i ];
    }

    return sum;
}

__attribute__( ( cmse_nonsecure_entry ) ) __attribute__( ( used ) )
uint32_t secure_demo_psp( void )
{
    uint32_t psp;

    __asm volatile ( "mrs %0, psp" : "=r" ( psp ) );
    return psp;
}
