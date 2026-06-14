/*
 * secure_main.c — the standalone FreeRTOS secure world's entry, replacing TF-M's
 * SPM for this feature test. Called from Reset_Handler (secure_startup.c) after
 * .data/.bss init. It:
 *   1. programs the KM4 IDAU/SAU/NVIC + AIRCR via the SAME target_cfg.c reused
 *      verbatim from the TF-M port (component/amebad/app_km4_secure/platform/
 *      rtl8721d/target_cfg.c), so the NS world (app_km4) is correctly attributed
 *      and the .gnu.sgstubs veneer region (0x0E400400) is Non-Secure-Callable;
 *   2. reads the 3-word NS handoff block the KM4 bootloader stashed at
 *      NS_HANDOFF_BLOCK (region_defs.h 0x10002000): [0]=MSP_NS, [1]=NS entry
 *      (app_km4 RamStartFun), [2]=NS VTOR (app_km4 VectorNS);
 *   3. sets MSP_NS / VTOR_NS, enables NS FPU access, and BLXNS-es into the NS
 *      reset vector — identical to what the bootloader's AMEBAD_TFM_BOOT block
 *      expected tfm_s's SPM to do.
 *
 * After the BLXNS the NS world runs FreeRTOS (the TZ-aware GCC_ARM_CM33_NONSECURE
 * port); the secure world is re-entered only through the SG veneers in
 * secure_demo.c / the FreeRTOS SecureContext_* / SecureInit_* port.
 */
#include "rtl8721d_device.h"   /* CMSIS core_cm33 (SCB/SCB_NS/__TZ_set_MSP_NS) */
#include <arm_cmse.h>
#include "target_cfg.h"        /* enable_fault_handlers / system_reset_cfg / sau_and_idau_cfg / nvic_* */
#include "region_defs.h"       /* NS_HANDOFF_BLOCK */

typedef void ( *ns_funcptr_t )( void ) __attribute__( ( cmse_nonsecure_call ) );

void SecureMain( void )
{
    volatile uint32_t * hb = ( volatile uint32_t * ) ( NS_HANDOFF_BLOCK );
    uint32_t ns_msp   = hb[ 0 ];
    uint32_t ns_entry = hb[ 1 ];
    uint32_t ns_vtor  = hb[ 2 ];
    ns_funcptr_t ns_reset;

    /* Secure attribution + banked fault/NVIC config (IDAU/SAU/ITNS/AIRCR).
     * Reused target_cfg.c — payload-independent; same recipe tfm_hal_platform_init
     * runs for the TF-M port. */
    enable_fault_handlers();
    system_reset_cfg();
    sau_and_idau_cfg();
    nvic_interrupt_target_state_cfg();

    /* Let the NS world use the FPU (mirrors the bootloader's pre-jump setup and
     * tfm_s). CP10/CP11 NS access + NS full CPACR. */
    SCB->CPACR    |= 0x00F00000u;
    SCB_NS->CPACR |= 0x00F00000u;
    SCB->NSACR    |= ( ( 1u << 10 ) | ( 1u << 11 ) );

    /* Clear the secure PRIMASK that Reset_Handler set with __disable_irq(). It is
     * BANKED, so leaving it set keeps the *global* execution priority boosted to 0
     * even after the NS world clears its own PRIMASK_NS — which makes the NS
     * start-scheduler SVC unable to activate and escalate straight to a Non-secure
     * HardFault. (TF-M's tfm_hal_platform_init does the same __enable_irq().) */
    __enable_irq();

    /* Hand off to the NS world: VTOR_NS + MSP_NS, then BLXNS to the NS reset. */
    SCB_NS->VTOR = ns_vtor;
    __TZ_set_MSP_NS( ns_msp );
    __DSB();
    __ISB();

    ns_reset = ( ns_funcptr_t ) cmse_nsfptr_create( ns_entry );
    ns_reset();

    /* NS world never returns here. */
    for( ; ; )
    {
    }
}
