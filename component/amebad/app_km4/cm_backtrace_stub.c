/*
 * cm_backtrace weak stubs for the KM4 image2 bootstrap.
 *
 * rtl8721dhp_app_start.c (ram_hp) calls cm_backtrace_init/_fault, but the
 * CmBacktrace library itself is NOT shipped in this ameba-rtos-d checkout
 * (only component/common/example/cm_backtrace/ — the example, not the impl).
 *
 * These are no-op weak definitions so the image2 links; a real CmBacktrace
 * (when added) overrides them. Backtrace-on-fault is a debug aid, not required
 * for boot. Signatures match the externs in rtl8721dhp_app_start.c.
 */
#include <stdint.h>

extern int DiagPrintf(const char *fmt, ...);

__attribute__((weak))
void cm_backtrace_init(const char *firmware_name,
                       const char *hardware_ver,
                       const char *software_ver)
{
    (void)firmware_name;
    (void)hardware_ver;
    (void)software_ver;
}

/* Minimal fault dump: the handler passes the faulting context's stack pointer,
 * which points at the Cortex-M exception frame [R0,R1,R2,R3,R12,LR,PC,xPSR].
 * PC (sp[6]) is the faulting instruction; CFSR/HFSR/BFAR pinpoint the cause. */
__attribute__((weak))
void cm_backtrace_fault(uint32_t fault_handler_sp, uint32_t fault_handler_lr)
{
    volatile uint32_t *sp = (volatile uint32_t *)fault_handler_sp;
    DiagPrintf("[FAULT] sp=%x lr=%x\r\n", fault_handler_sp, fault_handler_lr);
    DiagPrintf("[FAULT] R0=%x R1=%x R2=%x R3=%x R12=%x\r\n",
               sp[0], sp[1], sp[2], sp[3], sp[4]);
    DiagPrintf("[FAULT] stkLR=%x PC=%x xPSR=%x\r\n", sp[5], sp[6], sp[7]);
    DiagPrintf("[FAULT] CFSR=%x HFSR=%x BFAR=%x MMFAR=%x\r\n",
               *(volatile uint32_t *)0xE000ED28, *(volatile uint32_t *)0xE000ED2C,
               *(volatile uint32_t *)0xE000ED38, *(volatile uint32_t *)0xE000ED34);
}
