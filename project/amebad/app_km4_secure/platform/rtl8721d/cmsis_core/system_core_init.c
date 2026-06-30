/*
 * RTL8721D KM4 — minimal SystemInit for the TF-M secure image.
 * The Ameba-D boot chain (ROM + KM4 bootloader) already brings up clocks/PLL,
 * cache and PSRAM before entering image2/image3, so SystemInit only fixes up the
 * secure vector table base and FPU; clock config is left to the platform.
 */
#include "tfm_hal_device_header.h"
#include "system_core_init.h"
#include "region_defs.h"

/* KM4 default high-perf clock (placeholder; not used for baud since Ameba boot
 * already configured the LOGUART). Update via SystemCoreClockUpdate() if needed. */
uint32_t SystemCoreClock = 200000000U;

extern uint32_t __vectors_start__;   /* from the TF-M common scatter file */

void SystemCoreClockUpdate(void)
{
    /* Clocks owned by the Ameba boot chain; nothing to recompute here yet. */
}

void SystemInit(void)
{
#if defined(__VTOR_PRESENT) && (__VTOR_PRESENT == 1U)
    SCB->VTOR = (uint32_t)(&__vectors_start__);
#endif

#if defined(__FPU_USED) && (__FPU_USED == 1U)
    /* Enable CP10/CP11 (FPU) full access, secure + non-secure. */
    SCB->CPACR |= ((3U << 20U) | (3U << 22U));
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    SCB_NS->CPACR |= ((3U << 20U) | (3U << 22U));
    SCB->NSACR    |= (3U << 10U);   /* NS access to CP10/CP11 */
#endif
    __DSB();
    __ISB();
#endif

    SystemCoreClockUpdate();
}
