# AmebaD (RTL8721D) subtree. Entered only when AMEBAD_CHIP is set to
# amebad-km0 or amebad-km4 by the chip toolchain file (cmake/amebad-km{0,4}.cmake);
# the root CMakeLists.txt gates this add_subdirectory().
#
# Layout:
#
#   fwlib_headers    INTERFACE target carrying component/soc/realtek/amebad/
#                    fwlib/include/ (the 69 chip register / HAL inline headers)
#                    after they were mirrored out of the SDK into this tree.
#   sdk_includes     INTERFACE target carrying the remaining ~30 SDK header
#                    search paths still in-place at platform/ameba-rtos-d/.
#                    Picks inc_lp vs. inc_hp based on AMEBAD_CHIP.
#
#   ram_common       HAL ".c"s shared by KM0 and KM4 (ram_common/ in the SDK).
#                    KM4 bootloader doesn't link this — KM4 pulls HAL from
#                    on-chip ROM via rom_symbol_acut_boot.ld PROVIDE.
#   ram_lp           KM0-only HAL ".c"s (clk, pmc-lp, ...). Built only when
#                    AMEBAD_CHIP==amebad-km0.
#   ram_hp           KM4-only HAL ".c"s. Built only when AMEBAD_CHIP==amebad-km4.
#
#   usrcfg_common    Chip-agnostic board config (bootcfg / ipccfg / wificfg).
#                    Built once per chip toolchain — the same .c is compiled
#                    twice on disk for KM0 ABI vs. KM4 ABI without duplicating.
#   usrcfg_lp        KM0 trustzone/sleep/audio-lp board config.
#   usrcfg_hp        KM4 trustzone/sleep/audio-hp board config.
#
#   freertos         Upstream FreeRTOS-Kernel V10.6.2 from the
#                    component/thirdparty/freertos-kernel submodule (shared with
#                    Bouffalo), via add_subdirectory + a per-core ARMv8-M GCC
#                    port. Target `FreeRTOS` (ALIAS of freertos_kernel).
#   freertos_glue    AmebaD SDK FreeRTOS extensions (freertos_backtrace_ext /
#                    freertos_heap5_config / freertos_hooks), vendored here and
#                    kept out of the kernel lib. One chip-gated CMakeLists builds
#                    amebad_freertos_glue_km0 or _km4 from the same sources.
#   monitor_km0      KM0 debug monitor / shell (shell_init_ram), SDK app/monitor
#                    referenced in-place. KM0 only.
#   app_km0          KM0 LP application image (image2): links the above into
#                    target_img2_km0.axf + the prebuilt lib_pmc_lp / lib_wifi_fw.
#                    KM0 only.
#
#   common_ld_script INTERFACE target carrying the KM0-image ld fragments
#                    shared across boot stages (rlx8721d_layout.ld via -L,
#                    rlx8721d_rom_symbol_acut.ld via cat). Must be added
#                    before the bootloaders so the target + the
#                    AMEBAD_COMMON_*_LD path vars are in scope.
#   image_tool       CMake helper module — defines
#                    amebad_make_boot_all_bin(<axf-target> <out-name> <rsip-addr>)
#                    which wraps the SDK's stage-2 pipeline (objcopy split ->
#                    prepend 32-byte headers -> cat -> optional RSIP via
#                    EncTool). Local copies of prepend_header.sh + EncTool.
#                    Must be added before bootloader_km{0,4} so the function
#                    is in scope by the time those CMakeLists call it.
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

add_subdirectory(fwlib_headers)
add_subdirectory(sdk_includes)
add_subdirectory(ram_common)        # KM0-only consumer in practice; KM4 uses ROM
add_subdirectory(ram_lp)             # KM0-gated internally
add_subdirectory(ram_hp)             # KM4-gated internally
add_subdirectory(usrcfg_common)
add_subdirectory(usrcfg_lp)          # KM0-gated internally
add_subdirectory(usrcfg_hp)          # KM4-gated internally
add_subdirectory(freertos)           # upstream FreeRTOS kernel -> target `FreeRTOS`
add_subdirectory(freertos_glue)      # SDK FreeRTOS extensions, chip-gated (after freertos)
add_subdirectory(monitor_km0)        # KM0-gated internally (image2 shell/monitor)
add_subdirectory(monitor_km4)        # KM4-gated internally (image2 shell/monitor)
add_subdirectory(osdep_km4)          # KM4-gated internally (osdep/os abstraction)
add_subdirectory(ram_common_km4)     # KM4-gated internally (ipc/gdma/flash/libc subset)
add_subdirectory(ftl_km4)            # KM4-gated internally (flash translation layer)
add_subdirectory(at_cmd_km4)         # KM4-gated internally (basic AT log/command service)
add_subdirectory(image_tool)         # defines amebad_make_boot_all_bin(...)
add_subdirectory(common_ld_script)   # shared KM0-image ld fragments + path vars

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_subdirectory(bootloader_km0)
    add_subdirectory(bootloader_km0_2nd)   # opt-in via AMEBAD_ENABLE_2ND_BOOT
    add_subdirectory(app_km0)              # KM0 LP application image (image2)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_subdirectory(bootloader_km4)
    add_subdirectory(app_km4)              # KM4 HP application image (image2)
endif()
