# component/amebad/app_km4_freertos_secure — AmebaD RTL8721D KM4 secure world =
# a STANDALONE minimal FreeRTOS TrustZone secure image (NOT TF-M). It exists to
# bring up and test the FreeRTOS-Kernel ARM_CM33 secure-context feature: a non-
# secure FreeRTOS task safely calls a Secure-world function while the kernel saves/
# restores a per-task secure stack across preemption.
#
# Unlike app_km4_secure (TF-M, an ExternalProject), this is a plain in-tree CMake
# executable: the 4 FreeRTOS-Kernel ARM_CM33/secure/* sources + a tiny secure main/
# startup/demo-veneer + the RTL8721D SAU/IDAU port (target_cfg.c) reused verbatim
# from the TF-M platform. It links its own minimal scatter, exports the CMSE SG
# veneers via --cmse-implib (s_veneers_freertos.o, consumed by the NS app_km4
# link), and objcopy's to freertos_s.bin — a raw vector-table-first image that the
# bootloader boots cached-XIP @0x0E400000 exactly like tfm_s.bin (no IMG3 header).
#
# Opt-in: -DAMEBAD_FREERTOS_SECURE=ON (make SECURE=ON). KM4 only. Mutually
# exclusive with AMEBAD_TFM_BOOT (both own the secure world / the IMG3 flash slot).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()
if(NOT AMEBAD_FREERTOS_SECURE)
    return()
endif()
if(AMEBAD_TFM_BOOT)
    message(FATAL_ERROR
        "AMEBAD_FREERTOS_SECURE and AMEBAD_TFM_BOOT are mutually exclusive — both "
        "own the KM4 secure world / the dynamic IMG3 flash slot. Pick one "
        "(make SECURE=ON  or  make TFM=ON).")
endif()

# Reused, verbatim, from the TF-M platform port.
set(_plat   "${CMAKE_SOURCE_DIR}/component/amebad/app_km4_secure/platform/rtl8721d")
# Upstream FreeRTOS-Kernel ARM_CM33 secure port.
set(_frt_s  "${CMAKE_SOURCE_DIR}/component/freertos-kernel_config/freertos-kernel/portable/GCC/ARM_CM33/secure")
# The AmebaD SDK's vanilla CMSIS core (core_armv8mml.h + cmsis_gcc/_compiler/_version
# — does NOT pull platform_autoconf.h, unlike the sibling ameba-rtos copy). The reused
# rtl8721d_device.h #include "core_cm33.h" is satisfied by our core_cm33.h shim (this
# dir, first on the path) which routes to core_armv8mml.h.
set(_cmsis  "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/soc/realtek/amebad/cmsis")

foreach(_p
        "${_plat}/target_cfg.c" "${_plat}/cmsis_core/rtl8721d_device.h"
        "${_frt_s}/secure_context.c" "${_cmsis}/core_armv8mml.h")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "app_km4_freertos_secure: missing reused source '${_p}'")
    endif()
endforeach()

add_executable(target_img3_km4_frtos_s
    "${CMAKE_CURRENT_SOURCE_DIR}/secure_startup.c"   # vectors + Reset_Handler -> SecureMain
    "${CMAKE_CURRENT_SOURCE_DIR}/secure_main.c"      # SAU/IDAU + NS handoff + BLXNS
    "${CMAKE_CURRENT_SOURCE_DIR}/secure_demo.c"      # the NSC demo veneer
    "${_plat}/target_cfg.c"                          # reused RTL8721D SAU/IDAU/NVIC
    "${_frt_s}/secure_context.c"                     # FreeRTOS per-task secure context
    "${_frt_s}/secure_context_port.c"
    "${_frt_s}/secure_heap.c"                        # self-contained secure heap (S_DATA)
    "${_frt_s}/secure_init.c"                        # SecureInit_* veneers
)

# Include order: OUR minimal FreeRTOSConfig.h first, then the reused port headers
# + a full CMSIS core. (M33 feature macros __SAUREGION_PRESENT etc. are self-
# defined by rtl8721d_device.h, so no extra -D needed.)
target_include_directories(target_img3_km4_frtos_s PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"            # FreeRTOSConfig.h (secure-minimal), core_cm33.h shim
    "${CMAKE_CURRENT_SOURCE_DIR}/include"   # secure_demo.h (shared with the NS test)
    "${_frt_s}"                              # secure_context.h / secure_heap.h / secure_port_macros.h
    "${_plat}"                              # target_cfg.h
    "${_plat}/partition"                    # region_defs.h, flash_layout.h
    "${_plat}/cmsis_core"                   # rtl8721d_device.h, cmsis.h
    "${_cmsis}"                             # core_cm33.h + cmsis_gcc/_compiler/_version
)

# -mcmse / armv8-m.main+dsp / fpv5-sp-d16 / hard-float / -DCONFIG_PLATFORM_8721D
# all come from cmake/amebad-km4.cmake (global). Vendor sources: quiet warnings.
target_compile_options(target_img3_km4_frtos_s PRIVATE
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-sign-compare)

set(_implib "${CMAKE_CURRENT_BINARY_DIR}/s_veneers_freertos.o")
target_link_options(target_img3_km4_frtos_s PRIVATE
    "-T${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_freertos_s.ld"
    "-nostartfiles"                                 # we provide Reset_Handler (no crt0/_start)
    "-Wl,-e,Reset_Handler"
    "-Wl,--cmse-implib,--out-implib=${_implib}"     # SG-veneer import lib for the NS link
    "-Wl,--section-start,.gnu.sgstubs=0x0E400400"   # veneers @ CMSE_VENEER_REGION_START (NSC region)
    "-Wl,--no-gc-sections"                          # gc-sections breaks ld's CMSE stub sizing
    "-Wl,-Map=$<TARGET_FILE_DIR:target_img3_km4_frtos_s>/target_img3_frtos_s.map"
)
# The global toolchain link line is -nostdlib; pull in just libc (memcpy/memset the
# compiler emits for the startup init loops) + libgcc (__gnu_cmse_nonsecure_call for
# the BLXNS, plus any aeabi intrinsics). No crt0 — entry is Reset_Handler.
target_link_libraries(target_img3_km4_frtos_s PRIVATE c gcc)
set_property(TARGET target_img3_km4_frtos_s APPEND PROPERTY
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rlx8721d_freertos_s.ld")
# Make the import lib a known byproduct (the NS link consumes it).
set_property(TARGET target_img3_km4_frtos_s APPEND PROPERTY
    ADDITIONAL_CLEAN_FILES "${_implib}")

# Raw binary (vector-table-first, no header) — laid at the dynamic IMG3 slot by
# build_image_all.py, booted cached-XIP @0x0E400000 like tfm_s.bin.
add_custom_command(TARGET target_img3_km4_frtos_s POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary
            "$<TARGET_FILE:target_img3_km4_frtos_s>"
            "${CMAKE_CURRENT_BINARY_DIR}/freertos_s.bin"
    COMMAND "${CMAKE_SIZE}" -A --radix=16 "$<TARGET_FILE:target_img3_km4_frtos_s>"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/freertos_s.bin"
    COMMENT "amebad: objcopy FreeRTOS secure image -> freertos_s.bin"
    VERBATIM)
 