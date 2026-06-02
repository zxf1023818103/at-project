# amebad_freertos_glue_km0 — AmebaD SDK FreeRTOS extensions that are NOT part
# of upstream FreeRTOS, split out of the kernel lib:
#   freertos_backtrace_ext.c   crash/backtrace support
#   freertos_heap5_config.c    the heap_5 region table for the AmebaD memory map
# Both are SDK glue (component/os/freertos/, referenced in-place), so they sit
# beside the upstream `FreeRTOS` kernel rather than inside it. KM0 only —
# mirrors project_lp make/os, which compiles these two into the os group.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

set(_glue "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/os/freertos")

foreach(_p freertos_backtrace_ext.c freertos_heap5_config.c)
    if(NOT EXISTS "${_glue}/${_p}")
        message(FATAL_ERROR "amebad_freertos_glue_km0: missing '${_glue}/${_p}'")
    endif()
endforeach()

add_library(amebad_freertos_glue_km0 STATIC
    "${_glue}/freertos_backtrace_ext.c"
    "${_glue}/freertos_heap5_config.c"
)

# Needs FreeRTOS headers (and, transitively, amebad_sdk_includes). PUBLIC so a
# consumer linking the glue also gets the FreeRTOS API.
target_link_libraries(amebad_freertos_glue_km0 PUBLIC FreeRTOS)

target_compile_options(amebad_freertos_glue_km0 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
