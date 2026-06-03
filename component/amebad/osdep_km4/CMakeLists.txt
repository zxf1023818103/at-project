# amebad_osdep_km4 — OS abstraction layer (osdep) for the KM4 image2.
#
# The SDK groups these with FreeRTOS in os/freertos + os/os_dep:
#   freertos_service.c   rtw_* OS wrappers over FreeRTOS (rtw_up_sema, ...)
#   device_lock.c        device_mutex_lock / device_mutex_unlock
#   osdep_service.c      the generic OSDEP service layer
#   psram_reserve.c      PSRAM heap reserve (g_Psram_heap_inited, ...)
#
# KM0 image2 didn't need these; KM4's main/monitor/HAL do. Sources vendored out
# of the SDK into src/ (headers still come from amebad_sdk_includes' os/os_dep
# paths). KM4 toolchain only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_src "${CMAKE_CURRENT_SOURCE_DIR}/src")

foreach(_p freertos_service.c device_lock.c osdep_service.c psram_reserve.c)
    if(NOT EXISTS "${_src}/${_p}")
        message(FATAL_ERROR "amebad_osdep_km4: missing '${_src}/${_p}'")
    endif()
endforeach()

add_library(amebad_osdep_km4 STATIC
    "${_src}/freertos_service.c"
    "${_src}/device_lock.c"
    "${_src}/osdep_service.c"
    "${_src}/psram_reserve.c"
)

target_link_libraries(amebad_osdep_km4 PUBLIC FreeRTOS)

target_compile_options(amebad_osdep_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
