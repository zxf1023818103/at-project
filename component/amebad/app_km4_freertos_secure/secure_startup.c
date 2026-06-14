/*
 * secure_startup.c — RTL8721D KM4 standalone FreeRTOS secure-image startup (GCC).
 *
 * Deliberately self-contained: Reset_Handler does explicit .data copy + .bss zero
 * + ARMv8-M stack sealing, then jumps straight to SecureMain() — NO newlib _start
 * / __cmsis_start. The secure world runs no C library and no scheduler; it only
 * programs SAU/IDAU and hands off to the NS world, then services SG-veneer calls.
 *
 * The bootloader's secure-jump (component/amebad/bootloader_km4/boot_flash_hp.c)
 * reads vector[0]=__INITIAL_SP (→ __set_MSP) and vector[1]=Reset_Handler, exactly
 * like TF-M's tfm_s. The vector table is the first thing in the cached-XIP window
 * (S_CODE_START 0x0E400000); .gnu.sgstubs follows at 0x0E400400.
 */
#include "rtl8721d_device.h"   /* CMSIS core_cm33 (SCB/__set_*/__TZ_*) + Ameba device */

/* Linker-provided (rlx8721d_freertos_s.ld). */
extern uint32_t __INITIAL_SP;
extern uint32_t __STACK_LIMIT;
extern uint32_t __STACK_SEAL;
extern uint32_t __etext;          /* .data load address (LMA, in the XIP window) */
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern uint32_t __vectors_start__;

extern void SecureMain( void );   /* secure_main.c */

__attribute__( ( noreturn ) ) void Reset_Handler( void );

#define SECURE_WEAK_HANDLER( name )       \
    void __attribute__( ( weak ) ) name( void ); \
    void name( void ) { for( ; ; ) { } }

SECURE_WEAK_HANDLER( NMI_Handler )
SECURE_WEAK_HANDLER( HardFault_Handler )
SECURE_WEAK_HANDLER( MemManage_Handler )
SECURE_WEAK_HANDLER( BusFault_Handler )
SECURE_WEAK_HANDLER( UsageFault_Handler )
SECURE_WEAK_HANDLER( SecureFault_Handler )
SECURE_WEAK_HANDLER( SVC_Handler )
SECURE_WEAK_HANDLER( DebugMon_Handler )
SECURE_WEAK_HANDLER( PendSV_Handler )
SECURE_WEAK_HANDLER( SysTick_Handler )

typedef void ( *vector_t )( void );

/* 16-entry core vector table — the secure world enables no external IRQs (all are
 * targeted Non-Secure by target_cfg's NVIC ITNS), so no IRQ slots are needed. */
const vector_t __secure_vectors[] __attribute__( ( section( ".vectors" ), used ) ) =
{
    ( vector_t ) ( &__INITIAL_SP ),   /* [0]  initial MSP (read by the bootloader) */
    Reset_Handler,                    /* [1]  reset    (read by the bootloader)    */
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    SecureFault_Handler,
    0, 0, 0,                          /* reserved 8..10 */
    SVC_Handler,
    DebugMon_Handler,
    0,                                /* reserved 13 */
    PendSV_Handler,
    SysTick_Handler,
};

__attribute__( ( noreturn ) ) void Reset_Handler( void )
{
    uint32_t * src;
    uint32_t * dst;

    __disable_irq();

    /* Secure stack limits + seal (ARMv8-M). MSP was set by the bootloader to
     * __INITIAL_SP before the secure-jump; mirror PSP/limits here. */
    __set_MSPLIM( ( uint32_t ) ( &__STACK_LIMIT ) );
    __set_PSP( ( uint32_t ) ( &__INITIAL_SP ) );
    __set_PSPLIM( ( uint32_t ) ( &__STACK_LIMIT ) );

    /* ARMv8-M stack seal (this CMSIS lacks __TZ_set_STACKSEAL_S — write the two
     * 0xFEF5EDA5 seal words directly). */
    {
        volatile uint32_t * seal = ( volatile uint32_t * ) ( &__STACK_SEAL );
        seal[ 0 ] = 0xFEF5EDA5u;
        seal[ 1 ] = 0xFEF5EDA5u;
    }

    /* Copy .data from its flash LMA (cached-XIP window) to S_DATA. */
    src = &__etext;
    for( dst = &__data_start__; dst < &__data_end__; )
    {
        *dst++ = *src++;
    }

    /* Zero .bss (includes the FreeRTOS secure heap ucHeap + xSecureContexts). */
    for( dst = &__bss_start__; dst < &__bss_end__; )
    {
        *dst++ = 0u;
    }

    /* Secure vector table base + FPU (full access, S + NS). */
#if defined( __VTOR_PRESENT ) && ( __VTOR_PRESENT == 1U )
    SCB->VTOR = ( uint32_t ) ( &__vectors_start__ );
#endif
    SCB->CPACR |= ( ( 3U << 20U ) | ( 3U << 22U ) );   /* CP10/CP11 full access */
    __DSB();
    __ISB();

    SecureMain();

    for( ; ; )
    {
    }
}
