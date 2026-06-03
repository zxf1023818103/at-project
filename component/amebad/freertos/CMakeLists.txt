# FreeRTOS — upstream FreeRTOS-Kernel V10.6.2, built straight from the
# component/thirdparty/freertos-kernel submodule (the same kernel Bouffalo uses;
# see component/bouffalo_sdk/CMakeLists.txt). The produced kernel target
# `freertos_kernel` is ALIASed to `FreeRTOS` so every existing consumer keeps
# linking target_link_libraries(<tgt> FreeRTOS) unchanged.
#
# This replaces the old fork-specific FreeRTOS v10.2.0 (component/thirdparty/
# freertos_amebad, full-distro layout compiled file-by-file). The upstream
# kernel ships its own CMakeLists with the FREERTOS_PORT / freertos_config /
# FREERTOS_HEAP contract, so AmebaD now consumes it the standard way:
# add_subdirectory + an ARMv8-M GCC port chosen per core.
#
#   KM0 (LP) = Cortex-M23 -> GCC_ARM_CM0   (ARMv6-M port, see below)
#   KM4 (HP) image2 = Cortex-M33, non-secure, no TrustZone -> GCC_ARM_CM33_NTZ_NONSECURE
#
# The SDK's RTL8721D_LP/HP ports were stock-ARM derivatives with no functional
# divergence (tickless idle is config-driven via external SDK sleep hooks, not a
# port change), so the upstream ARM ports are functionally equivalent — BUT the
# port *family* matters and must match what the SDK ran:
#   - KM4 SDK port = ARMv8-M CM33 (FPU, resets MSP from VTOR[0]); KM4 image2's
#     vector table has a valid initial-SP word, so the native CM33_NTZ port works.
#   - KM0 SDK port = ARMv6-M CM0 ("The MSP stack is not reset as ... there is no
#     vector table offset register"). The native ARMv8-M CM23_NTZ port resets MSP
#     from VTOR[0] in vStartFirstTask, but KM0 image2's VTOR[0] is NOT a valid
#     stack top -> the start-scheduler SVC faults (HardFault, xPSR ISR=11). So KM0
#     uses the upstream ARMv6-M CM0 port (runs fine on the M23, exactly as the SDK
#     LP port did) which never touches MSP.
#
# heap_5 (FREERTOS_HEAP 5): the heap region table is supplied at runtime by the
# glue's freertos_heap5_config.c (vPortDefineHeapRegions over the AmebaD memory
# map) — see component/amebad/freertos_glue/ + freertos_glue_km{0,4}.
#
# FreeRTOSConfig.h: our config/FreeRTOSConfig.h override (#include_next to the
# SDK's inc_lp/inc_hp version) is carried by the freertos_config INTERFACE below.

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_frt_port GCC_ARM_CM0)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_frt_port GCC_ARM_CM33_NTZ_NONSECURE)
else()
    return()
endif()

set(_kernel_src "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos-kernel")
if(NOT EXISTS "${_kernel_src}/tasks.c")
    message(FATAL_ERROR
        "FreeRTOS: '${_kernel_src}/tasks.c' missing — run "
        "'git submodule update --init component/thirdparty/freertos-kernel'")
endif()

# --- freertos_config: the upstream kernel CMake mandates this INTERFACE target
# to locate FreeRTOSConfig.h. amebad_sdk_includes already carries the full
# FreeRTOSConfig.h search order — our config/ override FIRST, then the SDK base
# config (inc_lp/inc_hp) + platform_autoconf.h + the kernel headers — so this is
# just a thin alias that satisfies the kernel's "freertos_config must exist"
# contract. (config/ lives on amebad_sdk_includes, not here, so every consumer —
# not only the kernel — sees the same config, e.g. tickless-idle off.)
if(NOT TARGET freertos_config)
    add_library(freertos_config INTERFACE)
    target_link_libraries(freertos_config INTERFACE amebad_sdk_includes)
endif()

# Port + heap selection for the upstream kernel CMakeLists. CACHE ... FORCE so we
# win over the kernel's own default (set(FREERTOS_HEAP "4" CACHE ...)). km0/km4
# configure into separate build dirs, so the per-core port value never clashes.
set(FREERTOS_PORT "${_frt_port}" CACHE STRING "FreeRTOS port name" FORCE)
set(FREERTOS_HEAP "5"            CACHE STRING "FreeRTOS heap model" FORCE)

if(NOT TARGET freertos_kernel)
    add_subdirectory("${_kernel_src}"
                     "${CMAKE_BINARY_DIR}/component/thirdparty/freertos-kernel")
endif()

# Keep the `FreeRTOS` name every consumer already links.
if(NOT TARGET FreeRTOS)
    add_library(FreeRTOS ALIAS freertos_kernel)
endif()

# The upstream kernel CMakeLists compiles with -Wall -Wextra -Wpedantic -Werror
# (directory-scoped). The AmebaD FreeRTOSConfig.h (empty configASSERT, etc.) can
# trip set-but-unused / pedantic warnings inside the otherwise-clean kernel
# sources. Demote -Werror on the kernel targets so config-driven warnings don't
# fail the build (warnings stay visible). Same spirit as the warning-silencing
# the thirdparty zlib / littlefs / cjson targets get.
target_compile_options(freertos_kernel PRIVATE -Wno-error)
if(TARGET freertos_kernel_port)
    target_compile_options(freertos_kernel_port PRIVATE -Wno-error)
endif()
