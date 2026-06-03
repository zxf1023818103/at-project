# amebad_ftl — Flash Translation Layer (ftl_init et al.) for the KM4 image2, plus
# its mbed flash_api backend (flash_erase_sector / flash_*_word). main.c's
# app_ftl_init() calls into ftl, which sits on flash_api. ftl.c + flash_api.c
# (and ftl's headers ftl.h / ftl_int.h) are vendored here; the mbed HAL *headers*
# still come from the SDK. No heavy (wifi/lwip) deps. KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_base "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/common")

add_library(amebad_ftl STATIC
    ftl.c
    flash_api.c
)

# ftl.c -> ftl.h / ftl_int.h (vendored beside it) + flash_api.h (mbed hal_ext);
# flash_api.c -> pinmap.h (mbed hal) + objects.h/PinNames.h (mbed targets,
# already in sdk_includes' mbed/targets/hal/rtl8721d).
target_include_directories(amebad_ftl SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${_base}/mbed/hal_ext"
    "${_base}/mbed/hal")
target_link_libraries(amebad_ftl PUBLIC amebad_sdk_includes)

target_compile_options(amebad_ftl PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
