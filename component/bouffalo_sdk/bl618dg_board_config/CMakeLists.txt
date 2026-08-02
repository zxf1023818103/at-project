# BL618DG board configuration (extracted from bouffalo_sdk/bsp/board/bl618dgdk).
#
# Structure follows bl616cl_board_config; see bl616_board_config/CMakeLists.txt
# for the SDK -> native CMake mapping rationale. Two BL618DG specifics:
#
#  1. Dual-core. The linker script bl618dg_common.ld.in carves one physical RAM
#     into an AP half and an NP half, selected by CPU_AP / CPU_NP and sized by
#     CONFIG_RAM_FOR_AP_SIZE / CONFIG_WRAM_FOR_AP_SIZE. Both cores expand the
#     SAME .ld.in — they must therefore agree on the *_FOR_AP_SIZE values, so
#     those live here (not per-core) and default to the SDK dualcore example's.
#  2. B0 silicon. CPU_MODEL_B0 selects the non-A0 arms of the .ld.in (different
#     XIP/PSRAM/RAM base addresses). The A-series (CPU_MODEL_A0) path is not
#     supported here — this project targets the mass-production B0 part.
#
# Unlike bl616cldk this board ships no wireless_regdb.c / wireless_spur.c.

if(NOT BOUFFALO_CPU_ID)
    message(FATAL_ERROR
        "bl618dg_board_config: BOUFFALO_CPU_ID is not set. Configure with "
        "cmake/bl618dg-ap-toolchain.cmake or cmake/bl618dg-np-toolchain.cmake.")
endif()

option(BL618DG_BOARD_CONFIG_RF "Compile RF sources" OFF)

add_library(bl618dg_board_config OBJECT
    board.c
    board_flash_psram.c
    board_gpio.c
    fw_header.c
)

if(BL618DG_BOARD_CONFIG_RF)
    target_sources(bl618dg_board_config PRIVATE board_rf.c)
    # board_rf.c pulls the PHY/RF API headers (wl_api.h, phy.h) that ship with
    # the prebuilt phyrf lib, plus rfparam_adapter.h from the RF-parameter
    # loader. bl618dg_phyrf is INTERFACE, so that one is headers + the .a; the
    # umbrella links both too, but board_config compiles first.
    target_link_libraries(bl618dg_board_config PUBLIC bl618dg_phyrf rfparam)
endif()

target_include_directories(bl618dg_board_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(NOT DEFINED CONFIG_CONSOLE_UART_BAUDRATE)
    set(CONFIG_CONSOLE_UART_BAUDRATE 2000000)
endif()

# Split of the shared RAM/WRAM between the two cores. Defaults mirror the SDK's
# examples/bl618dg_dualcore/defconfig. Both cores must see identical values.
# AP gets the larger share: it runs the application, the WiFi stack and (AWS=ON)
# the TLS/MQTT client, while the NP image is minimal — measured 24 KB used out
# of the 248 KB the SDK default handed it, against 199 KB for an AP image that
# needs 109 KB before its heap. 0x48000 (288 KB) to the AP leaves the NP ~160 KB,
# still ~6x its actual usage.
if(NOT DEFINED CONFIG_RAM_FOR_AP_SIZE)
    set(CONFIG_RAM_FOR_AP_SIZE 0x48000)
endif()
if(NOT DEFINED CONFIG_WRAM_FOR_AP_SIZE)
    set(CONFIG_WRAM_FOR_AP_SIZE 0x32000)
endif()

if(BOUFFALO_CPU_ID STREQUAL "ap")
    set(_cpu_def CPU_AP)
else()
    set(_cpu_def CPU_NP)
endif()

target_compile_definitions(bl618dg_board_config PUBLIC
    "CONFIG_CONSOLE_UART_BAUDRATE=${CONFIG_CONSOLE_UART_BAUDRATE}"
    "CPU_MODEL_B0=1"
    "${_cpu_def}=1"
)

# board_init() -> boot_up_np() releases the NP core. Without CONFIG_BOOT_NP_ASYNC
# it points NP at the AP's own image ("both cores run the same code"); with it,
# NP is booted from a SEPARATE image appended after the AP one, located via the
# __dualcore_images__ linker symbol. This project ships distinct AP/NP images
# (project/bl618dg/app_{ap,np}), so the AP core must define it — same as the
# SDK's own dualcore example does in helloworld_ap.
if(BOUFFALO_CPU_ID STREQUAL "ap")
    target_compile_definitions(bl618dg_board_config PUBLIC CONFIG_BOOT_NP_ASYNC)
endif()

# board.c / board_gpio.c / board_flash_psram.c #include lhal headers
# (bflb_uart.h, bflb_gpio.h, bflb_clock.h, …). PUBLIC so consumers that
# pull in this OBJECT lib also see lhal.
target_link_libraries(bl618dg_board_config PUBLIC bflb_lhal tlsf_config)

set(_ld_in "${CMAKE_CURRENT_LIST_DIR}/bl618dg_common.ld.in")
set(_ld    "${CMAKE_CURRENT_BINARY_DIR}/linker.ld")

# The .ld.in is preprocessed, so every knob it reads must be passed as -D.
set(_ld_cpp_defs
    "-D${_cpu_def}=1"
    "-DCPU_MODEL_B0=1"
    "-DCONFIG_RAM_FOR_AP_SIZE=${CONFIG_RAM_FOR_AP_SIZE}"
    "-DCONFIG_WRAM_FOR_AP_SIZE=${CONFIG_WRAM_FOR_AP_SIZE}"
)
foreach(_flag IN ITEMS CONFIG_WIFI6 CONFIG_WLAN CONFIG_PSRAM CONFIG_LPAPP CONFIG_DVP_RASTER)
    if(${_flag})
        list(APPEND _ld_cpp_defs "-D${_flag}=1")
    endif()
endforeach()

add_custom_command(
    OUTPUT "${_ld}"
    COMMAND "${CMAKE_C_COMPILER}" -E -P -x assembler-with-cpp ${_ld_cpp_defs} "${_ld_in}" -o "${_ld}"
    DEPENDS "${_ld_in}"
    COMMENT "Expanding bl618dg_common.ld.in -> linker.ld (${BOUFFALO_CPU_ID}; defs: ${_ld_cpp_defs})"
    VERBATIM
)
add_custom_target(bl618dg_board_config_linker_script DEPENDS "${_ld}")
add_dependencies(bl618dg_board_config bl618dg_board_config_linker_script)

target_link_options(bl618dg_board_config INTERFACE
    "-T${_ld}"
    "-L${CMAKE_CURRENT_LIST_DIR}"
    "-ufw_header"
)

set_target_properties(bl618dg_board_config PROPERTIES
    INTERFACE_LINK_DEPENDS "${_ld};${_ld_in}"
)
