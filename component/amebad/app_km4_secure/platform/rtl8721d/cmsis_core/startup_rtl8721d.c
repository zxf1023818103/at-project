/*
 * RTL8721D (Ameba-D) KM4 — TF-M secure image startup (GCC).
 * Modeled on TF-M's startup_an521.c; vector table per rtl8721d_device.h IRQn.
 * External IRQ slots are weak->Default_Handler; TF-M/SPM install real handlers
 * at runtime via the RAM vector table (SCB->VTOR set in SystemInit).
 */
#include "tfm_hal_device_header.h"
#include "system_core_init.h"

extern uint32_t __INITIAL_SP;
extern uint32_t __STACK_LIMIT;
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
extern uint64_t __STACK_SEAL;
#endif
extern __NO_RETURN void __PROGRAM_START(void);

__NO_RETURN void Reset_Handler(void);

#define DEFAULT_IRQ_HANDLER(name) \
    void __WEAK name(void); \
    void name(void) { while (1) {} }

/* Core exceptions */
DEFAULT_IRQ_HANDLER(NMI_Handler)
DEFAULT_IRQ_HANDLER(HardFault_Handler)
DEFAULT_IRQ_HANDLER(MemManage_Handler)
DEFAULT_IRQ_HANDLER(BusFault_Handler)
DEFAULT_IRQ_HANDLER(UsageFault_Handler)
DEFAULT_IRQ_HANDLER(SecureFault_Handler)
DEFAULT_IRQ_HANDLER(SVC_Handler)
DEFAULT_IRQ_HANDLER(DebugMon_Handler)
DEFAULT_IRQ_HANDLER(PendSV_Handler)
DEFAULT_IRQ_HANDLER(SysTick_Handler)

/* External IRQ default (weak; overridden at runtime via NVIC/VTOR). */
void __WEAK Default_Handler(void);
void Default_Handler(void) { while (1) {} }

#define IRQ(n) void __WEAK IRQ##n##_Handler(void) __attribute__((alias("Default_Handler")));
/* 0..57 (gaps 47..48 included as reserved slots) */
IRQ(0)  IRQ(1)  IRQ(2)  IRQ(3)  IRQ(4)  IRQ(5)  IRQ(6)  IRQ(7)
IRQ(8)  IRQ(9)  IRQ(10) IRQ(11) IRQ(12) IRQ(13) IRQ(14) IRQ(15)
IRQ(16) IRQ(17) IRQ(18) IRQ(19) IRQ(20) IRQ(21) IRQ(22) IRQ(23)
IRQ(24) IRQ(25) IRQ(26) IRQ(27) IRQ(28) IRQ(29) IRQ(30) IRQ(31)
IRQ(32) IRQ(33) IRQ(34) IRQ(35) IRQ(36) IRQ(37) IRQ(38) IRQ(39)
IRQ(40) IRQ(41) IRQ(42) IRQ(43) IRQ(44) IRQ(45) IRQ(46) IRQ(47)
IRQ(48) IRQ(49) IRQ(50) IRQ(51) IRQ(52) IRQ(53) IRQ(54) IRQ(55)
IRQ(56) IRQ(57)
#undef IRQ

#define IRQH(n) IRQ##n##_Handler
extern const VECTOR_TABLE_Type __VECTOR_TABLE[];
const VECTOR_TABLE_Type __VECTOR_TABLE[] __VECTOR_TABLE_ATTRIBUTE = {
    (VECTOR_TABLE_Type)(&__INITIAL_SP),
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    SecureFault_Handler,
    0, 0, 0,                /* reserved 7..9 (8 used by SecureFault above) */
    SVC_Handler,
    DebugMon_Handler,
    0,                      /* reserved */
    PendSV_Handler,
    SysTick_Handler,
    /* External IRQ 0.. */
    IRQH(0),  IRQH(1),  IRQH(2),  IRQH(3),  IRQH(4),  IRQH(5),  IRQH(6),  IRQH(7),
    IRQH(8),  IRQH(9),  IRQH(10), IRQH(11), IRQH(12), IRQH(13), IRQH(14), IRQH(15),
    IRQH(16), IRQH(17), IRQH(18), IRQH(19), IRQH(20), IRQH(21), IRQH(22), IRQH(23),
    IRQH(24), IRQH(25), IRQH(26), IRQH(27), IRQH(28), IRQH(29), IRQH(30), IRQH(31),
    IRQH(32), IRQH(33), IRQH(34), IRQH(35), IRQH(36), IRQH(37), IRQH(38), IRQH(39),
    IRQH(40), IRQH(41), IRQH(42), IRQH(43), IRQH(44), IRQH(45), IRQH(46), IRQH(47),
    IRQH(48), IRQH(49), IRQH(50), IRQH(51), IRQH(52), IRQH(53), IRQH(54), IRQH(55),
    IRQH(56), IRQH(57),
};

void Reset_Handler(void)
{
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    __disable_irq();
#endif
    __set_PSP((uint32_t)(&__INITIAL_SP));
    __set_MSPLIM((uint32_t)(&__STACK_LIMIT));
    __set_PSPLIM((uint32_t)(&__STACK_LIMIT));
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    __TZ_set_STACKSEAL_S((uint32_t *)(&__STACK_SEAL));
#endif
    SystemInit();
    __PROGRAM_START();
}
