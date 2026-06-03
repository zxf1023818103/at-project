# amebad_monitor_km4 — KM4 debug monitor / shell. KM4 toolchain only.
#
# Mirrors project_hp make/app/monitor CSRC: ram/monitor_hp.c (HP, not the LP
# monitor_lp.c) + ram/shell_ram.c + ram/rtl_trace.c + ram/low_level_io.c.
# (The rom/* shell_rom/monitor_rom come from on-chip ROM via the symbol table,
# so they're not compiled here.) Sources vendored into the shared
# component/amebad/monitor/ folder (shared with monitor_km0, compiled per-ABI).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_mon "${CMAKE_SOURCE_DIR}/component/amebad/monitor")

# project_hp make/app/monitor CSRC: ram/{monitor_hp,shell_ram,rtl_trace,
# low_level_io} + rom/{shell_rom,monitor_rom} (the rom/ files are RAM-resident
# wrappers around ROM shell/monitor — they provide shell_init_rom etc.).
foreach(_p ram/monitor_hp.c ram/shell_ram.c ram/rtl_trace.c ram/low_level_io.c
           rom/shell_rom.c rom/monitor_rom.c)
    if(NOT EXISTS "${_mon}/${_p}")
        message(FATAL_ERROR "amebad_monitor_km4: missing '${_mon}/${_p}'")
    endif()
endforeach()

add_library(amebad_monitor_km4 STATIC
    "${_mon}/ram/monitor_hp.c"
    "${_mon}/ram/shell_ram.c"
    "${_mon}/ram/rtl_trace.c"
    "${_mon}/ram/low_level_io.c"
    "${_mon}/rom/shell_rom.c"
    "${_mon}/rom/monitor_rom.c"
)

target_link_libraries(amebad_monitor_km4 PUBLIC FreeRTOS)

target_compile_options(amebad_monitor_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
