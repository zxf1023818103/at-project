# amebad_monitor — AmebaD debug monitor / shell (shell_init_ram / shell_init_rom,
# the console command table, UART low-level I/O, trace), vendored out of the SDK
# (app/monitor/) into this folder.
#
# Split into a shared lib + a per-core lib (the per-core lib links the shared one):
#   amebad_monitor_common  {shell_ram,rtl_trace}.c  (both cores)
#   amebad_monitor         per-core entry — KM0: monitor_lp.c;
#                          KM4: monitor_hp.c + {shell_rom,monitor_rom}.c
# KM0 and KM4 are separate builds, so the per-core lib uses one name (amebad_monitor,
# no _km0/_km4 suffix — they never coexist). amebad_monitor_common is ABI-specific,
# compiled under whichever chip toolchain is configured. Headers come from
# amebad_sdk_includes (app/monitor/include, swlib/string, os_dep, fwlib) via the
# FreeRTOS link below.

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
else()
    return()
endif()

# monitor_rom.ld: the KM0 mask-ROM addresses of the shell_*/cmd_* ROM functions
# the KM0 monitor (monitor_lp.c) uses. Partitioned out of the shared KM0 ROM
# symbol table so they live with the monitor component; each KM0-image link stage
# cats it via AMEBAD_MONITOR_ROM_LD. KM4 builds the shell from source (shell_rom.c
# / monitor_rom.c) so there is no KM4 table.
set(AMEBAD_MONITOR_ROM_LD "${CMAKE_CURRENT_SOURCE_DIR}/monitor_rom.ld"
    CACHE INTERNAL "AmebaD KM0 monitor/shell ROM-symbol PROVIDE linker script")

# Vendor-code warning suppression, shared by the libs below.
set(_monitor_warnings
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)

# Shared across both cores. monitor sources pull freertos_pmu.h / FreeRTOS.h;
# link the kernel (which PUBLIC-links amebad_sdk_includes for the monitor /
# swlib / os_dep / fwlib headers).
# NB: the newlib console retarget (_write/_read via Diag*) that used to live here
# as low_level_io.c moved to component/amebad/syscalls (amebad_syscalls), the
# single home for the KM4 image's syscall surface. KM0 is stdio-free, so it never
# pulled low_level_io and needs no replacement.
add_library(amebad_monitor_common STATIC
    shell_ram.c
    rtl_trace.c
)
target_link_libraries(amebad_monitor_common PUBLIC FreeRTOS)
target_compile_options(amebad_monitor_common PRIVATE ${_monitor_warnings})

# Per-core monitor entry; links the shared lib (PUBLIC so consumers that link the
# per-core monitor also pull amebad_monitor_common). Same target name on both
# cores — they're never in the same build.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_library(amebad_monitor STATIC monitor_lp.c)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(amebad_monitor STATIC
        monitor_hp.c
        shell_rom.c
        monitor_rom.c
    )
endif()
target_link_libraries(amebad_monitor PUBLIC amebad_monitor_common)
target_compile_options(amebad_monitor PRIVATE ${_monitor_warnings})
