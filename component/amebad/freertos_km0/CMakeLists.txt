# amebad_freertos_km0 — FreeRTOS v10.2.0 kernel + RTL8721D_LP port for the
# KM0 LP core. KM0 toolchain only.
#
# Mirrors the os group built by
#   platform/ameba-rtos-d/.../project_lp/asdk/make/os/Makefile
# i.e. the 7 kernel .c + the RTL8721D_LP port + heap_5 + two AmebaD glue
# files (freertos_backtrace_ext / freertos_heap5_config). No freertos_service.c
# / cmsis_os.c — make/os doesn't compile those into the KM0 image.
#
# Source split:
#   kernel + port + heap_5  -> component/thirdparty/freertos_amebad submodule
#                              (the FreeRTOS-Kernel fork's ameba-d-port branch;
#                               this is why the submodule exists).
#   glue (2 .c)             -> SDK in-place; not carried in the kernel submodule.
#
# FreeRTOS API headers (FreeRTOS.h, task.h, portmacro.h, ...) are taken from
# the SUBMODULE so they match the sources we compile. FreeRTOSConfig.h
# (project inc_lp), os_dep, the os/freertos wrapper dir (glue headers),
# fwlib headers and -DCONFIG_PLATFORM_8721D come via amebad_sdk_includes.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

set(_frt  "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos_amebad/FreeRTOS/Source")
set(_glue "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/os/freertos")

# Friendlier than a downstream "file not found" if the submodule isn't inited.
if(NOT EXISTS "${_frt}/tasks.c")
    message(FATAL_ERROR
        "amebad_freertos_km0: '${_frt}/tasks.c' missing — run "
        "'git submodule update --init component/thirdparty/freertos_amebad'")
endif()

add_library(amebad_freertos_km0 STATIC
    # kernel (freertos_amebad submodule) — exact make/os CSRC list
    "${_frt}/tasks.c"
    "${_frt}/list.c"
    "${_frt}/queue.c"
    "${_frt}/timers.c"
    "${_frt}/event_groups.c"
    "${_frt}/croutine.c"
    "${_frt}/stream_buffer.c"
    # KM0 LP port + heap_5 (submodule)
    "${_frt}/portable/GCC/RTL8721D_LP/port.c"
    "${_frt}/portable/MemMang/heap_5.c"
    # AmebaD glue — SDK-only, no submodule equivalent
    "${_glue}/freertos_backtrace_ext.c"
    "${_glue}/freertos_heap5_config.c"
)

# FreeRTOS headers from the submodule (source of truth for the AmebaD port);
# PUBLIC so consumers that #include "FreeRTOS.h" resolve the same version.
# SYSTEM keeps -Wall/-Wextra off vendor headers. Listed on the target so they
# precede the SDK's freertos include paths pulled in via amebad_sdk_includes.
target_include_directories(amebad_freertos_km0 SYSTEM PUBLIC
    "${_frt}/include"
    "${_frt}/portable/GCC/RTL8721D_LP"
)

# FreeRTOSConfig.h (inc_lp), os_dep, os/freertos wrapper dir (glue headers),
# fwlib headers + CONFIG_PLATFORM_8721D / OTA / RSIP defines. PUBLIC because
# FreeRTOS.h pulls FreeRTOSConfig.h — consumers need the same include set.
target_link_libraries(amebad_freertos_km0 PUBLIC amebad_sdk_includes)

# Vendor code — suppress -Wall/-Wextra noise we don't own.
target_compile_options(amebad_freertos_km0 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
