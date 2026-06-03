# amebad_monitor — AmebaD debug monitor / shell (shell_init_ram / shell_init_rom,
# the console command table, UART low-level I/O, trace), vendored out of the SDK
# (app/monitor/) into this folder.
#
# Split into a shared lib + a per-core lib (the per-core lib links the shared one):
#   amebad_monitor_common  ram/{shell_ram,rtl_trace,low_level_io}.c  (both cores)
#   amebad_monitor_km0     ram/monitor_lp.c                          (KM0)
#   amebad_monitor_km4     ram/monitor_hp.c + rom/{shell_rom,monitor_rom}.c (KM4)
# The shared sources are ABI-specific, so amebad_monitor_common is compiled under
# whichever chip toolchain is configured. Target names are kept so the app_km0 /
# app_km4 link groups don't change (they list both the per-core lib and the
# common lib). Headers come from amebad_sdk_includes (app/monitor/include,
# swlib/string, os_dep, fwlib) via the FreeRTOS link below.

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
    ram/shell_ram.c
    ram/rtl_trace.c
    ram/low_level_io.c
)
target_link_libraries(amebad_monitor_common PUBLIC FreeRTOS)
target_compile_options(amebad_monitor_common PRIVATE ${_monitor_warnings})

# Per-core monitor entry; links the shared lib (PUBLIC so consumers that link a
# per-core monitor also pull amebad_monitor_common).
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_library(amebad_monitor_km0 STATIC ram/monitor_lp.c)
    target_link_libraries(amebad_monitor_km0 PUBLIC amebad_monitor_common)
    target_compile_options(amebad_monitor_km0 PRIVATE ${_monitor_warnings})
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(amebad_monitor_km4 STATIC
        ram/monitor_hp.c
        rom/shell_rom.c
        rom/monitor_rom.c
    )
    target_link_libraries(amebad_monitor_km4 PUBLIC amebad_monitor_common)
    target_compile_options(amebad_monitor_km4 PRIVATE ${_monitor_warnings})
endif()
