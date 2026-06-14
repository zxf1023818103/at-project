/*
 * freertos_secure_test.c — on-target test of the FreeRTOS-Kernel ARM_CM33
 * TrustZone secure-context feature (AmebaD KM4, make SECURE=ON).
 *
 * Proves that a non-secure FreeRTOS task can call a Secure-world function AND that
 * the kernel saves/restores a *per-task* secure stack across preemption:
 *
 *   - vSecureUserTask (prio +2) loops calling secure_demo_compute() with a long
 *     `spin`, so each secure call stays live on this task's secure stack for many
 *     ticks. The veneer fills + checksums a buffer ON THAT SECURE STACK.
 *   - vPreemptorTask (prio +3, higher) wakes every tick and ALSO calls the secure
 *     veneer — forcing a 2nd secure context and guaranteeing it preempts the user
 *     task *mid-secure-call*, running its own secure code on its own secure stack.
 *
 * If the kernel correctly saved the user task's secure PSP before switching and
 * restored it after, the user task's mid-call buffer survives and its checksum
 * matches the NS-side recomputation. If the secure stacks were shared / not
 * restored, the preemptor's secure call would corrupt the buffer -> mismatch.
 *
 * The PASS therefore *requires* the preemptor to also use a secure context
 * (fst_preemptor_calls > 0), so a broken save/restore cannot pass silently.
 */
#include "FreeRTOS.h"
#include "task.h"
#include "secure_demo.h"
#include <stdint.h>

extern void DiagPrintf( const char * fmt, ... );

#define FST_ITERS          100u
#define FST_USER_SPIN      200000u   /* user secure call spans many 1 kHz ticks */
#define FST_PREEMPT_SPIN   20000u

static uint32_t fst_expected( uint32_t seed, uint32_t tag )
{
    uint32_t s = 0u, i;

    for( i = 0u; i < SECURE_DEMO_BUF_WORDS; i++ )
    {
        s += SECURE_DEMO_PATTERN( seed, tag, i );
    }
    return s;
}

static volatile uint32_t fst_preemptor_calls = 0u;
static volatile uint32_t fst_preemptor_fail  = 0u;
static volatile uint32_t fst_preemptor_psp   = 0u;
static volatile uint32_t fst_done            = 0u;

static void vPreemptorTask( void * arg )
{
    ( void ) arg;

    /* This task makes secure calls -> it needs its own secure context. */
    portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE );

    while( fst_done == 0u )
    {
        uint32_t seed = fst_preemptor_calls + 0x00C0FFEEu;
        uint32_t got;

        vTaskDelay( 1 );   /* wake each tick; higher prio -> preempt the user task */
        got = secure_demo_compute( seed, 2u, FST_PREEMPT_SPIN );
        if( got != fst_expected( seed, 2u ) )
        {
            fst_preemptor_fail++;
        }
        fst_preemptor_psp = secure_demo_psp();
        fst_preemptor_calls++;
    }

    vTaskDelete( NULL );
}

static void vSecureUserTask( void * arg )
{
    uint32_t i, mismatches = 0u, user_psp = 0u;

    ( void ) arg;
    portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE );
    DiagPrintf( "[FST] starting secure-context preemption test\n\r" );

    for( i = 0u; i < FST_ITERS; i++ )
    {
        uint32_t got = secure_demo_compute( i, 1u, FST_USER_SPIN );

        if( got != fst_expected( i, 1u ) )
        {
            mismatches++;
            DiagPrintf( "[FST] FAIL (iter %d exp 0x%x got 0x%x)\n\r",
                        i, fst_expected( i, 1u ), got );
        }
        user_psp = secure_demo_psp();
    }

    fst_done = 1u;

    DiagPrintf( "[FST] secure PSP user=0x%x preemptor=0x%x distinct=%s\n\r",
                user_psp, fst_preemptor_psp,
                ( user_psp && fst_preemptor_psp && ( user_psp != fst_preemptor_psp ) )
                    ? "yes" : "no" );

    if( ( mismatches == 0u ) && ( fst_preemptor_calls > 0u ) && ( fst_preemptor_fail == 0u ) )
    {
        DiagPrintf( "[FST] secure-context preemption: PASS (%d iters, both tasks, %d preemptor calls)\n\r",
                    FST_ITERS, fst_preemptor_calls );
    }
    else
    {
        DiagPrintf( "[FST] secure-context preemption: FAIL (mismatch=%d preemptor_calls=%d preemptor_fail=%d)\n\r",
                    mismatches, fst_preemptor_calls, fst_preemptor_fail );
    }

    vTaskDelete( NULL );
}

void freertos_secure_test_start( void )
{
    /* Preemptor first (higher prio) so it's ready to preempt from the start. */
    if( xTaskCreate( vPreemptorTask, "fst_preempt", 1024, NULL,
                     tskIDLE_PRIORITY + 3, NULL ) != pdPASS )
    {
        DiagPrintf( "[FST] xTaskCreate(preemptor) failed\n\r" );
    }
    if( xTaskCreate( vSecureUserTask, "fst_user", 1024, NULL,
                     tskIDLE_PRIORITY + 2, NULL ) != pdPASS )
    {
        DiagPrintf( "[FST] xTaskCreate(user) failed\n\r" );
    }
}
