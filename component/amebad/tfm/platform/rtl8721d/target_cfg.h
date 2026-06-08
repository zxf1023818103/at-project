/*
 * RTL8721D (Ameba-D) KM4 — TF-M target configuration (TrustZone / IDAU / SAU).
 * Self-contained (an521 pattern): defines memory_region_limits + the platform
 * config API here; does NOT pull TF-M's common_target_cfg.h (which assumes a
 * CMSDK-style PPC interface this Ameba port doesn't use yet).
 */
#ifndef __TARGET_CFG_H__
#define __TARGET_CFG_H__

#include <stdint.h>

#define TFM_DRIVER_STDIO    Driver_USART0
#define NS_DRIVER_STDIO     Driver_USART0

struct memory_region_limits {
    uint32_t non_secure_code_start;
    uint32_t non_secure_partition_base;
    uint32_t non_secure_partition_limit;
    uint32_t veneer_base;
    uint32_t veneer_limit;
#ifdef BL2
    uint32_t secondary_partition_base;
    uint32_t secondary_partition_limit;
#endif
};

extern const struct memory_region_limits memory_regions;

/* Enable secure (and configured banked) fault handlers. */
void enable_fault_handlers(void);
/* AIRCR: PRIS + BFHFNMINS, per the Ameba-D BOOT_RAM_TZCfg recipe. */
void system_reset_cfg(void);
/* Configure SAU + the Ameba-D KM4 IDAU (0x5000C100) for the TF-M S/NS split. */
void sau_and_idau_cfg(void);
/* Route external interrupts to Non-Secure (NVIC ITNS). */
void nvic_interrupt_target_state_cfg(void);
/* Enable the secure interrupts TF-M owns. */
void nvic_interrupt_enable(void);

#endif /* __TARGET_CFG_H__ */
