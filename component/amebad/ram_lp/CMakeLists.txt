# amebad_ram_lp — KM0 LP-only HAL (clk, flashclk, pinmap, km4 startup
# control, system, app_start, simulation, cpft). 9 .c. KM0 toolchain only.
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/ram_lp/.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(amebad_ram_lp STATIC ${_srcs})
target_link_libraries(amebad_ram_lp PUBLIC amebad_sdk_includes)

target_compile_options(amebad_ram_lp PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
