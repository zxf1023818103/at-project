# component/freertos_config — cross-platform FreeRTOS config + portable extensions.
#
# (1) freertos_config_common  (INTERFACE, all platforms)
#     The single cross-platform FreeRTOSConfig.h, macro-selected (CONFIG_PLATFORM_8721D
#     + KM0/KM4 via __ARM_ARCH_8M_BASE__/__ARM_ARCH_8M_MAIN__; BLxxx for Bouffalo).
#     Carried onto each platform's include path (amebad_sdk_includes / Bouffalo).
#
# (2) freertos_config           (STATIC, ALL platforms)
#     The portable FreeRTOS extensions that are not part of the upstream kernel:
#       freertos_backtrace_ext.c   crash/backtrace helpers (vTaskStackSize/...)
#       freertos_heap_bflb_mm.c    FreeRTOS heap surface over the shared bflb_mm
#       freertos_heap_none.c       empty FREERTOS_HEAP stub
#       freertos_hooks.c           idle / stack-overflow / malloc-failed hooks (printf)
#     This is ALSO the kernel's mandated `freertos_config` target (the upstream
#     FreeRTOS-Kernel links any target of that name for FreeRTOSConfig.h), so it
#     PUBLIC-exposes this dir (FreeRTOSConfig.h + freertos_backtrace_ext.h).
#
#     CROSS-PLATFORM: every platform points FREERTOS_HEAP at freertos_heap_none.c
#     and uses freertos_heap_bflb_mm.c's pvPortMalloc over the shared bflb_mm — so
#     this single target is the FreeRTOS heap + hooks for AmebaD AND Bouffalo. The
#     per-platform pieces are attached to this target from each platform's own
#     CMakeLists *after the kernel exists* (which is also where amebad_sdk_includes /
#     the Bouffalo RISC-V port defs live):
#       - AmebaD   : component/amebad/freertos_port links amebad_sdk_includes
#                    (FreeRTOS kernel headers + platform_autoconf.h).
#       - Bouffalo : component/bouffalo_sdk links the kernel include dirs and adds
#                    configMTIME_* / portasmHANDLE_INTERRUPT.
#     Keeping those attachments on the platform side is why this file names no
#     platform-specific target (so it builds identically everywhere).

add_library(freertos_config_common INTERFACE)
# SYSTEM: the kernel compiles with -Wpedantic -Werror; the Bouffalo configASSERT
# uses __FUNCTION__ (non-ISO). Keep it a system include so that does not error.
target_include_directories(freertos_config_common SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- (2) portable FreeRTOS extensions = the kernel's `freertos_config` target ----
add_library(freertos_config STATIC
    freertos_backtrace_ext.c
    freertos_heap_bflb_mm.c   # FreeRTOS heap surface over the shared bflb_mm (TLSF)
    freertos_heap_none.c      # empty FREERTOS_HEAP stub (kernel compiles no heap_N.c)
    freertos_hooks.c
)

# PUBLIC: this dir (FreeRTOSConfig.h for the kernel + freertos_backtrace_ext.h) and
# the shared config base. bflb_mm (mm.h) backs the heap surface. The FreeRTOS kernel
# headers (FreeRTOS.h/portmacro.h) and any platform autoconf.h pulled by
# FreeRTOSConfig.h are attached per-platform once the kernel target exists (see the
# header comment). On AmebaD KM0 (no libc/libgcc) the libc symbols TLSF pulls
# (strncpy, printf) are ROM-aliased in app_km0/rlx8721d_img2_romalias.ld; bflb_mm
# pulls libgcc for __clzsi2/__ffssi2.
target_include_directories(freertos_config SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(freertos_config PUBLIC freertos_config_common bflb_mm)

target_compile_options(freertos_config PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare)
