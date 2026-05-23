# bflb_board_bl702ldk — BSP for BL702LDK reference board.

add_library(bflb_board_bl702ldk STATIC board.c fw_header.c)
target_include_directories(bflb_board_bl702ldk PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(CONFIG_BLUETOOTH)
    target_compile_definitions(bflb_board_bl702ldk PUBLIC CFG_BLUETOOTH_ENABLED)
endif()
if(CONFIG_LMAC154)
    target_compile_definitions(bflb_board_bl702ldk PUBLIC CFG_M154_ENABLED)
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_board_bl702ldk PRIVATE sdk_intf_lib)
    target_link_options(sdk_intf_lib INTERFACE -ufw_header)
endif()

set_property(GLOBAL PROPERTY LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/bl702l_flash.ld)

set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
