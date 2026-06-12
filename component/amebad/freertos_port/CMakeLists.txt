# component/amebad/freertos_port — the AmebaD FreeRTOS layer, in one folder:
#   (1) the upstream FreeRTOS-Kernel V10.6.2 (built from the
#       component/thirdparty/freertos-kernel submodule), per-core port + heap; and
#   (2) the AmebaD SDK FreeRTOS extensions that are NOT part of upstream (the
#       "glue"): backtrace, the heap_5 region table / bflb_mm heap surface, and the
#       application hooks the SDK kept in port.c.
#
# Target names are unchanged so every consumer keeps working:
#   FreeRTOS                     ALIAS of freertos_kernel (the upstream kernel)
#   freertos_config              INTERFACE the kernel mandates to find FreeRTOSConfig.h
#   amebad_freertos_glue_km{0,4} the per-core glue static lib (app_km{0,4} link groups)
#
# ============================================================================
# (1) Upstream FreeRTOS kernel
# ============================================================================
#
# This replaced the old fork-specific FreeRTOS v10.2.0 (component/thirdparty/
# freertos_amebad, full-distro layout compiled file-by-file). The upstream kernel
# ships its own CMakeLists with the FREERTOS_PORT / freertos_config / FREERTOS_HEAP
# contract, so AmebaD consumes it the standard way: add_subdirectory + an ARMv8-M
# GCC port chosen per core.
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
# FreeRTOSConfig.h: the single cross-platform header in component/freertos_config
# is carried by the shared freertos_config STATIC; AmebaD attaches amebad_sdk_includes
# to it below so platform_autoconf.h + the kernel headers resolve.

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_frt_port GCC_ARM_CM0)
    set(_glue_tgt amebad_freertos_glue_km0)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_frt_port GCC_ARM_CM33_NTZ_NONSECURE)
    set(_glue_tgt amebad_freertos_glue_km4)
else()
    return()
endif()

set(_kernel_src "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos-kernel")

# Consolidated header INTERFACE for amebad_sdk_includes: the FreeRTOS glue headers
# (this dir: freertos_service.h / freertos_heap5_config.h / backtrace_ext.h) + the
# upstream kernel's FreeRTOS.h (include/) + portmacro.h (the per-core port dir).
# Linked — not raw-pathed — by amebad_sdk_includes. An INTERFACE so it carries no
# objects and no cycle (linking the freertos_kernel_port STATIC would cycle back via
# freertos_config). The port PATH differs from the FREERTOS_PORT name above.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_kernel_port_path "GCC/ARM_CM0")
else()
    set(_kernel_port_path "GCC/ARM_CM33_NTZ/non_secure")
endif()
add_library(amebad_freertos_headers INTERFACE)
target_include_directories(amebad_freertos_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_kernel_src}/include"
    "${_kernel_src}/portable/${_kernel_port_path}"
)

if(NOT EXISTS "${_kernel_src}/tasks.c")
    message(FATAL_ERROR
        "FreeRTOS: '${_kernel_src}/tasks.c' missing — run "
        "'git submodule update --init component/thirdparty/freertos-kernel'")
endif()

# --- freertos_config: the upstream kernel CMake mandates a target of this name to
# locate FreeRTOSConfig.h. It is the cross-platform STATIC lib defined in
# component/freertos_config (the portable FreeRTOS extensions + bflb_mm heap surface;
# PUBLIC-exposes FreeRTOSConfig.h), added at the top level before this subtree, so it
# already exists here. Guard only as a safety net.
if(NOT TARGET freertos_config)
    message(FATAL_ERROR
        "freertos_config target missing — component/freertos_config must be added "
        "(top-level CMakeLists) before component/amebad")
endif()

# The shared component/freertos_config names no AmebaD target, so attach the AmebaD
# SDK include bundle here: amebad_sdk_includes carries the FreeRTOS kernel headers
# (FreeRTOS.h/portmacro.h) + platform_autoconf.h (pulled by FreeRTOSConfig.h's
# CONFIG_PLATFORM_8721D branch) needed to compile the extension sources. PUBLIC so
# the kernel (which links freertos_config) sees the same FreeRTOSConfig.h deps.
target_link_libraries(freertos_config PUBLIC amebad_sdk_includes)

# Port + heap selection for the upstream kernel CMakeLists. CACHE ... FORCE so we
# win over the kernel's own default (set(FREERTOS_HEAP "4" CACHE ...)). km0/km4
# configure into separate build dirs, so the per-core port value never clashes.
set(FREERTOS_PORT "${_frt_port}" CACHE STRING "FreeRTOS port name" FORCE)
# Heap: both cores use the shared bflb_mm (TLSF). Point FREERTOS_HEAP at an EMPTY
# source so the kernel compiles no heap_N.c; the heap surface (pvPortMalloc/
# vPortFree/vPortDefineHeapRegions/...) comes from freertos_config's
# freertos_heap_bflb_mm.c over bflb_mm. (The kernel CMake accepts an absolute path
# as a custom heap source.) On KM0 (LP, no libc/libgcc) the libc/libgcc symbols
# TLSF pulls (strncpy, ...) are aliased to the ROM in app_km0/rlx8721d_img2_romalias.ld.
# The portable FreeRTOS extension sources (heap surfaces, hooks, backtrace) live in
# the shared component/freertos_config; the amebad-memory-map region table
# (heap5_config.c) stays here. The per-core glue lib below compiles both.
set(_cfg "${CMAKE_SOURCE_DIR}/component/freertos_config")

set(FREERTOS_HEAP "${_cfg}/freertos_heap_none.c"
    CACHE STRING "FreeRTOS heap model" FORCE)

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

# ============================================================================
# (2) AmebaD memory-map glue (amebad_freertos_glue_km{0,4})
# ============================================================================
#
# The portable FreeRTOS extensions (backtrace, hooks, the bflb_mm heap surface, the
# empty heap stub) are compiled by the freertos_config STATIC lib in
# component/freertos_config. Only the AmebaD memory-map heap_5 region table stays
# here (it fills vPortDefineHeapRegions from the chip linker symbols). Target name
# amebad_freertos_glue_km{0,4} kept so the app link groups don't change; it links
# freertos_config for the heap surface / hooks.

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/freertos_heap5_config.c")
    message(FATAL_ERROR "amebad freertos_port: missing 'freertos_heap5_config.c'")
endif()

add_library(${_glue_tgt} STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/freertos_heap5_config.c"  # amebad memory map: per-core region table + os_heap_init -> vPortDefineHeapRegions
)

# FreeRTOS headers + the portable extensions / heap surface (freertos_config).
# PUBLIC so a consumer linking the glue also gets the FreeRTOS API.
target_link_libraries(${_glue_tgt} PUBLIC FreeRTOS freertos_config)

target_compile_options(${_glue_tgt} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
