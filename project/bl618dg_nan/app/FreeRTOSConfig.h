#pragma once
#include "../../bl616_usb_ncm/app/FreeRTOSConfig.h"
#define configCOMMAND_INT_MAX_OUTPUT_SIZE 512
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
/* Diagnostic windows are a few seconds, well below the 32-bit us wrap period. */
#undef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS 1
uint32_t bl618dg_nan_runtime_us(void);
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() do {} while (0)
#define portGET_RUN_TIME_COUNTER_VALUE() bl618dg_nan_runtime_us()
#endif
