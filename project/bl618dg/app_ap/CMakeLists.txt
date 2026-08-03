# ---- standalone CMake project (libs in component/, this image here) ----
# Built via:
#   cmake -S project/bl618dg/app_ap -B build/bl618dg_ap \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/bl618dg-ap-toolchain.cmake
# (the toolchain FORCE-sets BOUFFALO_CHIP=bl618dg + BOUFFALO_CPU_ID=ap).
#
# Mirrors project/amebad/app_km4: the AP core is BL618DG's application core.
# project/bl618dg/Makefile appends the NP image into this one's .bin (the SDK's
# CONFIG_DUALCORE_NP_IMAGE step) before packing the flashable whole image.
cmake_minimum_required(VERSION 3.20)
project(app_ap C CXX ASM)

get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(AT_ROOT "${AT_ROOT}" CACHE INTERNAL "at-project repo root")

if(NOT "${BOUFFALO_CPU_ID}" STREQUAL "ap")
    message(FATAL_ERROR
        "app_ap: configure with cmake/bl618dg-ap-toolchain.cmake "
        "(BOUFFALO_CPU_ID='${BOUFFALO_CPU_ID}')")
endif()

# ---- WiFi (wifi6 / fhost) ----
# Follows the SDK's solution_dualcore: WiFi runs on the AP core only (its wifi_np
# defconfig has CONFIG_WIFI_ENABLE commented out). Sizing mirrors the bl616 /
# bl616cl block in project/bouffalo/proj.conf.
set(CONFIG_WIFI6 y)
set(CONFIG_RF y)
set(CONFIG_VIF_MAX 2)
set(CONFIG_STA_MAX 4)
set(CONFIG_MAC_TXQ_DEPTH 16)
set(CONFIG_MAC_RXQ_DEPTH 12)
# The RF/PHY calibration data lives in the board's factory params, so the board
# layer needs its RF sources compiled in.
set(BL618DG_BOARD_CONFIG_RF ON)
# BLE controller: src/Common.h pulls btble_lib_api.h on the wifi6 Bouffalo chips,
# and the app shares the WiFi/BLE coexist path with bl616. Same profile as bl616.
set(CONFIG_BLUETOOTH y)
set(CONFIG_BTBLECONTROLLER_LIB ble1m10s1bredr0)

# NimBLE host on the on-chip controller: main.cpp starts the shared nimble_start()
# from a boot task under AT_PROJECT_NIMBLE. component/bouffalo_sdk already routes
# bl618dg to btblecontroller + nimble_port; this is the project-level switch, driven
# by project/bl618dg/Makefile's NIMBLE= flag. Same contract as project/bouffalo.
option(AT_ENABLE_NIMBLE "Build the NimBLE host" OFF)

# AWS IoT telemetry client (aws_iot_config): -DAT_ENABLE_AWS=ON at configure time,
# driven by project/bl618dg/Makefile's AWS= flag. Same contract as project/bouffalo.
option(AT_ENABLE_AWS "Build the AWS IoT telemetry client" OFF)

# AWSOTA: the coreMQTT-Agent + FreeRTOS-OTA variant (aws_iot_demo.c) instead of the
# lean telemetry-only client. Implies AT_ENABLE_AWS.
option(AT_ENABLE_AWS_OTA "Build the AWS IoT OTA agent" OFF)
if(AT_ENABLE_AWS_OTA)
    set(AT_ENABLE_AWS ON)
endif()

# PSRAM: register on-die PSRAM as a heap (MM_HEAP_PSRAM_0; pvPortMalloc uses
# MM_FLAG_HEAP_ANY so it falls back there once OCRAM/WRAM are full).
# ★ The bl618dgdk board on this bench has NO PSRAM: with PSRAM=ON it stops at boot with
#   "This chip has no psram, please disable CONFIG_PSRAM" (board_psram_x8_init detects
#   it for real). Keep OFF unless the die actually carries PSRAM.
option(AT_ENABLE_PSRAM "Register on-die PSRAM as heap" OFF)
if(AT_ENABLE_PSRAM)
    set(CONFIG_PSRAM y)
endif()

add_compile_options("-fmacro-prefix-map=${AT_ROOT}/src/=")   # __FILE__ basename, same as project/bouffalo
add_subdirectory(${AT_ROOT}/component ${CMAKE_BINARY_DIR}/component)
add_subdirectory(${AT_ROOT}/src       ${CMAKE_BINARY_DIR}/src)   # exports AT_USER_SOURCES

# The AP core runs the shared application — src/main.cpp is the single main()
# across every platform (it carries the WiFi bring-up sequence: wifi firmware
# task -> fhost_init -> wifi_mgmr -> wifi_service_start). The NP core keeps its
# own tiny main.c.
bflb_add_executable(app_ap BOARD bl618dgdk ${AT_USER_SOURCES})
target_link_libraries(app_ap PRIVATE bouffalo_sdk)

# NimBLE: src/main.cpp spawns nimble_start() under this macro. The host lib itself is
# pulled in by component/bouffalo_sdk (nimble_port) once AT_ENABLE_NIMBLE is set.
if(AT_ENABLE_NIMBLE)
    target_compile_definitions(app_ap PRIVATE AT_PROJECT_NIMBLE)
endif()

# `ota_http <url>` rides the same TLS transport as the AWS stack, so it is linked
# together with it — main.cpp gates ota_http_register() on the matching
# AT_PROJECT_AWS. Mirrors the identical block in project/bouffalo/CMakeLists.txt.
if(AT_ENABLE_AWS)
    target_sources(app_ap PRIVATE ${OTA_HTTP_SOURCES})
    # basename-only __FILE__ in the rxi log (feedback_log_basename_prefix_map); -w: vendored urlparse.
    set_source_files_properties("${AT_ROOT}/component/ota_http_config/ota_http.c" PROPERTIES
        COMPILE_OPTIONS "-fmacro-prefix-map=${AT_ROOT}/component/ota_http_config/=")
    set_source_files_properties("${AT_ROOT}/component/ota_http_config/urlparse.c" PROPERTIES
        COMPILE_OPTIONS "-w;-fmacro-prefix-map=${AT_ROOT}/component/ota_http_config/=")
    target_link_libraries(app_ap PRIVATE ota_http_config corehttp_config network_transport_headers)
endif()
