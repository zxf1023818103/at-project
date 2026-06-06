# amebad_ftl — Flash Translation Layer (ftl_init et al.) for the KM4 image2. main.c's
# app_ftl_init() calls into ftl, which sits on the mbed flash_api backend
# (flash_erase_sector / flash_*_word). ftl.c + its headers (ftl.h / ftl_int.h) are
# vendored here; flash_api.c and the mbed HAL headers now come from the
# component/amebad/mbed component (amebad_mbed), which ftl links. KM4 only.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_ftl STATIC
    ftl.c
)

# ftl.c -> ftl.h / ftl_int.h (vendored beside it) + flash_api.h. flash_api.c and
# the mbed HAL headers (flash_api.h / pinmap.h / device.h / objects.h / PinNames.h)
# come via amebad_mbed; amebad_sdk_includes supplies basic_types / fwlib.
target_include_directories(amebad_ftl SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(amebad_ftl PUBLIC amebad_mbed amebad_sdk_includes)

target_compile_options(amebad_ftl PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
