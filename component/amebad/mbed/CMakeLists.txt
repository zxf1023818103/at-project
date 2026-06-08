# component/amebad/mbed — Realtek AmebaD "mbed" HAL, vendored + FLATTENED from
#   platform/ameba-rtos-d/component/common/mbed/  (api/ + hal/ + hal_ext/ +
#   targets/hal/rtl8721d/  ->  one flat dir here; basenames are unique).
#
# This is the single home for the mbed HAL: the API headers (flash_api.h /
# pinmap.h / spdio_api.h / device.h / objects.h / PinNames.h ...) and the
# rtl8721d target sources. Replaces the former 8-header subset under
# sdk_headers/component/common/mbed/ (deleted).
#
# Only flash_api.c is compiled today (wifi's flash backend: flash_erase_sector /
# flash_*_word); the other target *.c are vendored but not built until a consumer
# needs them. Resync with the SDK when the platform/ameba-rtos-d submodule moves.
#
# KM4-gated: wifi (its only consumer) is KM4-only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_mbed STATIC
    flash_api.c
)
# PUBLIC: consumers (wifi) that link amebad_mbed get the flat mbed headers.
target_include_directories(amebad_mbed SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
# flash_api.c needs basic_types / fwlib HAL headers (via sdk_includes).
target_link_libraries(amebad_mbed PUBLIC amebad_sdk_includes)
target_compile_options(amebad_mbed PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
