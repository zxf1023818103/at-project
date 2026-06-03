# amebad_monitor — AmebaD debug monitor / shell (shell_init_ram / shell_init_rom,
# the console command table, UART low-level I/O, trace), vendored out of the SDK
# (app/monitor/) into this folder.
#
# Split into a shared lib + a per-core lib (the per-core lib links the shared one):
#   amebad_monitor_common  {shell_ram,rtl_trace,low_level_io}.c  (both cores)
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
add_library(amebad_monitor_common STATIC
    shell_ram.c
    rtl_trace.c
    low_level_io.c
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
