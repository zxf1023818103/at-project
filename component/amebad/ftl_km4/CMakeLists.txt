# amebad_ftl_km4 — Flash Translation Layer (ftl_init et al.) for the KM4
# image2, plus its mbed flash_api backend (flash_erase_sector / flash_*_word).
# main.c's app_ftl_init() calls into ftl, which sits on flash_api. ftl.c +
# flash_api.c (and ftl's private ftl_int.h) are vendored into src/; the mbed
# HAL *headers* still come from the SDK. No heavy (wifi/lwip) deps. KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

set(_src  "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(_base "${CMAKE_SOURCE_DIR}/platform/ameba-rtos-d/component/common")

foreach(_p "${_src}/ftl.c" "${_src}/flash_api.c")
    if(NOT EXISTS "${_p}")
        message(FATAL_ERROR "amebad_ftl_km4: missing '${_p}'")
    endif()
endforeach()

add_library(amebad_ftl_km4 STATIC "${_src}/ftl.c" "${_src}/flash_api.c")

# ftl.c -> ftl_int.h (vendored beside it in src/) + flash_api.h (mbed hal_ext);
# flash_api.c -> pinmap.h (mbed hal) + objects.h/PinNames.h (mbed targets,
# already in sdk_includes' mbed/targets/hal/rtl8721d).
target_include_directories(amebad_ftl_km4 SYSTEM PUBLIC
    "${_src}"
    "${_base}/mbed/hal_ext"
    "${_base}/mbed/hal")
target_link_libraries(amebad_ftl_km4 PUBLIC amebad_sdk_includes)

target_compile_options(amebad_ftl_km4 PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
