# bflb_board_bl618dgdk — BSP for BL618DGDK reference board.

set(BFLB_BOARD_BL618DGDK_SOURCES
    board_flash_psram.c
    board_gpio.c
    board.c
    fw_header.c
)
if(CONFIG_RF)
    list(APPEND BFLB_BOARD_BL618DGDK_SOURCES board_rf.c)
endif()

add_library(bflb_board_bl618dgdk STATIC ${BFLB_BOARD_BL618DGDK_SOURCES})
target_include_directories(bflb_board_bl618dgdk PUBLIC ${CMAKE_CURRENT_LIST_DIR})

if(TARGET sdk_intf_lib)
    target_link_libraries(bflb_board_bl618dgdk PRIVATE sdk_intf_lib)
    target_link_options(sdk_intf_lib INTERFACE -ufw_header)
endif()

if(NOT CONFIG_CUSTOM_LINKSCRIPT)
    if("${CPU_ID}" STREQUAL "lp")
        if("${CPU_MODEL}" STREQUAL "b0")
            message(STATUS "LINK IN bl618dg_ram_lp_b0.ld")
            set_property(GLOBAL PROPERTY LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/bl618dg_ram_lp_b0.ld)
            if(TARGET sdk_intf_lib)
                target_link_options(sdk_intf_lib INTERFACE -Wl,--no-relax)
            endif()
        endif()
    else()
        message(STATUS "LINK IN bl618dg_common.ld")
        set_property(GLOBAL PROPERTY LINKER_SCRIPT_IN ${CMAKE_CURRENT_LIST_DIR}/bl618dg_common.ld.in)
    endif()
endif()

if(CONFIG_CUSTOMIZE_BOARD_DIR)
    set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CONFIG_CUSTOMIZE_BOARD_DIR})
else()
    set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()
set_property(GLOBAL PROPERTY FW_TOOL_CUSTOM_BOARD_CONFIG config)
