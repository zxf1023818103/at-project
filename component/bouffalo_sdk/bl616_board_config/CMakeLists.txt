# BL616 board configuration (extracted from bouffalo_sdk/bsp/board/bl616dk).
#
# SDK -> native CMake mapping:
#   sdk_add_include_directories(.)         -> target_include_directories(... PUBLIC .)
#   target_sources(app PRIVATE <f>)        -> target_sources(<this> PRIVATE <f>)
#   sdk_set_linker_script_macro(<ld.in>)   -> add_custom_command running cpp on
#                                             the .ld.in template; the resulting
#                                             linker.ld is passed via -T.
#   sdk_add_link_options(-ufw_header)      -> target_link_options(... INTERFACE -ufw_header)
#
# CONFIG_RF (originally a Kconfig boolean) -> BL616_BOARD_CONFIG_RF option.
# Removed dead branches: CONFIG_CUSTOM_LINKSCRIPT (consumers can use their own
# linker script by not linking against this target) and the FW_TOOL_CUSTOM_*
# global properties (consumed only by bflb_flash.cmake, which we no longer use).

option(BL616_BOARD_CONFIG_RF "Compile RF / wireless regdb sources" OFF)

add_library(bl616_board_config OBJECT
    board.c
    board_flash_psram.c
    board_gpio.c
    fw_header.c
)

if(BL616_BOARD_CONFIG_RF)
    target_sources(bl616_board_config PRIVATE
        board_rf.c
        wireless_regdb.c
        wireless_spur.c
    )
endif()

target_include_directories(bl616_board_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# board.c #include lhal headers; mirror bl602_board_config's UART-baud
# default (upstream lives in defconfig, our extracted tree treats it as
# an opt-in here).
if(NOT DEFINED CONFIG_CONSOLE_UART_BAUDRATE)
    set(CONFIG_CONSOLE_UART_BAUDRATE 2000000)
endif()
target_compile_definitions(bl616_board_config PUBLIC
    "CONFIG_CONSOLE_UART_BAUDRATE=${CONFIG_CONSOLE_UART_BAUDRATE}"
)

# LP_FW=ON (CONFIG_LPAPP): board.c's board_recovery() (system/peripheral clock + console re-init after
# a PDS wake) and its board.h declaration are #ifdef LP_APP. PUBLIC so lp_cli.c (which calls it) sees it.
if(CONFIG_LPAPP)
    target_compile_definitions(bl616_board_config PUBLIC LP_APP=1)
endif()

# board.c / board_gpio.c / board_flash_psram.c #include lhal headers
# (bflb_uart.h, bflb_gpio.h, bflb_clock.h, …). PUBLIC so consumers that
# pull in this OBJECT lib also see lhal.
target_link_libraries(bl616_board_config PUBLIC bflb_lhal tlsf_config)

# Run cpp on bl616_common.ld.in to expand the #ifdef CONFIG_* defaults baked
# into the template. Without an autoconf.h, the template's #ifndef-guarded
# defaults take effect (4 MB flash XIP, 64 KB WRAM, no PSRAM size override).
set(_ld_in "${CMAKE_CURRENT_LIST_DIR}/bl616_common.ld.in")
set(_ld    "${CMAKE_CURRENT_BINARY_DIR}/linker.ld")

# CONFIG_WIFI6 changes WRAM_LENGTH default 64K -> 160K in the template
# (and ram_wifi sits inside WRAM). CONFIG_LPAPP / CONFIG_PSRAM also have
# `#if defined` branches. Propagate the ones the project sets in proj.conf
# as -D flags to the cpp pass; the template's #ifndef-guarded defaults
# cover the rest.
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
    COMMENT "Expanding ${CMAKE_CURRENT_LIST_DIR}/bl616_common.ld.in -> linker.ld (defs: ${_ld_cpp_defs})"
    VERBATIM
)
add_custom_target(bl616_board_config_linker_script DEPENDS "${_ld}")
add_dependencies(bl616_board_config bl616_board_config_linker_script)

target_link_options(bl616_board_config INTERFACE
    "-T${_ld}"
    # The expanded linker.ld retains `INCLUDE macsw_cache_affinity.ld.in` (ld
    # directive, not a cpp one). Add the source dir to ld's search path so
    # that include resolves.
    "-L${CMAKE_CURRENT_LIST_DIR}"
    "-ufw_header"
    # The prebuilt fhost SDK .a's (libwpa_supplicant/libmacsw_os_adapter) bundle their own copies of
    # common symbols that clash with ours (crc32 vs zlib; vApplicationStackOverflowHook vs
    # freertos_config). Let the linker take the first definition rather than erroring.
    "-Wl,--allow-multiple-definition"
)

set_target_properties(bl616_board_config PROPERTIES
    INTERFACE_LINK_DEPENDS "${_ld};${_ld_in};${CMAKE_CURRENT_LIST_DIR}/macsw_cache_affinity.ld.in"
)
