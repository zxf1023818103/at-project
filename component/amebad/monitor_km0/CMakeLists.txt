# amebad_monitor_km0 — KM0 debug monitor / shell (shell_init_ram, the
# console command table, UART low-level I/O, trace). KM0 toolchain only.
#
# Mirrors the CSRC list in
#   platform/ameba-rtos-d/.../project_lp/asdk/make/monitor/Makefile
# (ram/monitor_lp.c, ram/shell_ram.c, ram/rtl_trace.c, ram/low_level_io.c —
# KM0 uses monitor_lp, not monitor_hp).
#
# Sources are vendored into the shared component/amebad/monitor/ folder (one
# copy, compiled per-ABI by monitor_km0 + monitor_km4 — mirrors freertos_glue/);
# the monitor *headers* still come from amebad_sdk_includes (app/monitor/include,
# swlib/string, os_dep, fwlib).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

set(_mon "${CMAKE_SOURCE_DIR}/component/amebad/monitor/ram")

foreach(_p monitor_lp.c shell_ram.c rtl_trace.c low_level_io.c)
    if(NOT EXISTS "${_mon}/${_p}")
        message(FATAL_ERROR "amebad_monitor_km0: missing '${_mon}/${_p}'")
    endif()
endforeach()

add_library(amebad_monitor_km0 STATIC
    "${_mon}/monitor_lp.c"
    "${_mon}/shell_ram.c"
    "${_mon}/rtl_trace.c"
    "${_mon}/low_level_io.c"
)

# Headers come via amebad_sdk_includes (app/monitor/include, swlib/string for
# strproc.h, os_dep, fwlib). monitor_lp.c pulls freertos_pmu.h / FreeRTOS.h, so
# also link the upstream FreeRTOS kernel to get the submodule FreeRTOS headers
# (it PUBLIC-links amebad_sdk_includes transitively).
target_link_libraries(amebad_monitor_km0 PUBLIC FreeRTOS)

target_compile_options(amebad_monitor_km0 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
