cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../../cmake/tl3238-toolchain.cmake"
    CACHE FILEPATH "Cross compiler")
project(tl3238_freertos LANGUAGES C ASM)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
get_filename_component(AT_ROOT "../.." ABSOLUTE)
set(TL3238_NIMBLE_ROOT "${AT_ROOT}/component/nimble_config/upstream-1.10.0"
    CACHE PATH "Local NimBLE 1.10 component" FORCE)
option(TL3238_ENABLE_BLE "NimBLE host and mcumgr over the TL3238 controller" ON)
option(TL3238_ENABLE_CS "Channel Sounding with the custom V4048 TL323X SDK" ON)
option(TL3238_ENABLE_OTA "mcumgr native Telink A/B image management" ON)
option(TL3238_ENABLE_COC "Flash Studio iOS LE CoC throughput service" ON)
set(TL3238_OTA_BUILD_NUMBER "1" CACHE STRING "OTA image build number (version 0.1.0+build)")
if(NOT TL3238_OTA_BUILD_NUMBER MATCHES "^(0|[1-9][0-9]*)$" OR TL3238_OTA_BUILD_NUMBER GREATER 4294967295)
    message(FATAL_ERROR "TL3238_OTA_BUILD_NUMBER must be an unsigned 32-bit decimal integer without leading zeroes")
endif()
if(NOT TL3238_ENABLE_BLE)
    set(TL3238_ENABLE_CS OFF)
    set(TL3238_ENABLE_OTA OFF)
    set(TL3238_ENABLE_COC OFF)
endif()

add_subdirectory("${AT_ROOT}/component/telink_sdk_config" telink_sdk)

# Select the existing kernel and CLI components. Other platform configurations
# (Ameba/Bouffalo, networking and filesystems) are not needed by this demo.
add_library(freertos_config INTERFACE)
target_include_directories(freertos_config INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(freertos_config INTERFACE telink_sdk_headers)
add_subdirectory("${AT_ROOT}/component/telink_sdk_config/freertos_port" freertos_port)
set(FREERTOS_PORT A_CUSTOM_PORT CACHE STRING "TL3238 Andes D25F port" FORCE)
set(FREERTOS_HEAP 4 CACHE STRING "Upstream coalescing FreeRTOS heap" FORCE)
add_subdirectory("${AT_ROOT}/component/freertos-kernel_config/freertos-kernel" freertos_kernel)
add_library(FreeRTOS ALIAS freertos_kernel)
add_subdirectory("${AT_ROOT}/component/freertos-cli_config" freertos_cli)

add_executable(tl3238_freertos main.c console.c cli_commands.c)
set_target_properties(tl3238_freertos PROPERTIES SUFFIX ".elf")
target_compile_options(tl3238_freertos PRIVATE -Wall -Wextra -Werror)
target_include_directories(tl3238_freertos PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tl3238_freertos PRIVATE telink_sdk freertos-cli_config)
if(TL3238_ENABLE_BLE)
    add_subdirectory("${AT_ROOT}/component/telink_sdk_config/nimble_port" nimble_port)
    target_sources(tl3238_freertos PRIVATE ble_app.c)
    if(TL3238_ENABLE_CS)
        target_sources(tl3238_freertos PRIVATE cs_app.c)
    endif()
    target_link_libraries(tl3238_freertos PRIVATE tl3238_nimble)
endif()
target_link_options(tl3238_freertos PRIVATE
    "-T${TL3238_LINKER_SCRIPT}"
    "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/tl3238_freertos.map")
set_property(TARGET tl3238_freertos APPEND PROPERTY LINK_DEPENDS "${TL3238_LINKER_SCRIPT}")
add_custom_command(TARGET tl3238_freertos POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:tl3238_freertos>"
        "${CMAKE_CURRENT_BINARY_DIR}/tl3238_freertos.bin"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:tl3238_freertos>"
    VERBATIM)
