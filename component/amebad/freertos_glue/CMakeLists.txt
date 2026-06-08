# amebad_freertos_glue — AmebaD SDK FreeRTOS extensions that are NOT part of
# upstream FreeRTOS, vendored out of the SDK and kept beside the upstream
# `FreeRTOS` kernel rather than inside it:
#   freertos_backtrace_ext.c   crash/backtrace support
#   freertos_heap5_config.c    the heap_5 region table for the AmebaD memory map
#   freertos_hooks.c           vApplicationIdleHook / ...StackOverflowHook /
#                              ...MallocFailedHook (the SDK kept these in port.c;
#                              the upstream ARM port doesn't, so they live here)
#
# One folder, one CMakeLists, gated on AMEBAD_CHIP: the same source is compiled
# under whichever chip toolchain is configured, producing amebad_freertos_glue_km0
# (KM0 ABI) or amebad_freertos_glue_km4 (KM4 ABI). Target names are kept so the
# app_km0 / app_km4 link groups don't change. Mirrors project_lp/project_hp
# make/os, which compile these into the os group alongside the kernel.

if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_glue_tgt amebad_freertos_glue_km0)
elseif("${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    set(_glue_tgt amebad_freertos_glue_km4)
else()
    return()
endif()

set(_glue "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(_p freertos_backtrace_ext.c freertos_heap5_config.c freertos_hooks.c)
    if(NOT EXISTS "${_glue}/${_p}")
        message(FATAL_ERROR "amebad_freertos_glue: missing '${_glue}/${_p}'")
    endif()
endforeach()

add_library(${_glue_tgt} STATIC
    "${_glue}/freertos_backtrace_ext.c"
    "${_glue}/freertos_heap5_config.c"
    "${_glue}/freertos_hooks.c"
)

# Needs FreeRTOS headers (and, transitively, amebad_sdk_includes). PUBLIC so a
# consumer linking the glue also gets the FreeRTOS API.
target_link_libraries(${_glue_tgt} PUBLIC FreeRTOS)

target_compile_options(${_glue_tgt} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
if(AMEBAD_ENABLE_TZ_DEMO)   # temporary os_heap_init boot-progress markers (ZZ-HEAP-*)
    target_compile_definitions(${_glue_tgt} PRIVATE AMEBAD_TZ_DEMO_DBG=1)
endif()
