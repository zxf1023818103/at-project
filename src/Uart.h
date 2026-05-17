#pragma once

// Ameba builds get their CONFIG_AMEBADPLUS / CONFIG_PLATFORM_8721D etc.
// from platform_autoconf.h. Pull it in directly so the platform check
// below is correct regardless of who included Uart.h first.
#if defined __has_include
#  if __has_include("platform_autoconf.h")
#    include "platform_autoconf.h"
#  endif
#endif

#if defined BL602 || defined BL702 || defined BL606P || defined BL808 \
 || defined BL616 || defined BL616CL
#  define AT_UART_BACKEND_BFLB  1
#elif defined CONFIG_PLATFORM_8721D || defined CONFIG_PLATFORM_8710C \
   || defined CONFIG_PLATFORM_AMEBALITE || defined CONFIG_PLATFORM_AMEBASMART \
   || defined CONFIG_AMEBADPLUS || defined CONFIG_AMEBAGREEN2
#  define AT_UART_BACKEND_AMEBA 1
#endif

#ifdef __cplusplus
#  if defined AT_UART_BACKEND_BFLB
struct bflb_device_s;
namespace at { using UartDevice = ::bflb_device_s; }
#  elif defined AT_UART_BACKEND_AMEBA
// Opaque handle from UartAmebaGlue.h; wraps SDK serial_t.
struct at_uart_handle_s;
namespace at { using UartDevice = ::at_uart_handle_s; }
#  endif
#endif  // __cplusplus
