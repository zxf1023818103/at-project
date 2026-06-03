# amebad_freertos_glue_km4 — KM4 ABI build of the AmebaD SDK FreeRTOS
# extensions (freertos_backtrace_ext.c / freertos_heap5_config.c). Same
# chip-agnostic glue as freertos_glue_km0 — one shared vendored copy under
# component/amebad/freertos_glue/, compiled here under the KM4 toolchain.
# KM4 only. Mirrors project_hp make/os, which compiles these two into the os
# group alongside the kernel.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_glue "${CMAKE_SOURCE_DIR}/component/amebad/freertos_glue")

foreach(_p freertos_backtrace_ext.c freertos_heap5_config.c freertos_hooks.c)
    if(NOT EXISTS "${_glue}/${_p}")
        message(FATAL_ERROR "amebad_freertos_glue_km4: missing '${_glue}/${_p}'")
    endif()
endforeach()

add_library(amebad_freertos_glue_km4 STATIC
    "${_glue}/freertos_backtrace_ext.c"
    "${_glue}/freertos_heap5_config.c"
    "${_glue}/freertos_hooks.c"
)

target_link_libraries(amebad_freertos_glue_km4 PUBLIC FreeRTOS)

target_compile_options(amebad_freertos_glue_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
