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

__attribute__((weak))
void cm_backtrace_init(const char *firmware_name,
                       const char *hardware_ver,
                       const char *software_ver)
{
    (void)firmware_name;
    (void)hardware_ver;
    (void)software_ver;
}

__attribute__((weak))
void cm_backtrace_fault(uint32_t fault_handler_sp, uint32_t fault_handler_lr)
{
    (void)fault_handler_sp;
    (void)fault_handler_lr;
}
