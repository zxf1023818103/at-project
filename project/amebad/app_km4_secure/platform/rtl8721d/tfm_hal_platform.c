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

enum tfm_hal_status_t tfm_hal_platform_init(void)
{
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

/* The bootloader writes a 3-word NS handoff block at NS_HANDOFF_BLOCK:
 * [0]=MSP_NS, [1]=NS entry (app_km4 RamStartFun), [2]=NS VTOR (app_km4 VectorNS).
 * The NS world is app_km4 (NSPE), a full AmebaD image2 whose reset vector is
 * resolved at runtime — not a plain table at NS_CODE_START — hence this block. */
uint32_t tfm_hal_get_ns_MSP(void)
{
    return ((volatile uint32_t *)NS_HANDOFF_BLOCK)[0];
}

uint32_t tfm_hal_get_ns_entry_point(void)
{
    return ((volatile uint32_t *)NS_HANDOFF_BLOCK)[1];
}

uint32_t tfm_hal_get_ns_VTOR(void)
{
    return ((volatile uint32_t *)NS_HANDOFF_BLOCK)[2];
}
