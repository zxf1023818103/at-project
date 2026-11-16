#pragma once
#define CONFIG_USB_HS
#include "cherryusb_config_template.h"
#ifdef CONFIG_NAN_NATIVE_WHC_USB
/* Original WHC interface has four endpoints; CherryUSB parser reserves a slot. */
#undef CONFIG_USBHOST_MAX_ENDPOINTS
#define CONFIG_USBHOST_MAX_ENDPOINTS 8
#endif
