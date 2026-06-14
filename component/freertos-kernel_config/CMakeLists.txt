# component/freertos-kernel_config — home for the FreeRTOS-Kernel submodule
# (component/freertos-kernel_config/freertos-kernel, V10.6.2) PLUS the cross-platform
# FreeRTOS config + portable extensions (formerly component/freertos_config).
#
# The kernel submodule itself is consumer-coupled — it needs FREERTOS_PORT / heap /
# the freertos_config target fed before add_subdirectory — so it is add_subdirectory'd
# directly by each platform (component/amebad/freertos_port and
# component/bouffalo_sdk), pointing at this dir's freertos-kernel. This CMakeLists
# builds only the config/extension targets below (added once from the repo root):
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
#     CROSS-PLATFORM: every platform points FREERTOS_HEAP at freertos_heap_none.c and
#     uses freertos_heap_bflb_mm.c's pvPortMalloc over the shared bflb_mm. The
#     per-platform pieces are attached to this target from each platform's own
#     CMakeLists *after the kernel exists* (where amebad_sdk_includes / the Bouffalo
#     RISC-V port defs live), which is why this file names no platform-specific target.

add_library(freertos_config_common INTERFACE)
# SYSTEM: the kernel compiles with -Wpedantic -Werror; the Bouffalo configASSERT uses
# __FUNCTION__ (non-ISO). Keep it a system include so that does not error.
target_include_directories(freertos_config_common SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# ---- (2) portable FreeRTOS extensions = the kernel's `freertos_config` target ----
add_library(freertos_config STATIC
    freertos_backtrace_ext.c
    freertos_heap_bflb_mm.c   # FreeRTOS heap surface over the shared bflb_mm (TLSF)
    freertos_heap_none.c      # empty FREERTOS_HEAP stub (kernel compiles no heap_N.c)
    freertos_hooks.c
)

# PUBLIC: this dir (FreeRTOSConfig.h for the kernel + freertos_backtrace_ext.h) and the
# shared config base. bflb_mm (mm.h) backs the heap surface. The FreeRTOS kernel headers
# (FreeRTOS.h/portmacro.h) and any platform autoconf.h pulled by FreeRTOSConfig.h are
# attached per-platform once the kernel target exists (see the header comment). On
# AmebaD KM0 (no libc/libgcc) the libc symbols TLSF pulls (strncpy, printf) are
# ROM-aliased in app_km0/rlx8721d_img2_romalias.ld; bflb_mm pulls libgcc for
# __clzsi2/__ffssi2.
target_include_directories(freertos_config SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(freertos_config PUBLIC freertos_config_common bflb_mm)

target_compile_options(freertos_config PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare)

# ---- (3) upstream FreeRTOS kernel (V10.6.2): built here via the submodule's own CMake ----
# The kernel needs a port chosen per platform/core. Derive it from the chip vars (known at
# configure start; AmebaD km0/km4 configure into separate build trees, so the per-core value
# never clashes), set FREERTOS_PORT/HEAP, then add_subdirectory the submodule — the standard
# upstream contract. Platform specifics are still attached to freertos_config from each
# platform's own CMakeLists *after* this (amebad_sdk_includes -> platform_autoconf.h on
# AmebaD; the mtime defs on Bouffalo); target usage requirements resolve at generate, so the
# attach order doesn't matter.
#   KM0 (LP) = Cortex-M23 -> GCC_ARM_CM0 (ARMv6-M; the ARMv8-M CM23 port faults at scheduler
#              start on KM0's image2 — see component/amebad/freertos_port for the why)
#   KM4 (HP) image2 = Cortex-M33, non-secure -> GCC_ARM_CM33_NTZ_NONSECURE
#   Bouffalo (RISC-V)                        -> GCC_RISC_V
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_frt_port GCC_ARM_CM0)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    # KM4 image2 is non-secure. Default = the No-TrustZone port. When the
    # standalone FreeRTOS secure world is built (AMEBAD_FREERTOS_SECURE, make
    # SECURE=ON) the NS kernel switches to the TZ-aware port so it allocates +
    # saves/restores a per-task secure context against the secure image's
    # SecureContext_* SG veneers. (CONFIG_TRUSTZONE_EN=1 — set globally for the
    # km4 tree by the root CMakeLists when the flag is on — turns on
    # configENABLE_TRUSTZONE in FreeRTOSConfig.h.)
    if(AMEBAD_FREERTOS_SECURE)
        set(_frt_port GCC_ARM_CM33_NONSECURE)
    else()
        set(_frt_port GCC_ARM_CM33_NTZ_NONSECURE)
    endif()
elseif(BOUFFALO_CHIP)
    set(_frt_port GCC_RISC_V)
endif()

if(_frt_port)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/freertos-kernel/tasks.c")
        message(FATAL_ERROR "FreeRTOS: 'freertos-kernel/tasks.c' missing — run "
            "'git submodule update --init component/freertos-kernel_config/freertos-kernel'")
    endif()
    # CACHE ... FORCE so we win over the kernel's own defaults (FREERTOS_HEAP "4"). Heap: both
    # AmebaD cores + Bouffalo use the shared bflb_mm (TLSF); point FREERTOS_HEAP at the empty
    # stub so the kernel compiles no heap_N.c (the pvPortMalloc surface is freertos_config's
    # freertos_heap_bflb_mm.c over bflb_mm).
    set(FREERTOS_PORT "${_frt_port}" CACHE STRING "FreeRTOS port name" FORCE)
    set(FREERTOS_HEAP "${CMAKE_CURRENT_SOURCE_DIR}/freertos_heap_none.c"
        CACHE STRING "FreeRTOS heap model" FORCE)
    if(NOT TARGET freertos_kernel)
        add_subdirectory(freertos-kernel
                         "${CMAKE_BINARY_DIR}/component/freertos-kernel_config/freertos-kernel")
    endif()
    # The name every consumer already links.
    if(NOT TARGET FreeRTOS)
        add_library(FreeRTOS ALIAS freertos_kernel)
    endif()
    # The upstream kernel compiles -Wall -Wextra -Wpedantic -Werror; the platform
    # FreeRTOSConfig.h (empty configASSERT, etc.) can trip config-driven warnings inside the
    # otherwise-clean sources. Demote -Werror so they don't fail the build (warnings stay).
    target_compile_options(freertos_kernel PRIVATE -Wno-error)
    if(TARGET freertos_kernel_port)
        target_compile_options(freertos_kernel_port PRIVATE -Wno-error)
    endif()

    # TZ secure-context port (make SECURE=ON): the kernel (tasks.c/port.c/portasm.c)
    # must see configENABLE_TRUSTZONE=1 so the TCB carries xSecureContext and the
    # port compiles the secure-context save/restore. Scoped PRIVATE to the kernel
    # (NOT global) so the SDK's app_start in amebad_ram_hp keeps configENABLE_TRUSTZONE=0
    # and does NOT compile its BOOT_IMG3/load_psram_image_s secure-load branch (the
    # bootloader owns the secure handoff). xSecureContext is appended to the end of
    # the TCB and there is no static task allocation, so this stays ABI-safe vs the
    # NS SDK TUs that include FreeRTOS headers with configENABLE_TRUSTZONE=0.
    if(AMEBAD_FREERTOS_SECURE AND "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
        target_compile_definitions(freertos_kernel PRIVATE CONFIG_TRUSTZONE_EN=1)
        if(TARGET freertos_kernel_port)
            target_compile_definitions(freertos_kernel_port PRIVATE CONFIG_TRUSTZONE_EN=1)
        endif()
    endif()
endif()

# Umbrella INTERFACE (伞形): consumers link `freertos-kernel_config` to get the
# freertos_config target (which PUBLIC-links freertos_config_common).
add_library(freertos-kernel_config INTERFACE)
target_link_libraries(freertos-kernel_config INTERFACE freertos_config)
