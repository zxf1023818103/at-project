# amebad_ram_hp — KM4 HP-only HAL (audio, I2S, LCDC, SD, SDIO host, SSI,
# OTA, startup, system, app_start, simulation, cpft, clk). 14 .c. KM4
# toolchain only.
#
# Currently no-op: KM4 toolchain (cmake/amebad-km4.cmake) doesn't exist
# yet. Source files are mirrored so this subtree is ready when KM4 lands.
# At that point amebad_sdk_includes will also need a km4 branch
# (inc_hp + FreeRTOS RTL8721D_HP).
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/ram_hp/.

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(amebad_ram_hp STATIC ${_srcs})
target_link_libraries(amebad_ram_hp PUBLIC amebad_sdk_includes)

target_compile_options(amebad_ram_hp PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
