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
#   (amebad_sdk_includes / amebad_freertos_headers)
#                    The chip-aware bundle of the SDK header search paths +
#                    the FreeRTOS header bundle, defined INLINE in this file
#                    (between the header-only components it links and usrcfg).
#                    Was the separate sdk_headers/ subdir; folded in here once it
#                    held only its CMakeLists (the SDK platform headers it once
#                    carried had moved to their owners).
#
#   usrcfg           Board config, one chip-gated CMakeLists over a flat folder;
#                    the SDK filename infix selects the lib: usrcfg_common
#                    (rtl8721d_* — bootcfg/ipccfg/wificfg, both cores), usrcfg_lp
#                    (rtl8721dlp_* — KM0 flash/intf/pinmap/sleep), usrcfg_hp
#                    (rtl8721dhp_* — KM4 trustzone/interface).
#
#   (heap)           Heap registration is the shared ram_heap_init()
#                    (component/freertos-kernel_config/ram_heap.c), driven by the app's linker
#                    symbols; the AmebaD-only freertos_port heap_5 glue lib is gone. The upstream
#                    FreeRTOS-Kernel (-> `FreeRTOS`) + per-core port/heap is built by
#                    component/freertos-kernel_config; the kernel/glue header bundle +
#                    the freertos_config <- amebad_sdk_includes attach are inline below.
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

# TF-M PSA secure storage (ITS + PS). When ON (make STORAGE=ON, implies TFM=ON),
# the TF-M SPE enables the Internal Trusted Storage + Protected Storage partitions
# (PS pulls the crypto partition / mbedcrypto into the SPE), backed by a real
# AmebaD flash driver; app_km4 runs a psa_its/psa_ps self-test. OFF by default.
option(AMEBAD_TFM_STORAGE "Enable TF-M PSA Internal Trusted Storage + Protected Storage + NS self-test" OFF)

# POSIX-fd VFS self-test (syscalls/newlib_syscalls.c): fopen/open + socket-fd over LittleFS +
# FreeRTOS+TCP. OFF by default (the VFS is otherwise gc-stripped when no consumer references it).
# Consumed by lfs_port (the vfs_selftest fn) + app_km4 (the call) below.
option(AMEBAD_VFS_SELFTEST "Build + run the POSIX-fd VFS self-test (fopen/open/socket over LFS+sockets)" OFF)

# Ed25519 secure-boot self-test (make SBOOT=ON). build_image_all.py signs image2 in the boot-ROM's
# SBOOT format; sboot_cli.c verifies the running signed image with the boot ROM's ed25519 verify.
# Non-destructive: proves the ROM verify path without burning eFuse. Read by app_km4 below.
option(AMEBAD_SBOOT "Ed25519-sign image2 + on-device `sboot` secure-boot self-test (no eFuse burn)" OFF)

# DEBUG eFuse provisioning aids (make EFUSE_DEBUG=ON) - NEVER ship: the `efuse_burn` console command,
# the boot-time dump of all eFuse keys + MACs, and the verbose bootloader log (Boot_Log_En). Default
# OFF; gates the NS callers (so the SPE eFuse read/write IOCTLs have no caller in a normal build).
option(AMEBAD_EFUSE_DEBUG "DEBUG eFuse burn/dump aids + verbose boot log (never ship)" OFF)

add_subdirectory(fwlib)              # chip fwlib: headers (amebad_fwlib_headers) + ROM-symbol ld vars + RAM HAL libs (amebad_ram_common/_lp/_hp, chip-gated)
add_subdirectory(xmodem)             # ROM xmodem headers (amebad_xmodem) + xmodem.ld symbols; linked by sdk_includes below
add_subdirectory(string_rom)         # ROM swlib-string headers (amebad_string_rom) + string_rom_km{4,0}.ld; linked by sdk_includes below
add_subdirectory(cmsis_rom)          # CMSIS core/DSP/mpu headers (amebad_cmsis_rom) + cmsis_rom_km{4,0}.ld; linked by sdk_includes below
add_subdirectory(dlist)              # header-only doubly-linked-list (amebad_dlist); linked by sdk_includes below (must precede sdk_headers)
add_subdirectory(platform_stdlib)    # header-only libc-retarget header (amebad_platform_stdlib: platform_stdlib_rtl8721d.h); linked by sdk_includes below (must precede sdk_headers)
# ============================================================================================
# amebad_sdk_includes + amebad_freertos_headers  (folded in from the former sdk_headers/ subdir,
# which by then held only this CMakeLists - the SDK platform headers it once carried had already
# moved to their owners). All paths are absolute (${AT_ROOT}/...), so no relocation was
# needed. Sits here (after the header-only components it links, before usrcfg) to keep the same
# target-definition order; the forward refs to later components (osdep/monitor/pmu/...) resolve
# at generate time as before.
# ============================================================================================
# amebad_sdk_includes — chip-aware INTERFACE bundle of the SDK include paths the
# AmebaD vendored headers still expect on the search path.
#
# Pruned to the LOAD-BEARING set only: the broad out-of-tree
# (platform/ameba-rtos-d) header dirs that ameba_soc.h + the os abstraction +
# the project config genuinely drag in. The dead/replaceable ones were dropped —
# WLAN driver (minus the one include/ that holds the global autoconf.h), lwip
# v1.4.1, INIC, polarssl, the SDK's mbedtls-2.4.0 (replaced with the SHARED
# component/mbedtls_config/mbedtls), test/example/utilities, api/wifi, api/network,
# mbed HAL (moved onto the wifi target, its only consumer). lwip is fully removed
# (IP stack = FreeRTOS-Plus-TCP); the residual driver/netif ABI shim is
# component/amebad/freertos_tcp_port/netif_shim.h (target amebad_netif_shim).
#
# In-tree members (NOT out-of-tree): amebad_fwlib_headers (69 chip/HAL headers),
# our freertos/config override, the upstream freertos-kernel headers, shared
# mbedtls/include. (The old platform/ header bundle that lived here is gone:
# platform_stdlib_rtl8721d.h + time64.h moved to the syscalls component, dlist.h
# became the amebad_dlist component, and the platform_stdlib.h dispatcher was
# deleted — only 8721d was ever selected.)
#
# What remains out-of-tree (the irreducible SDK header surface — would need
# vendoring those headers in-tree to remove): os + os/freertos + os_dep, soc
# cmsis/monitor/xmodem/swlib/misc, and the wlan component's headers (just for
# autoconf.h; lib_wlan.a/lib_wps.a live there).
#
# The per-core project config (base FreeRTOSConfig.h + platform_opts/autoconf +
# main.h) is now in-tree, owned by the app component: app_km0 (KM0) /
# app_km4 (KM4). Chip-gated below via _proj_cfg_inc.

# amebad_freertos_headers — the FreeRTOS HEADER bundle for AmebaD: the upstream kernel's
# FreeRTOS.h (include/) + portmacro.h (per-core port dir). INTERFACE so it carries only include
# dirs — linking the kernel STATIC would cycle back via freertos_config. Per-core port path
# (km0 = ARMv6-M CM0; km4 image2 = ARMv8-M CM33, no-TrustZone non-secure).
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_frt_port_path "GCC/ARM_CM0")
else()
    set(_frt_port_path "GCC/ARM_CM33_NTZ/non_secure")
endif()
set(_frt_src "${AT_ROOT}/component/freertos-kernel_config/freertos-kernel")
add_library(amebad_freertos_headers INTERFACE)
target_include_directories(amebad_freertos_headers SYSTEM INTERFACE
    "${_frt_src}/include"
    "${_frt_src}/portable/${_frt_port_path}")

add_library(amebad_sdk_includes INTERFACE)
# The entire SDK header surface comes via each owner's header INTERFACE target —
# header deps through target_link_libraries, NOT a pile of raw
# target_include_directories. Each *_headers INTERFACE is defined before its
# component's chip gate, so it exists on both cores (the headers are needed KM0+KM4
# even where the component's compiled lib is KM4-only). The per-core port / project
# config that used to be computed here now lives with its owner (amebad_freertos_headers
# picks the kernel port path per core; amebad_proj_config is defined by the matching app).
target_link_libraries(amebad_sdk_includes INTERFACE
    # in-tree header-only components (own + expose their own dir)
    amebad_fwlib_headers
    amebad_xmodem
    amebad_string_rom
    amebad_cmsis_rom
    amebad_dlist
    amebad_platform_stdlib
    freertos_config_common
    # SDK header surface (replaces the old raw target_include_directories block):
    amebad_freertos_headers       # upstream FreeRTOS.h + portmacro.h + the freertos glue headers
    amebad_proj_config            # per-core platform_opts / Kconfig autoconf / main.h
    amebad_osdep_headers          # os + os/freertos + os_dep
    amebad_monitor_headers        # shell / monitor_rom / rtl_trace
    amebad_pmu_headers            # rtl8721d_freertos_pmu.h (pulled by ameba_soc.h)
    amebad_syscall_port_headers   # time64.h
    amebad_wlan_headers           # autoconf.h (global Kconfig)
    amebad_mbedtls_headers        # shared upstream mbedtls/version.h
)

# OTA region — base addresses for the two image2 partitions. Default
# values match SDK upstream (rtl8721d_ota.h #define defaults). Override
# on the cmake line when relocating image2:
#   cmake -DAMEBAD_OTA1_ADDR=0x080xx000 -DAMEBAD_OTA2_ADDR=0x081xx000 ...
#
# Notes:
#   - 4 KB aligned (RSIP mask granularity).
#   - KM0 and KM4 image2 are combined into one blob; only KM0's start
#     address needs setting (KM4 follows immediately after).
#   - Consumed everywhere via SDK macro LS_IMG2_OTA{1,2}_ADDR:
#       * boot stage:   usrcfg_common/src/rtl8721d_bootcfg.c (OTA_Region[])
#       * image2 stage: SDK misc/rtl8721d_ota.c (OtaTargetHdr lookup, etc.)
#     fwlib/rtl8721d_ota.h provides the default via #ifndef so
#     the CMake -D injection below cleanly overrides it (no -Wmacro-redefined).
#
# Lives in sdk_includes (not usrcfg_common) because the macro reaches
# beyond board config — image2-stage SDK code references the same macro.
set(AMEBAD_OTA1_ADDR "0x08006000" CACHE STRING
    "AmebaD KM0 IMG2 OTA1 partition start address (4K-aligned)")
set(AMEBAD_OTA2_ADDR "0x08183000" CACHE STRING
    "AmebaD KM0 IMG2 OTA2 partition start address (4K-aligned)")
target_compile_definitions(amebad_sdk_includes INTERFACE
    "LS_IMG2_OTA1_ADDR=${AMEBAD_OTA1_ADDR}"
    "LS_IMG2_OTA2_ADDR=${AMEBAD_OTA2_ADDR}"
)

# RSIP Mask entry 0 — covers the system-data region (BACKUP + XIPSYS,
# 2*4K starting at 0x08002000 by default). SDK comment: "Entry 0 is
# already used by System Data". Entries 1-3 are user-customizable
# directly in usrcfg_common/src/rtl8721d_bootcfg.c (no cmake option
# for those, they're meant to be hand-edited).
#
# Defaults match SDK upstream (bootcfg.c literal + layout.ld
# BACKUP(0x08002000)+XIPSYS(0x08003000)). Override on the cmake line
# when relocating system data:
#   cmake -DAMEBAD_RSIP_MASK_ENTRY0_ADDR=0xXX \
#         -DAMEBAD_RSIP_MASK_ENTRY0_PAGES=N ...
#
# Notes:
#   - ADDR: 4K aligned. PAGES: count of 4K pages, max 255.
#   - Consumed by usrcfg_common/src/rtl8721d_bootcfg.c RSIP_Mask_Config[0].
#   - If you relocate ADDR, you also need to move BACKUP/XIPSYS in the
#     layout.ld files (still hard-coded — see Wave 2 plan).
#   - Lives in sdk_includes (not usrcfg_common) for parity with OTA
#     option; any future layout-driven option goes here too.
set(AMEBAD_RSIP_MASK_ENTRY0_ADDR "0x08002000" CACHE STRING
    "AmebaD RSIP mask entry 0 start address (system data, 4K-aligned)")
set(AMEBAD_RSIP_MASK_ENTRY0_PAGES "2" CACHE STRING
    "AmebaD RSIP mask entry 0 size in 4K pages (max 255)")
target_compile_definitions(amebad_sdk_includes INTERFACE
    "AMEBAD_RSIP_MASK_ENTRY0_ADDR=${AMEBAD_RSIP_MASK_ENTRY0_ADDR}"
    "AMEBAD_RSIP_MASK_ENTRY0_PAGES=${AMEBAD_RSIP_MASK_ENTRY0_PAGES}"
)

# (The former raw target_include_directories block — kernel include/port, freertos
# glue, osdep, monitor, pmu, syscall_port, at_cmd, wlan, mbedtls — moved to each
# owner's *_headers INTERFACE, linked above. See git history for the path list.)

# The shared freertos_config (the kernel's FreeRTOSConfig.h carrier, defined in
# component/freertos-kernel_config) needs AmebaD's platform_autoconf.h — pulled by
# FreeRTOSConfig.h's CONFIG_PLATFORM_8721D branch. Attach our include bundle onto it so the kernel
# and every freertos_config consumer resolve it. No cycle: amebad_sdk_includes reaches the FreeRTOS
# kernel headers only through amebad_freertos_headers (include-only, no link edge back to the
# kernel / freertos_config). (Moved here from component/amebad/freertos_port.)
if(TARGET freertos_config)
    target_link_libraries(freertos_config PUBLIC amebad_sdk_includes)
endif()
# ----------------------------------------- end folded sdk_headers -----------------------------
add_subdirectory(usrcfg)             # chip-gated: usrcfg_common (+ _lp on KM0 / _hp on KM4)
# (freertos_port removed: its only lib was the heap_5 glue; heap registration is now the shared
#  ram_heap_init() in component/freertos-kernel_config/ram_heap.c)
add_subdirectory(monitor)            # chip-gated (image2 shell/monitor)
add_subdirectory(osdep)              # KM4-gated internally (osdep/os abstraction)
add_subdirectory(mbed)               # KM4-gated: flattened mbed HAL (amebad_mbed: flash_api.c + headers), consumed by wifi
add_subdirectory(mbedtls_rom)        # KM4-gated: headers to call ROM mbedTLS 2.4.0 as SW (no build)
add_subdirectory(mbedtls_port)            # KM4-gated: AmebaD port of upstream mbedTLS v3.6.4 (config + FreeRTOS threading-alt + entropy) attached onto mbedcrypto/mbedx509 (added by component/CMakeLists.txt); umbrella `amebad_mbedtls`
# FreeRTOS+TCP: the portable core stack + config (freertos-plus-tcp_config) is added by
# the parent component/CMakeLists.txt; here we add the AmebaD Realtek NetworkInterface
# port (in-tree) that links it. The port also owns the netif_shim ABI header (INTERFACE
# target amebad_netif_shim).
add_subdirectory(freertos_tcp_port)  # KM4-gated: Realtek WiFi <-> stack bridge (`amebad_freertos_tcp`, DHCP bring-up, EXCLUDE)
# (cares_port merged into the cross-platform component/c-ares_config; no platform-HAL code)
add_subdirectory(pmu)                # chip-gated: prebuilt PMC lib (lib_pmc_lp/hp.a) + pmu headers
add_subdirectory(wlan)               # KM4-gated: host WiFi driver libs (lib_wlan*.a) + API headers
add_subdirectory(wpa_supplicant)     # KM4-gated: supplicant cores (lib_wps.a + lib_eap.a) + config/glue source + headers
add_subdirectory(wifi)               # KM4-gated: host WiFi API + bring-up (amebad_wifi, EXCLUDE)
# The cross-platform config dirs (freertos-plus-tcp_config / ngtcp2_config / coremqtt_config /
# picotls_config / mbedtls_config / ...) are added by the parent component/CMakeLists.txt; the in-tree
# consumers below (app_km4, the ngtcp2 QUIC self-test gated by AMEBAD_ENABLE_QUIC, ...) just link the
# resulting targets.
add_subdirectory(hci)                # KM4-gated: BLE controller bring-up + HCI transport + bridge (no btgap.a)
# NimBLE: the config + header surface (nimble_config) is added by the parent
# component/CMakeLists.txt; here we add the AmebaD port (in-tree, KM4-gated; links
# nimble_config + amebad_hci).
add_subdirectory(nimble_port)        # KM4-gated: NimBLE host stack (`nimble` lib) on amebad_hci; SM crypto on mbedTLS
# (mcumgr_port / smp_bt merged into the cross-platform component/mcumgr_config; no platform-HAL code)
# newlib retarget: the cross-platform core (syscall_config OBJECT lib: VFS + malloc +
# byte-order) is added by the parent component/CMakeLists.txt; here we add the AmebaD
# console backend (in-tree). Both OBJECT libs, linked into the image by app_km4.
add_subdirectory(syscall_port)       # KM4-gated: AmebaD console backend (amebad_syscall_port OBJECT lib: console_loguart/lpuart) + vfs_lfs_mount via lfs_port
add_subdirectory(lfs_port)           # KM4-gated: AmebaD flash adapter for LittleFS (lfs_port; diskio over flash_api + mount) + attaches the AmebaD LittleFS self-test gates onto lfs_config

add_subdirectory(image_tool)         # defines amebad_make_{boot,image2}_all_bin*(...) + amebad_make_image_all_bin()
                                     #   (-> target amebad_image_all, wraps build_image_all.py). Build HELPER
                                     #   (CMake functions, global) - kept on the lib side; the executables in
                                     #   project/amebad call these functions.
add_subdirectory(common_ld_script)   # shared KM0-image ld fragments + AMEBAD_COMMON_*_LD cache vars (global)

# The executable-producing dirs (bootloader_km{0,4}, app_km{0,4}, app_km4_secure) live in project/amebad
# now (lib/executable split); the root CMakeLists add_subdirectory(project/amebad) after this component
# subtree, so they see image_tool's functions + common_ld_script's vars + every lib target defined above.
