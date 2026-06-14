/*
 * core_cm33.h — shim for the standalone FreeRTOS secure image.
 *
 * The reused TF-M device header (app_km4_secure/.../cmsis_core/rtl8721d_device.h)
 * does `#include "core_cm33.h"`. The AmebaD SDK (platform/ameba-rtos-d) ships the
 * Cortex-M33 core as the generic Armv8-M Mainline header core_armv8mml.h (vanilla
 * CMSIS — unlike the sibling ameba-rtos copy, it does NOT pull platform_autoconf.h),
 * so route core_cm33.h -> core_armv8mml.h. The M33 feature macros
 * (__SAUREGION_PRESENT / __FPU_PRESENT / __DSP_PRESENT / __NVIC_PRIO_BITS / ...)
 * are defined by rtl8721d_device.h *before* this include, so the SAU/FPU register
 * blocks are present.
 */
#ifndef __CORE_CM33_H_SHIM__
#define __CORE_CM33_H_SHIM__

#include "core_armv8mml.h"

#endif /* __CORE_CM33_H_SHIM__ */
