# bflb_board_bl702dk — BSP for BL702DK reference board.

add_library(bflb_board_bl702dk STATIC board.c fw_header.c)
target_include_directories(bflb_board_bl702dk PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(CONFIG_BLUETOOTH)
    target_compile_definitions(bflb_board_bl702dk PUBLIC CFG_BLUETOOTH_ENABLED)
endif()
if(CONFIG_LMAC154)
    target_compile_definitions(bflb_board_bl702dk PUBLIC CFG_M154_ENABLED)
endif()
if(CONFIG_PSRAM)
    target_compile_definitions(bflb_board_bl702dk PUBLIC CONFIG_PSRAM)
endif()

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_board_bl702dk PRIVATE sdk_intf_lib)
    target_link_options(sdk_intf_lib INTERFACE -ufw_header)
endif()

if(CONFIG_PM)
    set_property(GLOBAL PROPERTY LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/bl702_flash_lp.ld)
else()
    set_property(GLOBAL PROPERTY LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/bl702_flash.ld)
endif()

set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
