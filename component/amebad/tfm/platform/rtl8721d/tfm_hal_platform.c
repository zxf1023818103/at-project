/*
 * RTL8721D KM4 — TF-M platform HAL (bring-up).
 * Brings up the secure-side attribution (IDAU/SAU/NVIC) via target_cfg and a
 * console for TF-M logging. Clocks are already up (Ameba boot chain).
 */
#include "rtl8721d_device.h"
#include "tfm_hal_platform.h"
#include "target_cfg.h"
#include "region_defs.h"
#include "uart_stdout.h"

/* Memory region limits (BL2 OFF -> no header offset); from region_defs.h. */
const struct memory_region_limits memory_regions = {
    .non_secure_code_start      = NS_CODE_START,
    .non_secure_partition_base  = NS_PARTITION_START,
    .non_secure_partition_limit = NS_PARTITION_START + NS_PARTITION_SIZE - 1,
    .veneer_base                = CMSE_VENEER_REGION_START,
    .veneer_limit               = CMSE_VENEER_REGION_START + CMSE_VENEER_REGION_SIZE - 1,
};

/* Bring-up trace via the AmebaD secure-ROM LOGUART (same as startup). */
static inline void tfm_dbg_puts(const char *s)
{
    void (*pc)(unsigned char) = (void (*)(unsigned char))0x10102af5u;
    while (*s) pc((unsigned char)*s++);
}

enum tfm_hal_status_t tfm_hal_platform_init(void)
{
    tfm_dbg_puts("H1\n");
    __enable_irq();

    /* Secure attribution + banked fault/NVIC config (IDAU/SAU/ITNS/AIRCR). */
    enable_fault_handlers();
    tfm_dbg_puts("H2\n");
    system_reset_cfg();
    tfm_dbg_puts("H3\n");
    sau_and_idau_cfg();
    tfm_dbg_puts("H4\n");
    nvic_interrupt_target_state_cfg();
    nvic_interrupt_enable();
    tfm_dbg_puts("H5\n");

    /* TF-M log console (routes to the Ameba LOGUART via Driver_USART0). */
    stdio_init();
    tfm_dbg_puts("H6\n");

    return TFM_HAL_SUCCESS;
}

uint32_t tfm_hal_get_ns_VTOR(void)
{
    return memory_regions.non_secure_code_start;
}

uint32_t tfm_hal_get_ns_MSP(void)
{
    return *((uint32_t *)memory_regions.non_secure_code_start);
}

uint32_t tfm_hal_get_ns_entry_point(void)
{
    return *((uint32_t *)(memory_regions.non_secure_code_start + 4));
}
