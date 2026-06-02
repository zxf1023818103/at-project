# amebad_monitor_km4 — KM4 debug monitor / shell. KM4 toolchain only.
#
# Mirrors project_hp make/app/monitor CSRC: ram/monitor_hp.c (HP, not the LP
# monitor_lp.c) + ram/shell_ram.c + ram/rtl_trace.c + ram/low_level_io.c.
# (The rom/* shell_rom/monitor_rom come from on-chip ROM via the symbol table,
# so they're not compiled here.) Sources referenced in-place from the SDK.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_mon "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/soc/realtek/amebad/app/monitor/ram")

foreach(_p monitor_hp.c shell_ram.c rtl_trace.c low_level_io.c)
    if(NOT EXISTS "${_mon}/${_p}")
        message(FATAL_ERROR "amebad_monitor_km4: missing '${_mon}/${_p}'")
    endif()
endforeach()

add_library(amebad_monitor_km4 STATIC
    "${_mon}/monitor_hp.c"
    "${_mon}/shell_ram.c"
    "${_mon}/rtl_trace.c"
    "${_mon}/low_level_io.c"
)

target_link_libraries(amebad_monitor_km4 PUBLIC FreeRTOS)

target_compile_options(amebad_monitor_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
