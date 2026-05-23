# bflb_board_bl602dk — BSP for BL602DK reference board.

add_library(bflb_board_bl602dk STATIC
    board.c
    board_gpio.c
    fw_header.c
)
target_include_directories(bflb_board_bl602dk PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_board_bl602dk PRIVATE sdk_intf_lib)
    target_link_options(sdk_intf_lib INTERFACE -ufw_header)
endif()

set_property(GLOBAL PROPERTY LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/bl602_flash.ld)

# fw tools needs to find board/config in the extracted location (original
# SDK relied on a fallback that pointed at platform/bouffalo_sdk/bsp/board/...).
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
