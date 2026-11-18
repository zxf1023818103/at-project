#pragma once
#ifdef CONFIG_USB_NCM_ENUM_DEBUG
#define CONFIG_USB_DBG_LEVEL 3
#endif
#include "cherryusb_config_template.h"
#ifndef CONFIG_USB_NCM_FULL_SPEED_ONLY
#define CONFIG_USB_HS
#endif
