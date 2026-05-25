# amebad_ram_common — shared HAL drivers (UART/I2C/SPI/ADC/GPIO/
# Flash/RTC/IPC/WDG/IR/...), 20 .c. **KM0 only**.
#
# Naming caveat: SDK puts these under fwlib/ram_common/ as "common to
# both cores by source", but KM4 build never compiles them — KM4 boot
# / image2 stages pull these HAL functions from on-chip ROM via the
# rom_map_hp/ symbol map (Realtek's ROM-callable A-cut HAL).
#
# Confirmation: KM4 SDK has no make/ subdir for ram_common; rtl8721d_qdec.c
# hard-codes APBPeriph_QDEC0 which is only defined in rtl8721dlp_rcc.h
# (the LP-side RCC header).
#
# Project-local copy of platform/ameba-rtos-d/.../fwlib/ram_common/.
# Resync when the submodule pointer moves:
#   diff -r platform/ameba-rtos-d/component/soc/realtek/amebad/fwlib/ram_common/ \
#           component/amebad/ram_common/src/

if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km0")
    return()
endif()

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
