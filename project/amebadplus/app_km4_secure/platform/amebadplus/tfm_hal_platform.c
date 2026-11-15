/*
 * AMEBADPLUS KM4 — TF-M platform HAL.
 * Brings up the secure-side attribution (IDAU/SAU/NVIC) via target_cfg and a
 * console for TF-M logging. Clocks are already up (Ameba boot chain).
 */
#include "amebadplus_device.h"
#include "tfm_hal_platform.h"
#include "target_cfg.h"
#include "region_defs.h"
#include "uart_stdout.h"

__attribute__((noreturn)) void __wrap_tfm_core_panic(void)
{
    uint32_t pc = (uint32_t)__builtin_return_address(0);
    const char *s = "[TF-M] panic caller=";
    while (*s) ((void (*)(unsigned char))0xaba1)(*s++);
    for (int i=28; i>=0; i-=4) ((void (*)(unsigned char))0xaba1)("0123456789abcdef"[(pc>>i)&15]);
    ((void (*)(unsigned char))0xaba1)('\n');
    __disable_irq(); for (;;) __WFE();
}

/* Memory region limits (BL2 OFF -> no header offset); from region_defs.h. */
const struct memory_region_limits memory_regions = {
    .non_secure_code_start      = NS_CODE_START,
    .non_secure_partition_base  = NS_PARTITION_START,
    .non_secure_partition_limit = NS_PARTITION_START + NS_PARTITION_SIZE - 1,
    .veneer_base                = CMSE_VENEER_REGION_START,
    .veneer_limit               = CMSE_VENEER_REGION_START + CMSE_VENEER_REGION_SIZE - 1,
};

enum tfm_hal_status_t tfm_hal_platform_init(void)
{
    extern void dplus_secure_flash_init(void);
    dplus_secure_flash_init();
    __enable_irq();

    /* Secure attribution + banked fault/NVIC config (IDAU/SAU/ITNS/AIRCR). */
    enable_fault_handlers();
    system_reset_cfg();
    sau_and_idau_cfg();
    nvic_interrupt_target_state_cfg();
    nvic_interrupt_enable();

    /* TF-M log console (routes to the Ameba LOGUART via Driver_USART0). */
    stdio_init();

    return TFM_HAL_SUCCESS;
}

/* The bootloader writes a 4-word NS handoff block at NS_HANDOFF_BLOCK:
 * [0]=MSP_NS, [1]=NS entry (app_km4 RamStartFun), [2]=NS VTOR (app_km4 VectorNS), [3]=active APP_ALL physical offset.
 * The NS world is app_km4 (NSPE), a Dplus image2 whose reset vector is
 * resolved at runtime — not a plain table at NS_CODE_START — hence this block. */
uint32_t tfm_hal_get_ns_MSP(void)
{
    return ((volatile uint32_t *)NS_HANDOFF_BLOCK)[0];
}

uint32_t tfm_hal_get_ns_entry_point(void)
{
    /* NS must use the checked PSA gateway for flash programming. Prevent
     * remapping a secure flash/storage region through the NS XIP window. */
    ((volatile uint32_t *)0x51102400)[0] &= ~(1u << 10); /* SPIC user mode */
    ((volatile uint32_t *)0x51102400)[2] &= ~((1u << 2) | (1u << 3)); /* MPC + RSIP */
    ((volatile uint32_t *)0x51102400)[3] = 1; /* Lock PPC until reset. */
    __DSB(); __ISB();
    uint32_t entry = ((volatile uint32_t *)NS_HANDOFF_BLOCK)[1];
    const char *s = "[TF-M] NS entry=";
    while (*s) ((void (*)(unsigned char))0xaba1)(*s++);
    for (int shift = 28; shift >= 0; shift -= 4)
        ((void (*)(unsigned char))0xaba1)("0123456789abcdef"[(entry >> shift) & 15]);
    ((void (*)(unsigned char))0xaba1)('\n');
    if (entry < NS_CODE_START || entry >= NS_CODE_LIMIT || !(entry & 1))
        while (1) {}
    return entry;
}

uint32_t tfm_hal_get_ns_VTOR(void)
{
    return ((volatile uint32_t *)NS_HANDOFF_BLOCK)[2];
}
