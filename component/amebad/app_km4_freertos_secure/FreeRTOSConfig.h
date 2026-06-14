/*
 * FreeRTOSConfig.h - MINIMAL secure-image config for the AmebaD KM4 standalone
 * FreeRTOS TrustZone secure world (component/amebad/app_km4_freertos_secure).
 *
 * The four FreeRTOS-Kernel ARM_CM33 secure-port sources (secure_context.c,
 * secure_context_port.c, secure_heap.c, secure_init.c) include "FreeRTOSConfig.h"
 * (via secure_context.h) ONLY for a handful of secureconfig / configENABLE knobs.
 * They touch the SCB/FPU via hard-coded addresses, not the CMSIS device header,
 * and never pull the kernel proper. So the secure image deliberately uses THIS
 * tiny self-contained config (placed first on its include path) instead of the
 * shared component/freertos-kernel_config/FreeRTOSConfig.h, which would drag in
 * platform_autoconf.h and the whole NS SDK config surface.
 *
 * The NS side keeps using the shared cross-platform FreeRTOSConfig.h (with
 * CONFIG_TRUSTZONE_EN=1 -> configENABLE_TRUSTZONE=1) - see the plan / app_km4.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* This image IS the secure world: TrustZone in use, FPU on (matches the NS side's
 * fpv5-sp-d16 hard-float so the secure FP stack frame layout agrees). */
#define configENABLE_TRUSTZONE        1
#define configENABLE_FPU              1
#define configENABLE_MVE              0
#define configENABLE_MPU              0

/* Per-task secure stacks live in the secure heap (secure_heap.c, a self-contained
 * static array in S_DATA). 8 contexts is the upstream default; this test uses 3
 * (idle + user + preemptor). The 16 KB heap is far larger than
 * 3 * configMINIMAL_SECURE_STACK_SIZE. */
#define secureconfigMAX_SECURE_CONTEXTS    8
#define secureconfigTOTAL_HEAP_SIZE        ( ( size_t ) ( 16 * 1024 ) )
#define secureconfigUSE_MALLOC_FAILED_HOOK 0
#define configAPPLICATION_ALLOCATED_HEAP   0

#endif /* FREERTOS_CONFIG_H */
