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
#   usrcfg           Board config, one chip-gated CMakeLists over per-group
#                    subfolders: usrcfg_common (common/ — bootcfg/ipccfg/wificfg,
#                    built for both cores), usrcfg_lp (lp/ — KM0 flash/intf/
#                    pinmap/sleep), usrcfg_hp (hp/ — KM4 trustzone/interface).
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
#                    amebad_monitor_km0 or _km4 from the shared sources.
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

add_subdirectory(fwlib_headers)
add_subdirectory(sdk_includes)
add_subdirectory(ram_common)        # KM0-only consumer in practice; KM4 uses ROM
add_subdirectory(ram_lp)             # KM0-gated internally
add_subdirectory(ram_hp)             # KM4-gated internally
add_subdirectory(usrcfg)             # chip-gated: usrcfg_common (+ _lp on KM0 / _hp on KM4)
add_subdirectory(freertos)           # upstream FreeRTOS kernel -> target `FreeRTOS`
add_subdirectory(freertos_glue)      # SDK FreeRTOS extensions, chip-gated (after freertos)
add_subdirectory(monitor)            # chip-gated (image2 shell/monitor)
add_subdirectory(osdep_km4)          # KM4-gated internally (osdep/os abstraction)
add_subdirectory(ram_common_km4)     # KM4-gated internally (ipc/gdma/flash/libc subset)
add_subdirectory(ftl_km4)            # KM4-gated internally (flash translation layer)
add_subdirectory(at_cmd_km4)         # KM4-gated internally (basic AT log/command service)
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
endif()

# Cross-core final image. `cmake --build <km0-or-km4-build-dir> -t amebad_image_all`
# runs build_image_all.py to (re)build both cores and assemble the flashable
# Image_All.bin. Defined in both core configures; invoke from either.
amebad_make_image_all_bin()
