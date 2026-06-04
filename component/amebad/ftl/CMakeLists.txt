# amebad_ftl — Flash Translation Layer (ftl_init et al.) for the KM4 image2, plus
# its mbed flash_api backend (flash_erase_sector / flash_*_word). main.c's
# app_ftl_init() calls into ftl, which sits on flash_api. ftl.c + flash_api.c
# (and ftl's headers ftl.h / ftl_int.h) are vendored here; the mbed HAL *headers*
# still come from the SDK. No heavy (wifi/lwip) deps. KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# mbed HAL headers are vendored into the in-tree sdk_headers mirror (not pulled
# from platform/ameba-rtos-d anymore).
set(_base "${CMAKE_SOURCE_DIR}/component/amebad/sdk_headers/component/common")

add_library(amebad_ftl STATIC
    ftl.c
    flash_api.c
)

# ftl.c -> ftl.h / ftl_int.h (vendored beside it) + flash_api.h (mbed hal_ext);
# flash_api.h/.c -> pinmap.h (mbed hal) + device.h/objects.h/PinNames.h (mbed
# targets). These mbed HAL header dirs are ftl-specific, so they live here on the
# ftl target — not in amebad_sdk_includes (which carries only the broadly-needed
# SDK headers).
target_include_directories(amebad_ftl SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_base}/mbed/hal_ext"
    "${_base}/mbed/hal"
    "${_base}/mbed/targets/hal/rtl8721d")
target_link_libraries(amebad_ftl PUBLIC amebad_sdk_includes)

target_compile_options(amebad_ftl PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
