# amebad_monitor — AmebaD debug monitor / shell (shell_init_ram / shell_init_rom,
# the console command table, UART low-level I/O, trace), vendored out of the SDK
# (app/monitor/) into this folder.
#
# One chip-gated CMakeLists builds amebad_monitor_km0 (KM0) or amebad_monitor_km4
# (KM4) from the shared sources:
#   ram/monitor_lp.c (KM0) | ram/monitor_hp.c (KM4)   per-core monitor entry
#   ram/{shell_ram,rtl_trace,low_level_io}.c          shared by both cores
#   rom/{shell_rom,monitor_rom}.c                     KM4 only (RAM-resident
#                                                     wrappers around the ROM
#                                                     shell/monitor)
# Target names are kept so the app_km0 / app_km4 link groups don't change.
# Headers come from amebad_sdk_includes (app/monitor/include, swlib/string,
# os_dep, fwlib).

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_mon_tgt amebad_monitor_km0)
    set(_mon_srcs
        ram/monitor_lp.c
        ram/shell_ram.c
        ram/rtl_trace.c
        ram/low_level_io.c)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_mon_tgt amebad_monitor_km4)
    set(_mon_srcs
        ram/monitor_hp.c
        ram/shell_ram.c
        ram/rtl_trace.c
        ram/low_level_io.c
        rom/shell_rom.c
        rom/monitor_rom.c)
else()
    return()
endif()

set(_mon "${CMAKE_CURRENT_SOURCE_DIR}")

set(_srcs)
foreach(_p ${_mon_srcs})
    if(NOT EXISTS "${_mon}/${_p}")
        message(FATAL_ERROR "amebad_monitor: missing '${_mon}/${_p}'")
    endif()
    list(APPEND _srcs "${_mon}/${_p}")
endforeach()

add_library(${_mon_tgt} STATIC ${_srcs})

# monitor_*.c pull freertos_pmu.h / FreeRTOS.h; link the kernel (which PUBLIC-
# links amebad_sdk_includes for the monitor / swlib / os_dep / fwlib headers).
target_link_libraries(${_mon_tgt} PUBLIC FreeRTOS)

target_compile_options(${_mon_tgt} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
