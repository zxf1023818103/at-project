/*
 * AMEBADPLUS (AmebaDplus) KM4 — minimal CMSIS device header for TF-M.
 * IRQn_Type from component/amebad/fwlib/amebadplus_vector.h; M33 feature config
 * matches the KM4 (Armv8-M Mainline + DSP + single-precision FPU + TrustZone).
 */
#ifndef __AMEBADPLUS_DEVICE_H__
#define __AMEBADPLUS_DEVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IRQn {
    /* Cortex-M33 core exceptions */
    NonMaskableInt_IRQn      = -14,
    HardFault_IRQn           = -13,
    MemoryManagement_IRQn    = -12,
    BusFault_IRQn            = -11,
    UsageFault_IRQn          = -10,
    SecureFault_IRQn         = -9,
    SVCall_IRQn              = -5,
    DebugMonitor_IRQn        = -4,
    PendSV_IRQn              = -2,
    SysTick_IRQn             = -1,
    DEVICE_IRQ0 = 0, DEVICE_IRQ_LAST = 95,
} IRQn_Type;

#define AMEBADPLUS_IRQ_MAX        (96)

/* ---- Cortex-M33 processor and core peripheral config ---- */
#define __CM33_REV              0x0000U
#define __SAUREGION_PRESENT     1U      /* SAU regions present              */
#define __MPU_PRESENT           1U
#define __FPU_PRESENT           1U
#define __FPU_DP                0U      /* single-precision (fpv5-sp-d16)   */
#define __DSP_PRESENT           1U
#define __NVIC_PRIO_BITS        3U
#define __Vendor_SysTickConfig  0U
#define __VTOR_PRESENT          1U

#include "core_cm33.h"
/* Armv8.1-M TRD (the rest of the implemented register map matches M33). */
#ifndef SCB_CCR_TRD_Msk
#define SCB_CCR_TRD_Msk (1UL << 20U)
#endif

/* Per-device vector-table entry type (CMSIS provides only the template). */
typedef void (*VECTOR_TABLE_Type)(void);

#ifdef __cplusplus
}
#endif

#endif /* __AMEBADPLUS_DEVICE_H__ */
