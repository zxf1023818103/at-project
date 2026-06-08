# AmebaD (RTL8721D) subtree. Entered only when AMEBAD_CHIP is set to
# amebad-km0 or amebad-km4 by the chip toolchain file (cmake/amebad-km{0,4}.cmake);
# the root CMakeLists.txt gates this add_subdirectory().
#
# Layout:
#
#   fwlib            The SDK fwlib subtree (flat folder, one CMakeLists). Carries
#                    the INTERFACE target amebad_fwlib_headers (fwlib/include/ —
#                    the 69 chip register / HAL inline headers), the
#                    AMEBAD_FWLIB_ROM_KM{4,0}_LD ROM-symbol ld vars, AND the
#                    RAM-resident HAL driver libs: ram_common (rtl8721d_* — shared
#                    HAL; KM0 builds all 20, KM4 builds only the few the HP ROM
#                    lacks), ram_lp (rtl8721dlp_* — KM0 clk/pinmap/startup/...),
#                    ram_hp (rtl8721dhp_* — KM4 audio/i2s/sd/...). (Merge of the
#                    former fwlib_headers/ + ram/.)
#   sdk_headers      Holds the 4 SDK platform headers vendored flat (platform/)
#                    AND defines the amebad_sdk_includes INTERFACE target — the
#                    chip-aware bundle of the remaining SDK header search paths
#                    (most still in-place at platform/ameba-rtos-d/). Picks
#                    app_km0 vs. app_km4 based on AMEBAD_CHIP. (Was the separate
#                    sdk_includes component, now folded in here.)
#
#   usrcfg           Board config, one chip-gated CMakeLists over a flat folder;
#                    the SDK filename infix selects the lib: usrcfg_common
#                    (rtl8721d_* — bootcfg/ipccfg/wificfg, both cores), usrcfg_lp
#                    (rtl8721dlp_* — KM0 flash/intf/pinmap/sleep), usrcfg_hp
#                    (rtl8721dhp_* — KM4 trustzone/interface).
#
#   freertos         Upstream FreeRTOS-Kernel V10.6.2 from the
#                    component/thirdparty/freertos-kernel submodule (shared with
#                    Bouffalo), via add_subdirectory + a per-core ARMv8-M GCC
#                    port. Target `FreeRTOS` (ALIAS of freertos_kernel).
#   freertos_glue    AmebaD SDK FreeRTOS extensions (freertos_backtrace_ext /
#                    freertos_heap5_config / freertos_hooks), vendored here and
#                    kept out of the kernel lib. One chip-gated CMakeLists builds
#                    amebad_freertos_glue_km0 or _km4 from the same sources.
#   monitor          AmebaD debug monitor / shell (shell_init_ram), vendored
#                    from SDK app/monitor. One chip-gated CMakeLists builds
#                    amebad_monitor_common (shared) + amebad_monitor (per-core
#                    entry — monitor_lp on KM0 / monitor_hp+rom on KM4).
#   app_km0          KM0 LP application image (image2): links the above into
#                    target_img2_km0.axf + the prebuilt lib_pmc_lp / lib_wifi_fw.
#                    KM0 only.
#
#   common_ld_script INTERFACE target carrying the KM0-image ld fragments
#                    shared across boot stages (rlx8721d_layout.ld via -L,
#                    rlx8721d_rom_symbol_acut.ld via cat). Must be added
#                    before the bootloaders so the target + the
#                    AMEBAD_COMMON_*_LD path vars are in scope.
#   image_tool       CMake helper module — defines the per-core packaging
#                    functions amebad_make_boot_all_bin / amebad_make_image2_all_bin
#                    (+ _hp) which wrap the SDK's stage-2 pipeline (objcopy split
#                    -> prepend 32-byte headers -> cat -> optional RSIP via
#                    EncTool) as POST_BUILD steps, plus amebad_make_image_all_bin()
#                    which wraps the cross-core assembler build_image_all.py as the
#                    `amebad_image_all` target. Local copies of prepend_header.sh +
#                    EncTool + build_image_all.py. Must be added before
#                    bootloader_km{0,4}/app_km{0,4} so the functions are in scope
#                    when those CMakeLists call them.
#
#   bootloader_km0   KM0 1st-stage loader (target_loader.axf, 2 .c files).
#   bootloader_km0_2nd
#                    KM0 2nd-stage bootloader (opt-in via AMEBAD_ENABLE_2ND_BOOT).
#   bootloader_km4   KM4 1st-stage loader (target_loader.axf, 3 .c files).

if(NOT AMEBAD_CHIP MATCHES "^amebad-km[04]$")
    message(FATAL_ERROR
        "component/amebad: requires "
        "-DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km{0,4}.cmake "
        "(AMEBAD_CHIP='${AMEBAD_CHIP}', expected 'amebad-km0' or 'amebad-km4')")
endif()

# Secure-world (IMG3 / RDP-style TrustZone-M) demo. OFF by default. When ON:
#  - app_km4_secure/ builds the secure image (target target_img3_km4_secure) + km4_image3_all.bin
#  - the KM4 bootloader is built with CONFIG_TRUSTZONE_EN=1 so it loads IMG3
#    (BOOT_FLASH_Image3Load) and BOOT_RAM_TZCfg programs the SAU/IDAU
#  - app_km4 carves the top secure RAM (BD_RAM_S/NSC) out of the NS region and
#    links cmse_implib.lib to call the veneers
#  - build_image_all.py lays km4_image3_all.bin at the fixed flash offset
# It only affects the bootloader-side TUs (boot_flash_hp.c, trustzonecfg.c) — NOT
# the app's FreeRTOS (configENABLE_TRUSTZONE would force the TZ kernel port we
# don't have), so it is applied per-target/per-source, never globally.
option(AMEBAD_ENABLE_TZ_DEMO "Build the RDP-style secure-world (IMG3) demo" OFF)

# TF-M-first boot: the KM4 bootloader copies the RAM-linked tfm_s SPE (built
# out-of-tree, laid at flash 0x08100000) into secure SRAM and secure-jumps to it,
# bypassing the image2/NS handoff + BOOT_RAM_TZCfg (TF-M owns TrustZone). For
# bringing up TF-M on AmebaD HW. OFF by default. tfm_s.bin is a build artifact
# (not in VCS): build TF-M out of tree and drop bin/tfm_s.bin into the secure
# image's build dir (build/km4/component/amebad/app_km4_secure/); the ns_stub
# blob is generated there by the ns_stub target.
option(AMEBAD_TFM_BOOT "KM4 bootloader boots tfm_s (TF-M SPE) instead of the NS app" OFF)

add_subdirectory(fwlib)              # chip fwlib: headers (amebad_fwlib_headers) + ROM-symbol ld vars + RAM HAL libs (amebad_ram_common/_lp/_hp, chip-gated)
add_subdirectory(xmodem)             # ROM xmodem headers (amebad_xmodem) + xmodem.ld symbols; linked by sdk_includes below
add_subdirectory(string_rom)         # ROM swlib-string headers (amebad_string_rom) + string_rom_km{4,0}.ld; linked by sdk_includes below
add_subdirectory(cmsis_rom)          # CMSIS core/DSP/mpu headers (amebad_cmsis_rom) + cmsis_rom_km{4,0}.ld; linked by sdk_includes below
add_subdirectory(sdk_headers)        # flat platform/ headers + defines amebad_sdk_includes (aggregates fwlib_headers/xmodem/string_rom/cmsis_rom + SDK include paths); consumed by the ram HAL libs (forward ref) and app/boot
add_subdirectory(usrcfg)             # chip-gated: usrcfg_common (+ _lp on KM0 / _hp on KM4)
add_subdirectory(freertos)           # upstream FreeRTOS kernel -> target `FreeRTOS`
add_subdirectory(freertos_glue)      # SDK FreeRTOS extensions, chip-gated (after freertos)
add_subdirectory(monitor)            # chip-gated (image2 shell/monitor)
add_subdirectory(osdep)              # KM4-gated internally (osdep/os abstraction)
add_subdirectory(mbed)               # KM4-gated: flattened mbed HAL (amebad_mbed: flash_api.c + headers), consumed by wifi
add_subdirectory(at_cmd)             # KM4-gated internally (basic AT log/command service)
add_subdirectory(mbedtls_rom)        # KM4-gated: headers to call ROM mbedTLS 2.4.0 as SW (no build)
add_subdirectory(netif_shim)         # KM4-gated: residual driver<->netif ABI shim (lwip removed)
add_subdirectory(freertos_tcp)       # KM4-gated: FreeRTOS-Plus-TCP V4.0.0 (DHCP bring-up, EXCLUDE)
add_subdirectory(pmu)                # chip-gated: prebuilt PMC lib (lib_pmc_lp/hp.a) + pmu headers
add_subdirectory(wlan)               # KM4-gated: host WiFi driver libs (lib_wlan*.a) + API headers
add_subdirectory(wpa_supplicant)     # KM4-gated: supplicant cores (lib_wps.a + lib_eap.a) + config/glue source + headers
add_subdirectory(wifi)               # KM4-gated: host WiFi API + bring-up (amebad_wifi, EXCLUDE)
# QUIC test (ngtcp2 + full wolfSSL TLS 1.3). AMEBAD_ENABLE_QUIC forces the
# wolfSSL TLS layer ON (AMEBAD_WOLFSSL_TLS) before the wolfssl subdir reads it.
option(AMEBAD_ENABLE_QUIC "Build ngtcp2 QUIC client self-test (pulls full wolfSSL TLS 1.3)" OFF)
if(AMEBAD_ENABLE_QUIC)
    set(AMEBAD_WOLFSSL_TLS ON CACHE BOOL "wolfSSL TLS 1.3 + QUIC (forced by AMEBAD_ENABLE_QUIC)" FORCE)
endif()

add_subdirectory(wolfssl)            # KM4-gated: wolfCrypt (v5.9.1) bare-metal lib for on-target crypto benchmark (+TLS1.3/QUIC when AMEBAD_WOLFSSL_TLS)
add_subdirectory(ngtcp2)             # KM4-gated: ngtcp2 QUIC + crypto/wolfssl (only built when AMEBAD_ENABLE_QUIC)
add_subdirectory(build_info)         # INTERFACE: UTS_VERSION banner -D (replaces the vendored build_info.h shim)
add_subdirectory(hci)                # KM4-gated: BLE controller bring-up + HCI transport + bridge (no btgap.a)
add_subdirectory(syscalls)           # KM4-gated: consolidated newlib syscall retarget (amebad_syscalls OBJECT lib)
add_subdirectory(image_tool)         # defines amebad_make_{boot,image2}_all_bin*(...)
                                     #   + amebad_make_image_all_bin() (-> target
                                     #     amebad_image_all, wraps build_image_all.py)
add_subdirectory(common_ld_script)   # shared KM0-image ld fragments + path vars

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_subdirectory(bootloader_km0)
    add_subdirectory(bootloader_km0_2nd)   # opt-in via AMEBAD_ENABLE_2ND_BOOT
    add_subdirectory(app_km0)              # KM0 LP application image (image2)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_subdirectory(bootloader_km4)
    add_subdirectory(app_km4)              # KM4 HP application image (image2)
    add_subdirectory(app_km4_secure)       # KM4 secure-world image (IMG3), opt-in AMEBAD_ENABLE_TZ_DEMO
endif()

# Cross-core final image. `cmake --build <km0-or-km4-build-dir> -t amebad_image_all`
# runs build_image_all.py to (re)build both cores and assemble the flashable
# Image_All.bin. Defined in both core configures; invoke from either.
amebad_make_image_all_bin()
