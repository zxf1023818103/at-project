# component/amebad/monitor_port — the AmebaD platform port for the cross-platform console
# engine (component/monitor_config, target `monitor`). Provides the monitor.h seam over the
# LOGUART: the RX backend (KM4 IRQ + StreamBuffer / KM0 poll), the KM0<->KM4 console hand-off
# (`>>`/`km0`/`km4` over IPC ch0), reboot, and the dw/ew/flash/efuse ROM commands. Target
# `monitor_port` (one lib per build; KM0 and KM4 never coexist).
#
# The vendored SDK HEADERS shell.h / monitor_rom.h / rtl_trace.h stay (the SDK umbrella
# rtl8721d.h pulls shell.h + monitor_rom.h; string_rom/diag.h pulls rtl_trace.h). They are
# header-only ABI surface. ConfigDebugBuffer / ConfigDebugBufferGet are ROM symbols
# (rom_symbol .ld), independent of the deleted rtl_trace.c.

# Header-only INTERFACE (before the chip gate). Linked by amebad_sdk_includes instead of a raw
# target_include_directories — exposes shell.h / monitor_rom.h / rtl_trace.h.
add_library(amebad_monitor_headers INTERFACE)
target_include_directories(amebad_monitor_headers SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
else()
    return()
endif()

# monitor_rom.ld: KM0 mask-ROM addresses of the ROM shell (shell_rom, cmd_*, shell_*). Cat'd into
# the KM0 bootloader AND image2 links via AMEBAD_MONITOR_ROM_LD (PROVIDE -> unreferenced entries
# cost nothing). Two live users on KM0: the BOOTLOADER calls shell_rom (boot-time uartburn
# console), and the image2 CLI's dw/ew/flash/efuse commands call the ROM cmd_* handlers. KM4 has
# no ROM table for these (it compiles cmd_* from monitor_rom.c), so no KM4 table.
set(AMEBAD_MONITOR_ROM_LD "${CMAKE_CURRENT_SOURCE_DIR}/monitor_rom.ld"
    CACHE INTERNAL "AmebaD KM0 ROM shell/cmd symbol PROVIDE linker script (bootloader + image2)")

# Vendor-code warning suppression (the port pulls the SDK headers under -Wall -Wextra).
set(_monitor_warnings
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# The port: the monitor.h seam glue (monitor_cli.c) + the per-core identity seam
# (monitor_plat_{hp,lp}.c). KM4 also compiles the dw/ew/flash/efuse ROM handlers
# (monitor_rom.c); KM0 gets them from monitor_rom.ld (ROM addresses). Links the shared
# engine `monitor` (PUBLIC, so a consumer of monitor_port pulls the engine too) + FreeRTOS
# (ameba_soc.h arrives transitively via amebad_sdk_includes).
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_library(monitor_port STATIC monitor_cli.c monitor_plat_lp.c)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(monitor_port STATIC monitor_cli.c monitor_plat_hp.c monitor_rom.c)
endif()
target_link_libraries(monitor_port PUBLIC monitor FreeRTOS)
target_compile_options(monitor_port PRIVATE ${_monitor_warnings})
