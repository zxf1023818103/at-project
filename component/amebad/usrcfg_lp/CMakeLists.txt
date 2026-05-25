# amebad_usrcfg_lp — KM0 LP-only board configuration (flash config,
# interface config, pinmap, sleep config). 5 .c. KM0 toolchain only.
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/usrcfg/
# (filenames with "lp" infix).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(amebad_usrcfg_lp STATIC ${_srcs})
target_link_libraries(amebad_usrcfg_lp PUBLIC amebad_sdk_includes)

target_compile_options(amebad_usrcfg_lp PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
