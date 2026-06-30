/*
 * RTL8721D KM4 — TF-M peripheral definitions (minimal bring-up).
 * No secure-peripheral-owning partitions yet, so only the std UART handle the
 * SPM/log expect. Add real peripheral platform_data_t instances when partitions
 * declare MMIO (isolation L2/L3).
 */
#ifndef __TFM_PERIPHERALS_DEF_H__
#define __TFM_PERIPHERALS_DEF_H__

#include "tfm_plat_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct platform_data_t;

extern struct platform_data_t tfm_peripheral_std_uart;
#define TFM_PERIPHERAL_STD_UART     (&tfm_peripheral_std_uart)

/* Number of MPU/peripheral regions a partition may own (L1: none enforced). */
#define TFM_PERIPHERAL_FF_TEST_UART_REGION      (0)

#ifdef __cplusplus
}
#endif

#endif /* __TFM_PERIPHERALS_DEF_H__ */
