# bflb_board_bl616cldk — BSP for BL616CLDK reference board.

set(BFLB_BOARD_BL616CLDK_SOURCES
    board_flash_psram.c
    board_gpio.c
    board.c
    fw_header.c
)
if(CONFIG_RF)
    list(APPEND BFLB_BOARD_BL616CLDK_SOURCES
        board_rf.c
        wireless_regdb.c
        wireless_spur.c)
endif()

add_library(bflb_board_bl616cldk STATIC ${BFLB_BOARD_BL616CLDK_SOURCES})
target_include_directories(bflb_board_bl616cldk PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_board_bl616cldk PRIVATE sdk_intf_lib)
    target_link_options(sdk_intf_lib INTERFACE -ufw_header)
endif()

if(NOT CONFIG_CUSTOM_LINKSCRIPT)
    message(STATUS "LINK IN bl616cl_common.ld.in")
    set_property(GLOBAL PROPERTY LINKER_SCRIPT_IN ${CMAKE_CURRENT_LIST_DIR}/bl616cl_common.ld.in)
endif()

if(CONFIG_CUSTOMIZE_BOARD_DIR)
    set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CONFIG_CUSTOMIZE_BOARD_DIR})
else()
    set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
