# bflb_board_bl616dk — BSP for BL616DK reference board.

set(BFLB_BOARD_BL616DK_SOURCES
    board_flash_psram.c
    board_gpio.c
    board.c
    fw_header.c
)
if(CONFIG_RF)
    list(APPEND BFLB_BOARD_BL616DK_SOURCES
        board_rf.c
        wireless_regdb.c
        wireless_spur.c)
endif()

add_library(bflb_board_bl616dk STATIC ${BFLB_BOARD_BL616DK_SOURCES})
target_include_directories(bflb_board_bl616dk PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_board_bl616dk PRIVATE sdk_intf_lib)
endif()

# Linker script (template processed by SDK) — register globally for SDK link step.
if(NOT CONFIG_CUSTOM_LINKSCRIPT)
    message(STATUS "LINK IN bl616_common.ld.in")
    set_property(GLOBAL PROPERTY LINKER_SCRIPT_IN ${CMAKE_CURRENT_LIST_DIR}/bl616_common.ld.in)
endif()

# Link options that need to propagate to the final SDK link
if(TARGET sdk_intf_lib)
    target_link_options(sdk_intf_lib INTERFACE -ufw_header)
endif()

# fw tools board directory select
if(CONFIG_CUSTOMIZE_BOARD_DIR)
    set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CONFIG_CUSTOMIZE_BOARD_DIR})
else()
    set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
