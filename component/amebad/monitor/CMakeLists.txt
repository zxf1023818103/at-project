# amebad_monitor — the AmebaD console: FreeRTOS-Plus-CLI over the LOGUART, on both cores,
# with a KM0<->KM4 console hand-off (`>>` / `km0` / `km4`). Replaces the legacy ROM/RAM
# AT-monitor shell (shell_ram/shell_rom/monitor_rom/monitor_{hp,lp}.c + the .cmd.table.data
# command table + the LOGUART RX IRQ). The console POLLS the UART (no RX IRQ) — see monitor_cli.c.
#
# Split into a shared engine + a per-core seam (the per-core lib links the shared one):
#   amebad_monitor_common  monitor_cli.c                  (engine + commands + switch + IPC handler)
#   amebad_monitor         per-core monitor_plat_{hp,lp}.c (only the "KM4"/"KM0" identity string)
# KM0 and KM4 are separate builds, so the per-core lib uses one name (amebad_monitor, no
# _km0/_km4 suffix — they never coexist). Both link freertos_cli (the FreeRTOS-Plus-CLI core,
# component/freertos-cli_config).
#
# The vendored SDK HEADERS shell.h / monitor_rom.h / rtl_trace.h stay (the SDK umbrella
# rtl8721d.h pulls shell.h + monitor_rom.h; string_rom/diag.h pulls rtl_trace.h). They are
# header-only ABI surface; only the shell *implementation* sources were removed. ConfigDebugBuffer
# / ConfigDebugBufferGet are ROM symbols (rom_symbol .ld), independent of the deleted rtl_trace.c.

# Header-only INTERFACE (before the chip gate). Linked by amebad_sdk_includes instead of a raw
# target_include_directories — exposes shell.h / monitor_rom.h / rtl_trace.h / monitor.h.
add_library(amebad_monitor_headers INTERFACE)
target_include_directories(amebad_monitor_headers SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
else()
    return()
endif()

# monitor_rom.ld: KM0 mask-ROM addresses of the ROM boot-shell (shell_rom, cmd_*, shell_*). The
# image2 console no longer uses the ROM shell (it's FreeRTOS-Plus-CLI now), but the KM0
# BOOTLOADER still calls shell_rom (boot_ram_lp.c / boot_flash_lp.c — the boot-time uartburn
# console), so this ROM-symbol PROVIDE table is still cat'd into the KM0 bootloader links via
# AMEBAD_MONITOR_ROM_LD (NOT into image2 anymore). KM4 builds the boot shell differently — no table.
set(AMEBAD_MONITOR_ROM_LD "${CMAKE_CURRENT_SOURCE_DIR}/monitor_rom.ld"
    CACHE INTERNAL "AmebaD KM0 ROM boot-shell symbol PROVIDE linker script (bootloader-only)")

# Vendor-code warning suppression (the engine pulls the SDK headers under -Wall -Wextra).
set(_monitor_warnings
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Shared engine (both cores). Pulls FreeRTOS + the FreeRTOS-Plus-CLI core + the SDK headers
# (ameba_soc.h via amebad_sdk_includes, carried by FreeRTOS PUBLIC link).
add_library(amebad_monitor_common STATIC
    monitor_cli.c
)
target_link_libraries(amebad_monitor_common PUBLIC FreeRTOS freertos_cli)
target_compile_options(amebad_monitor_common PRIVATE ${_monitor_warnings})

# Per-core identity seam; links the shared engine (PUBLIC so a consumer that links the per-core
# lib also pulls amebad_monitor_common). Same target name on both cores — never in one build.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_library(amebad_monitor STATIC monitor_plat_lp.c)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(amebad_monitor STATIC monitor_plat_hp.c)
endif()
target_link_libraries(amebad_monitor PUBLIC amebad_monitor_common)
target_compile_options(amebad_monitor PRIVATE ${_monitor_warnings})
