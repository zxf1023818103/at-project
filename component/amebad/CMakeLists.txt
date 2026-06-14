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
#   freertos_port    The AmebaD FreeRTOS layer in one folder: (1) the upstream
#                    FreeRTOS-Kernel V10.6.2 from the component/thirdparty/
#                    freertos-kernel submodule (shared with Bouffalo), via
#                    add_subdirectory + a per-core ARMv8-M GCC port — target
#                    `FreeRTOS` (ALIAS of freertos_kernel); and (2) the SDK
#                    FreeRTOS extensions / glue (freertos_backtrace_ext /
#                    freertos_heap5_config / freertos_hooks), built per-core into
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

# TF-M secure world (in-tree). When ON, app_km4_secure builds TF-M as the KM4
# secure image (SPE) via ExternalProject (tfm_spe), and the KM4 bootloader copies
# tfm_s from flash 0x08100000 into secure SRAM and secure-jumps to it, bypassing
# the image2/NS handoff + BOOT_RAM_TZCfg (TF-M owns TrustZone). The NS side is the
# normal KM4 image2 (app_km4), which links TF-M's exported NS interface
# (s_veneers.o + PSA headers). OFF by default; build with `make TFM=ON`.
option(AMEBAD_TFM_BOOT "Build + boot TF-M (KM4 secure-world SPE) instead of the plain NS app" OFF)

# Standalone FreeRTOS TrustZone secure world (in-tree). When ON, app_km4_freertos_secure
# builds a minimal FreeRTOS secure image (NOT TF-M) as the KM4 secure world, and the
# NS app_km4 switches to the TZ-aware GCC_ARM_CM33_NONSECURE FreeRTOS port (per-task
# secure context). Brings up + tests the FreeRTOS secure-context feature. OFF by
# default; build with `make SECURE=ON`. Mutually exclusive with AMEBAD_TFM_BOOT.
option(AMEBAD_FREERTOS_SECURE "Build + boot the standalone FreeRTOS TrustZone secure image (KM4 secure-context feature test)" OFF)

# TF-M PSA secure storage (ITS + PS). When ON (make STORAGE=ON, implies TFM=ON),
# the TF-M SPE enables the Internal Trusted Storage + Protected Storage partitions
# (PS pulls the crypto partition / mbedcrypto into the SPE), backed by a real
# AmebaD flash driver; app_km4 runs a psa_its/psa_ps self-test. OFF by default.
option(AMEBAD_TFM_STORAGE "Enable TF-M PSA Internal Trusted Storage + Protected Storage + NS self-test" OFF)

add_subdirectory(fwlib)              # chip fwlib: headers (amebad_fwlib_headers) + ROM-symbol ld vars + RAM HAL libs (amebad_ram_common/_lp/_hp, chip-gated)
add_subdirectory(xmodem)             # ROM xmodem headers (amebad_xmodem) + xmodem.ld symbols; linked by sdk_includes below
add_subdirectory(string_rom)         # ROM swlib-string headers (amebad_string_rom) + string_rom_km{4,0}.ld; linked by sdk_includes below
add_subdirectory(cmsis_rom)          # CMSIS core/DSP/mpu headers (amebad_cmsis_rom) + cmsis_rom_km{4,0}.ld; linked by sdk_includes below
add_subdirectory(dlist)              # header-only doubly-linked-list (amebad_dlist); linked by sdk_includes below (must precede sdk_headers)
add_subdirectory(platform_stdlib)    # header-only libc-retarget header (amebad_platform_stdlib: platform_stdlib_rtl8721d.h); linked by sdk_includes below (must precede sdk_headers)
add_subdirectory(sdk_headers)        # defines amebad_sdk_includes (aggregates fwlib_headers/xmodem/string_rom/cmsis_rom/dlist/platform_stdlib + SDK include paths); consumed by the ram HAL libs (forward ref) and app/boot
add_subdirectory(usrcfg)             # chip-gated: usrcfg_common (+ _lp on KM0 / _hp on KM4)
add_subdirectory(freertos_port)      # upstream FreeRTOS kernel (-> `FreeRTOS`) + SDK glue (amebad_freertos_glue_km{0,4}), chip-gated
add_subdirectory(monitor)            # chip-gated (image2 shell/monitor)
add_subdirectory(osdep)              # KM4-gated internally (osdep/os abstraction)
add_subdirectory(mbed)               # KM4-gated: flattened mbed HAL (amebad_mbed: flash_api.c + headers), consumed by wifi
add_subdirectory(at_cmd)             # KM4-gated internally (basic AT log/command service)
add_subdirectory(mbedtls_rom)        # KM4-gated: headers to call ROM mbedTLS 2.4.0 as SW (no build)
add_subdirectory(mbedtls_port)            # KM4-gated: AmebaD port of upstream mbedTLS v3.6.4 (config + FreeRTOS threading-alt + entropy) attached onto mbedcrypto/mbedx509 (added by component/CMakeLists.txt); umbrella `amebad_mbedtls`
# FreeRTOS+TCP: the portable core stack + config (freertos-plus-tcp_config) is added by
# the parent component/CMakeLists.txt; here we add the AmebaD Realtek NetworkInterface
# port (in-tree) that links it. The port also owns the netif_shim ABI header (INTERFACE
# target amebad_netif_shim).
add_subdirectory(freertos_tcp_port)  # KM4-gated: Realtek WiFi <-> stack bridge (`amebad_freertos_tcp`, DHCP bring-up, EXCLUDE)
add_subdirectory(pmu)                # chip-gated: prebuilt PMC lib (lib_pmc_lp/hp.a) + pmu headers
add_subdirectory(wlan)               # KM4-gated: host WiFi driver libs (lib_wlan*.a) + API headers
add_subdirectory(wpa_supplicant)     # KM4-gated: supplicant cores (lib_wps.a + lib_eap.a) + config/glue source + headers
add_subdirectory(wifi)               # KM4-gated: host WiFi API + bring-up (amebad_wifi, EXCLUDE)
# AMEBAD_ENABLE_QUIC (+ the derived AMEBAD_WOLFSSL_TLS force) is declared in the parent
# component/CMakeLists.txt, before wolfssl_config / ngtcp2_config are added there. It
# stays a cache var so app_km4 (below) still reads it.

# POSIX-fd VFS self-test (syscalls/newlib_syscalls.c): fopen/open + socket-fd over
# LittleFS + FreeRTOS+TCP. OFF by default (the VFS is otherwise gc-stripped when no
# consumer references it). Consumed by lfs_port (the vfs_selftest fn) + app_km4 (the
# call). Declared here so both subdirs see it.
option(AMEBAD_VFS_SELFTEST "Build + run the POSIX-fd VFS self-test (fopen/open/socket over LFS+sockets)" OFF)

# wolfssl_config / ngtcp2_config / coremqtt_config / corehttp_config / freertos-cli_config
# are added by the parent component/CMakeLists.txt (shared, consumer-coupled config dirs);
# the in-tree consumers below (app_km4, ngtcp2 self-test, ...) just link the resulting targets.
add_subdirectory(build_info)         # INTERFACE: UTS_VERSION banner -D (replaces the vendored build_info.h shim)
add_subdirectory(hci)                # KM4-gated: BLE controller bring-up + HCI transport + bridge (no btgap.a)
# NimBLE: the config + header surface (nimble_config) is added by the parent
# component/CMakeLists.txt; here we add the AmebaD port (in-tree, KM4-gated; links
# nimble_config + amebad_hci).
add_subdirectory(nimble_port)        # KM4-gated: NimBLE host stack (`nimble` lib) on amebad_hci; SM crypto on mbedTLS
# newlib retarget: the cross-platform core (syscall_config OBJECT lib: VFS + malloc +
# byte-order) is added by the parent component/CMakeLists.txt; here we add the AmebaD
# console backend (in-tree). Both OBJECT libs, linked into the image by app_km4.
add_subdirectory(syscall_port)       # KM4-gated: AmebaD console backend (amebad_syscall_port OBJECT lib: console_loguart/lpuart) + vfs_lfs_mount via lfs_port
add_subdirectory(lfs_port)           # KM4-gated: AmebaD flash adapter for LittleFS (amebad_lfs_port; diskio over flash_api + mount) + attaches the AmebaD LittleFS self-test gates onto lfs_config

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
    add_subdirectory(app_km4_secure)       # KM4 secure-world: TF-M SPE (ExternalProject), opt-in AMEBAD_TFM_BOOT
    add_subdirectory(app_km4_freertos_secure) # KM4 secure-world: standalone FreeRTOS TZ secure image, opt-in AMEBAD_FREERTOS_SECURE
endif()

# Cross-core final image. `cmake --build <km0-or-km4-build-dir> -t amebad_image_all`
# runs build_image_all.py to (re)build both cores and assemble the flashable
# Image_All.bin. Defined in both core configures; invoke from either.
amebad_make_image_all_bin()
