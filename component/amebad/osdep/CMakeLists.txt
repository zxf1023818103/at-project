# amebad_osdep — OS abstraction layer (osdep) for the KM4 image2. KM4-only (KM0
# image2 didn't need these). Sources vendored out of the SDK (os/freertos +
# os/os_dep); headers still come from amebad_sdk_includes' os/os_dep paths.
#   freertos_service.c   rtw_* OS wrappers over FreeRTOS (rtw_up_sema, ...)
#   device_lock.c        device_mutex_lock / device_mutex_unlock
#   osdep_service.c      the generic OSDEP service layer
#   psram_reserve.c      PSRAM heap reserve (g_Psram_heap_inited, ...)

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_osdep STATIC
    freertos_service.c
    device_lock.c
    osdep_service.c
    psram_reserve.c
)
target_link_libraries(amebad_osdep PUBLIC FreeRTOS)
target_compile_options(amebad_osdep PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
