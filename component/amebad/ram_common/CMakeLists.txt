# amebad_ram_common — chip-shared HAL drivers (UART/I2C/SPI/ADC/GPIO/
# Flash/RTC/IPC/WDG/IR/...), 20 .c. Built once with the active
# toolchain's -march; both KM0 and KM4 link it.
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/ram_common/.
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/ram_common/ \
#           component/amebad/ram_common/src/

file(GLOB _srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(amebad_ram_common STATIC ${_srcs})
target_link_libraries(amebad_ram_common PUBLIC amebad_sdk_includes)

# Vendor code — suppress -Wall/-Wextra noise we don't own.
target_compile_options(amebad_ram_common PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
)
