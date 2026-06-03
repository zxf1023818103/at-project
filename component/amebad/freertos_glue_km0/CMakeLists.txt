# amebad_freertos_glue_km0 — AmebaD SDK FreeRTOS extensions that are NOT part
# of upstream FreeRTOS, split out of the kernel lib:
#   freertos_backtrace_ext.c   crash/backtrace support
#   freertos_heap5_config.c    the heap_5 region table for the AmebaD memory map
#   freertos_hooks.c           vApplicationIdleHook / ...StackOverflowHook (the
#                              SDK kept these in port.c; the upstream ARM port
#                              doesn't, so they live here now)
# All are SDK glue, vendored out of the SDK into component/amebad/freertos_glue/
# (one shared copy, compiled per-ABI by km0 + km4) so the build no longer reaches
# into platform/ameba-rtos-d/component/os/freertos/. They sit beside the upstream
# `FreeRTOS` kernel rather than inside it. KM0 only — mirrors project_lp make/os,
# which compiles these two into the os group.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

set(_glue "${CMAKE_SOURCE_DIR}/component/amebad/freertos_glue")

foreach(_p freertos_backtrace_ext.c freertos_heap5_config.c freertos_hooks.c)
    if(NOT EXISTS "${_glue}/${_p}")
        message(FATAL_ERROR "amebad_freertos_glue_km0: missing '${_glue}/${_p}'")
    endif()
endforeach()

add_library(amebad_freertos_glue_km0 STATIC
    "${_glue}/freertos_backtrace_ext.c"
    "${_glue}/freertos_heap5_config.c"
    "${_glue}/freertos_hooks.c"
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
