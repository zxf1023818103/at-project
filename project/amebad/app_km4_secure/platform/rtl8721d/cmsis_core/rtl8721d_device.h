/*
 * RTL8721D (Ameba-D) KM4 — minimal CMSIS device header for TF-M.
 * IRQn_Type from component/amebad/fwlib/rtl8721d_vector.h; M33 feature config
 * matches the KM4 (Armv8-M Mainline + DSP + single-precision FPU + TrustZone).
 */
#ifndef __RTL8721D_DEVICE_H__
#define __RTL8721D_DEVICE_H__

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
    /* RTL8721D KM4 external interrupts */
    SYSTEM_ON_IRQ            = 0,
    WDG_IRQ                  = 1,
    RXI300_IRQ              = 2,
    UART_LOG_IRQ            = 3,
    GPIOA_IRQ              = 4,
    RTC_IRQ                = 5,
    I2C0_IRQ              = 6,
    SPI_FLASH_IRQ          = 7,
    GPIOB_IRQ              = 8,
    UARTLP_IRQ            = 9,
    KEYSCAN_IRQ            = 10,
    CTOUCH_IRQ            = 11,
    BOR2_IRQ              = 12,
    SGPIO_IRQ            = 13,
    IPC_IRQ              = 14,
    ADC_IRQ              = 15,
    QDECODER_IRQ          = 16,
    TIMER0_IRQ            = 17,
    TIMER1_IRQ            = 18,
    TIMER2_IRQ            = 19,
    TIMER3_IRQ            = 20,
    TIMER4_IRQ            = 21,
    TIMER5_IRQ            = 22,
    LCDC_IRQ            = 23,
    USB_OTG_IRQ          = 24,
    SDIO_DEVICE_IRQ        = 25,
    SDIO_HOST_IRQ          = 26,
    CRYPTO_IRQ            = 27,
    I2S0_PCM0_IRQ          = 28,
    PWR_DOWN_IRQ          = 29,
    ADC_COMP_IRQ          = 30,
    WL_DMA_IRQ            = 31,
    WL_PROTOCOL_IRQ        = 32,
    PSRAMC_IRQ            = 33,
    UART0_IRQ            = 34,
    UART1_IRQ            = 35,
    SPI0_IRQ            = 36,
    SPI1_IRQ            = 37,
    USI_IRQ              = 38,
    IR_IRQ              = 39,
    BT2WL_STS_IRQ          = 40,
    GDMA0_CHANNEL0_IRQ      = 41,
    GDMA0_CHANNEL1_IRQ      = 42,
    GDMA0_CHANNEL2_IRQ      = 43,
    GDMA0_CHANNEL3_IRQ      = 44,
    GDMA0_CHANNEL4_IRQ      = 45,
    GDMA0_CHANNEL5_IRQ      = 46,
    WL_PROTOCOL_IRQ_S      = 49,
    CRYPTO_IRQ_S          = 50,
    RXI300_IRQ_S          = 51,
    GDMA0_CHANNEL0_IRQ_S    = 52,
    GDMA0_CHANNEL1_IRQ_S    = 53,
    GDMA0_CHANNEL2_IRQ_S    = 54,
    GDMA0_CHANNEL3_IRQ_S    = 55,
    GDMA0_CHANNEL4_IRQ_S    = 56,
    GDMA0_CHANNEL5_IRQ_S    = 57,
} IRQn_Type;

#define RTL8721D_IRQ_MAX        (58)   /* 0..57 */

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

/* Per-device vector-table entry type (CMSIS provides only the template). */
typedef void (*VECTOR_TABLE_Type)(void);

#ifdef __cplusplus
}
#endif

#endif /* __RTL8721D_DEVICE_H__ */
