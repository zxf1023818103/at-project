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

set(_mon "${CMAKE_CURRENT_SOURCE_DIR}")

# Shared across both cores.
add_library(amebad_monitor_common STATIC
    "${_mon}/ram/shell_ram.c"
    "${_mon}/ram/rtl_trace.c"
    "${_mon}/ram/low_level_io.c"
)
# monitor sources pull freertos_pmu.h / FreeRTOS.h; link the kernel (which
# PUBLIC-links amebad_sdk_includes for the monitor/swlib/os_dep/fwlib headers).
target_link_libraries(amebad_monitor_common PUBLIC FreeRTOS)

set(_mon_targets amebad_monitor_common)

# Per-core monitor entry; links the shared lib (PUBLIC so consumers that link a
# per-core monitor also pull amebad_monitor_common).
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    add_library(amebad_monitor_km0 STATIC "${_mon}/ram/monitor_lp.c")
    target_link_libraries(amebad_monitor_km0 PUBLIC amebad_monitor_common)
    list(APPEND _mon_targets amebad_monitor_km0)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    add_library(amebad_monitor_km4 STATIC
        "${_mon}/ram/monitor_hp.c"
        "${_mon}/rom/shell_rom.c"
        "${_mon}/rom/monitor_rom.c"
    )
    target_link_libraries(amebad_monitor_km4 PUBLIC amebad_monitor_common)
    list(APPEND _mon_targets amebad_monitor_km4)
endif()

foreach(_t ${_mon_targets})
    target_compile_options(${_t} PRIVATE
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-function
        -Wno-sign-compare
    )
endforeach()
