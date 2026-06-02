# FreeRTOS — upstream FreeRTOS v10.2.0 kernel, built straight from the
# component/thirdparty/freertos_amebad submodule. Target is named `FreeRTOS`
# so consumers link it as target_link_libraries(<tgt> FreeRTOS).
#
# Pure upstream kernel only: the 7 generic .c + the chip's GCC port + heap_5.
# The AmebaD SDK FreeRTOS *extensions* (freertos_backtrace_ext,
# freertos_heap5_config) are NOT upstream — they live in the separate
# amebad_freertos_glue_km0 component.
#
# The submodule (v10.2.0) predates FreeRTOS-Kernel's own CMakeLists, so we
# compile its Source/ tree directly instead of add_subdirectory'ing it. If the
# fork later grows an upstream CMakeLists, this can switch to add_subdirectory +
# the standard freertos_kernel target.
#
# FreeRTOSConfig.h (project inc_lp), the port's portmacro.h, os_dep and
# -DCONFIG_PLATFORM_8721D come via amebad_sdk_includes.

# Chip -> GCC port directory. KM0 LP today; KM4 HP is TrustZone-split
# (RTL8721D_HP/{non_secure,secure}) and gets wired when KM4 image2 lands.
if("${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    set(_port "GCC/RTL8721D_LP")
else()
    return()
endif()

set(_frt "${CMAKE_SOURCE_DIR}/component/thirdparty/freertos_amebad/FreeRTOS/Source")

if(NOT EXISTS "${_frt}/tasks.c")
    message(FATAL_ERROR
        "FreeRTOS: '${_frt}/tasks.c' missing — run "
        "'git submodule update --init component/thirdparty/freertos_amebad'")
endif()

add_library(FreeRTOS STATIC
    "${_frt}/tasks.c"
    "${_frt}/list.c"
    "${_frt}/queue.c"
    "${_frt}/timers.c"
    "${_frt}/event_groups.c"
    "${_frt}/croutine.c"
    "${_frt}/stream_buffer.c"
    "${_frt}/portable/${_port}/port.c"
    "${_frt}/portable/MemMang/heap_5.c"
)

# FreeRTOS API headers from the submodule (source of truth); PUBLIC so consumers
# that #include "FreeRTOS.h" resolve the same version. Listed on the target so
# they precede the SDK's freertos include paths from amebad_sdk_includes.
target_include_directories(FreeRTOS SYSTEM PUBLIC
    "${_frt}/include"
    "${_frt}/portable/${_port}"
)

# FreeRTOSConfig.h (inc_lp), os_dep, fwlib headers + CONFIG_PLATFORM_8721D.
# PUBLIC because FreeRTOS.h pulls FreeRTOSConfig.h — consumers need the set.
target_link_libraries(FreeRTOS PUBLIC amebad_sdk_includes)

# Vendor code — suppress -Wall/-Wextra noise we don't own.
target_compile_options(FreeRTOS PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
