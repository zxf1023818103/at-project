# amebad_osdep_km4 — OS abstraction layer (osdep) for the KM4 image2.
#
# The SDK groups these with FreeRTOS in os/freertos + os/os_dep:
#   freertos_service.c   rtw_* OS wrappers over FreeRTOS (rtw_up_sema, ...)
#   device_lock.c        device_mutex_lock / device_mutex_unlock
#   osdep_service.c      the generic OSDEP service layer
#   psram_reserve.c      PSRAM heap reserve (g_Psram_heap_inited, ...)
#
# KM0 image2 didn't need these; KM4's main/monitor/HAL do. Sources referenced
# in-place from the SDK. KM4 toolchain only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_os "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/os")

foreach(_p freertos/freertos_service.c os_dep/device_lock.c
           os_dep/osdep_service.c os_dep/psram_reserve.c)
    if(NOT EXISTS "${_os}/${_p}")
        message(FATAL_ERROR "amebad_osdep_km4: missing '${_os}/${_p}'")
    endif()
endforeach()

add_library(amebad_osdep_km4 STATIC
    "${_os}/freertos/freertos_service.c"
    "${_os}/os_dep/device_lock.c"
    "${_os}/os_dep/osdep_service.c"
    "${_os}/os_dep/psram_reserve.c"
)

target_link_libraries(amebad_osdep_km4 PUBLIC FreeRTOS)

target_compile_options(amebad_osdep_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
