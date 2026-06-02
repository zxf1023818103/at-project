# amebad_monitor_km0 — KM0 debug monitor / shell (shell_init_ram, the
# console command table, UART low-level I/O, trace). KM0 toolchain only.
#
# Mirrors the CSRC list in
#   platform/ameba-rtos-d/.../project_lp/asdk/make/monitor/Makefile
# (ram/monitor_lp.c, ram/shell_ram.c, ram/rtl_trace.c, ram/low_level_io.c —
# KM0 uses monitor_lp, not monitor_hp).
#
# Sources are referenced in-place from the SDK (not mirrored): they need no
# project patches, and the build already depends on platform/ameba-rtos-d for
# the ~30 SDK header paths carried by amebad_sdk_includes. Resync is automatic
# when the submodule pointer moves.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

set(_mon "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/soc/realtek/amebad/app/monitor/ram")

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
# also link amebad_freertos_km0 to get the submodule FreeRTOS headers (it
# PUBLIC-links amebad_sdk_includes transitively).
target_link_libraries(amebad_monitor_km0 PUBLIC amebad_freertos_km0)

target_compile_options(amebad_monitor_km0 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
