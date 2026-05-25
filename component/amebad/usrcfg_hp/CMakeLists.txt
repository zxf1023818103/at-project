# amebad_usrcfg_hp — KM4 HP-only board configuration (TrustZone-M
# config, interface config). 2 .c. KM4 toolchain only.
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/usrcfg/
# (filenames with "hp" infix).

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(amebad_usrcfg_hp STATIC ${_srcs})
target_link_libraries(amebad_usrcfg_hp PUBLIC amebad_sdk_includes)

target_compile_options(amebad_usrcfg_hp PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
