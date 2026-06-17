# BL616CL board configuration (extracted from bouffalo_sdk/bsp/board/bl616cldk).
#
# Structure identical to bl616_board_config; see its CMakeLists.txt for the
# SDK -> native CMake mapping rationale.

option(BL616CL_BOARD_CONFIG_RF "Compile RF / wireless regdb sources" OFF)

add_library(bl616cl_board_config OBJECT
    board.c
    board_flash_psram.c
    board_gpio.c
    fw_header.c
)

if(BL616CL_BOARD_CONFIG_RF)
    target_sources(bl616cl_board_config PRIVATE
        board_rf.c
        wireless_regdb.c
        wireless_spur.c
    )
endif()

target_include_directories(bl616cl_board_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(NOT DEFINED CONFIG_CONSOLE_UART_BAUDRATE)
    set(CONFIG_CONSOLE_UART_BAUDRATE 2000000)
endif()
target_compile_definitions(bl616cl_board_config PUBLIC
    "CONFIG_CONSOLE_UART_BAUDRATE=${CONFIG_CONSOLE_UART_BAUDRATE}"
)

# board.c / board_gpio.c / board_flash_psram.c #include lhal headers
# (bflb_uart.h, bflb_gpio.h, bflb_clock.h, …). PUBLIC so consumers that
# pull in this OBJECT lib also see lhal.
target_link_libraries(bl616cl_board_config PUBLIC bflb_lhal tlsf_config)

set(_ld_in "${CMAKE_CURRENT_LIST_DIR}/bl616cl_common.ld.in")
set(_ld    "${CMAKE_CURRENT_BINARY_DIR}/linker.ld")

set(_ld_cpp_defs "")
foreach(_flag IN ITEMS CONFIG_WIFI6 CONFIG_WLAN CONFIG_PSRAM CONFIG_LPAPP)
    if(${_flag})
        list(APPEND _ld_cpp_defs "-D${_flag}=1")
    endif()
endforeach()

add_custom_command(
    OUTPUT "${_ld}"
    COMMAND "${CMAKE_C_COMPILER}" -E -P -x assembler-with-cpp ${_ld_cpp_defs} "${_ld_in}" -o "${_ld}"
    DEPENDS "${_ld_in}"
    COMMENT "Expanding ${CMAKE_CURRENT_LIST_DIR}/bl616cl_common.ld.in -> linker.ld (defs: ${_ld_cpp_defs})"
    VERBATIM
)
add_custom_target(bl616cl_board_config_linker_script DEPENDS "${_ld}")
add_dependencies(bl616cl_board_config bl616cl_board_config_linker_script)

target_link_options(bl616cl_board_config INTERFACE
    "-T${_ld}"
    "-L${CMAKE_CURRENT_LIST_DIR}"
    "-ufw_header"
)

set_target_properties(bl616cl_board_config PROPERTIES
    INTERFACE_LINK_DEPENDS "${_ld};${_ld_in};${CMAKE_CURRENT_LIST_DIR}/macsw_cache_affinity.ld.in"
)
