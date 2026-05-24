# bouffalo_sdk — umbrella INTERFACE library that links all chip-relevant
# Bouffalo components in one shot. Consumers do:
#
#   target_link_libraries(<exe> PRIVATE bouffalo_sdk)
#
# and pick up the low-level HAL, board init sources, linker script, and
# the -ufw_header link option transitively.

if(NOT BOUFFALO_CHIP)
    message(FATAL_ERROR "bouffalo_sdk: BOUFFALO_CHIP not set (use a chip toolchain)")
endif()

# FreeRTOS-Kernel (component/freertos-kernel, the bouffalo-port branch on
# the zxf1023818103 fork). Configure it BEFORE add_subdirectory so the
# kernel's CMakeLists can read FREERTOS_PORT + freertos_config.
if(NOT TARGET freertos_config)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        set(_mtime_base    "0x0200BFF8UL")
        set(_mtimecmp_base "0x02004000UL")
    else()
        # bl616, bl616cl
        set(_mtime_base    "0xE000BFF8UL")
        set(_mtimecmp_base "0xE0004000UL")
    endif()
    add_library(freertos_config INTERFACE)
    target_include_directories(freertos_config SYSTEM INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/bflb_freertos_port/include"
    )
    target_compile_definitions(freertos_config INTERFACE
        "configMTIME_BASE_ADDRESS=${_mtime_base}"
        "configMTIMECMP_BASE_ADDRESS=${_mtimecmp_base}"
        portasmHANDLE_INTERRUPT=interrupt_entry
        default_interrupt_handler=freertos_risc_v_trap_handler
    )
endif()
set(FREERTOS_PORT GCC_RISC_V CACHE STRING "FreeRTOS port name" FORCE)
if(NOT TARGET freertos_kernel)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../freertos-kernel"
        "${CMAKE_BINARY_DIR}/component/freertos-kernel"
    )
endif()

# bflb_autoconf must come first: its INTERFACE forces -include autoconf.h
# into every TU that links it (transitively, via bflb_lhal below).
add_subdirectory(bflb_autoconf)
add_subdirectory(bflb_freertos_port)
add_subdirectory(bflb_sdk_version)
add_subdirectory(bflb_sysinit)
# std must be added before lhal — lhal links against ${BOUFFALO_CHIP}_std.
add_subdirectory(${BOUFFALO_CHIP}_std)
add_subdirectory(bflb_lhal)
add_subdirectory(bflb_log)
add_subdirectory(bflb_mm)
add_subdirectory(bflb_block_pool)
add_subdirectory(bflb_timestamp)
add_subdirectory(bflb_partition)
add_subdirectory(bflb_mtd)
add_subdirectory(bflb_sys)
add_subdirectory(bflb_async_event)
add_subdirectory(bflb_power_mgmt)
add_subdirectory(${BOUFFALO_CHIP}_board_config)

# phyrf only matters when RF/Wi-Fi is enabled. rfparam (BL602-only in this
# fork — BL616 / BL616CL use a different RF parameter pipeline shipped inside
# their respective phyrf .a archives) needs phyrf's headers, so it gates on
# the same flag.
if(CONFIG_RF)
    add_subdirectory(${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        add_subdirectory(bl602_rfparam)
    endif()
endif()

add_library(bouffalo_sdk INTERFACE)
target_link_libraries(bouffalo_sdk INTERFACE
    bflb_autoconf
    bflb_sdk_version
    bflb_freertos_port
    ${BOUFFALO_CHIP}_std
    bflb_lhal
    bflb_mm
    bflb_log
    bflb_block_pool
    bflb_timestamp
    bflb_partition
    bflb_mtd
    bflb_sys
    bflb_async_event
    bflb_power_mgmt
    ${BOUFFALO_CHIP}_board_config
    # bflb_sysinit comes last so its archive is scanned AFTER bl<chip>_std,
    # which is the only thing that references sysinit_run_all.
    bflb_sysinit
)

# OBJECT libraries don't propagate their object files transitively through an
# INTERFACE intermediary — CMake only auto-extracts $<TARGET_OBJECTS:...> when
# the OBJECT lib is linked directly to a real target. Stitching them in
# explicitly here so symbols kept alive only by linker-script KEEP() rules
# (.fw_header, .version.sdk) actually reach the final ELF.
target_sources(bouffalo_sdk INTERFACE
    $<TARGET_OBJECTS:bflb_sdk_version>
    $<TARGET_OBJECTS:${BOUFFALO_CHIP}_board_config>
)
if(CONFIG_RF)
    target_link_libraries(bouffalo_sdk INTERFACE ${BOUFFALO_CHIP}_phyrf)
    if("${BOUFFALO_CHIP}" STREQUAL "bl602")
        target_link_libraries(bouffalo_sdk INTERFACE bl602_rfparam)
    endif()
endif()
