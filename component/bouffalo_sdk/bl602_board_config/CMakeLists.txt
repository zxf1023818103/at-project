# BL602 board configuration (extracted from bouffalo_sdk/bsp/board/bl602dk).
#
# SDK -> native CMake mapping:
#   sdk_add_include_directories(.)        -> target_include_directories(... PUBLIC .)
#   target_sources(app PRIVATE <f>)       -> target_sources(<this> PRIVATE <f>)
#   sdk_set_linker_script(<ld>)           -> target_link_options(... INTERFACE -T<ld>)
#   sdk_add_link_options(-ufw_header)     -> target_link_options(... INTERFACE -ufw_header)
#
# Exposed as an OBJECT library so every source ends up directly in the
# consumer's link line (matching the SDK's target_sources(app ...) semantics).
# Link options propagate via the INTERFACE.

add_library(bl602_board_config OBJECT
    board.c
    board_gpio.c
    fw_header.c
)

target_include_directories(bl602_board_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# board.c / board_gpio.c #include lhal headers (bflb_uart.h, bflb_gpio.h, …).
# PUBLIC so consumers that pull in this OBJECT lib also see lhal.
target_link_libraries(bl602_board_config PUBLIC bflb_lhal bflb_mm)

set(_ld "${CMAKE_CURRENT_LIST_DIR}/bl602_flash.ld")

target_link_options(bl602_board_config INTERFACE
    "-T${_ld}"
    "-L${CMAKE_CURRENT_LIST_DIR}"
    # fw_header is referenced only by the linker script; -u keeps it live.
    "-ufw_header"
)

set_target_properties(bl602_board_config PROPERTIES
    INTERFACE_LINK_DEPENDS "${_ld}"
)
