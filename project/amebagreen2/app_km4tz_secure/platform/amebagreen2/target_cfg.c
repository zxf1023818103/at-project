/* Boot configures the Green2 SAU and MPC before releasing KM4NS. Do not unlock or
 * broaden those regions here; SPE consumes the same hardware attribution. */
#include "target_cfg.h"
#include "amebagreen2_device.h"
void enable_fault_handlers(void)
{
    SCB->SHCSR |= SCB_SHCSR_SECUREFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
                  SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_MEMFAULTENA_Msk;
}
void system_reset_cfg(void) {}
void sau_and_idau_cfg(void) {}
void nvic_interrupt_target_state_cfg(void)
{
    for (unsigned i = 0; i < 3; ++i) NVIC->ITNS[i] = 0xffffffff;
}
void nvic_interrupt_enable(void) {}
