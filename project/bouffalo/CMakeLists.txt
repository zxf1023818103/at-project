cmake_minimum_required(VERSION 3.20)
# ASM enabled at top level (the deleted root CMakeLists used to via project(at_project C CXX ASM)):
# component's universal configs (freertos-kernel etc.) + bouffalo_sdk have .S sources, and a late
# enable_language(ASM) in a subdir scope leaves CMAKE_ASM_COMPILE_OBJECT unset for them.
project(at C CXX ASM)

# CMP0118 NEW (CMake 3.20+) makes the GENERATED source-file property
# visible across directory boundaries. Required because src/CMakeLists.txt
# (pulled in via add_subdirectory below) registers a custom command for
# the ragel-generated AtCommandParser.rl.cpp; without this policy,
# bflb_add_executable(at ${AT_USER_SOURCES}) in this directory wouldn't
# know that the .cpp is a build product and would error
# "Cannot find source file".
cmake_policy(SET CMP0118 NEW)

include(proj.conf)

# The repo-root CMakeLists was deleted (each image is its own project now), so this entry adds the
# shared subtrees directly (like project/ameba): component self-adds the universal *_config + the
# BOUFFALO branch's bouffalo_sdk; src exports AT_USER_SOURCES. AT_ROOT anchors the repo root.
get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(AT_ROOT "${AT_ROOT}" CACHE INTERNAL "at-project repo root")
add_compile_options("-fmacro-prefix-map=${AT_ROOT}/src/=")   # __FILE__ basename (moved from the deleted root)
add_subdirectory(${AT_ROOT}/component ${CMAKE_BINARY_DIR}/component)   # universal configs (self) + bouffalo_sdk
add_subdirectory(${AT_ROOT}/src       ${CMAKE_BINARY_DIR}/src)        # exports AT_USER_SOURCES

bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(at PRIVATE bouffalo_sdk)

# Shared `ota_http <url>` command (component/ota_http_config): compiled into the app (per-image macros),
# over coreHTTP + llhttp + the shared TLS transport, streaming into the BL602 OTA slot writer
# (bouffalo_sdk/ota_flash_port). main() registers it from app_console_register_platform(). bl602 only:
# its https path needs the mbedTLS TLS transport (TLS_FreeRTOS_*), which - like aws_iot - is only
# linked on bl602 (bl616/cl leave it undefined). ota_flash_port itself (no TLS) rides all chips unused.
if(BOUFFALO_CHIP STREQUAL "bl602")
    target_sources(at PRIVATE ${OTA_HTTP_SOURCES})
    set_source_files_properties("${AT_ROOT}/component/ota_http_config/urlparse.c" PROPERTIES COMPILE_OPTIONS "-w")
    target_link_libraries(at PRIVATE ota_http_config corehttp_config)
    target_include_directories(at PRIVATE
        "${AT_ROOT}/component/corehttp_config"
        "${AT_ROOT}/component/corehttp_config/corehttp/source/include"
        "${AT_ROOT}/component/corehttp_config/corehttp/source/interface"
        "${AT_ROOT}/component/corehttp_config/corehttp/source/dependency/3rdparty/llhttp/include"
        "${AT_ROOT}/component/network_transport")   # transport_tls.h
endif()

# NimBLE host: main.cpp starts the shared nimble_start() from a boot task under this macro.
if(CONFIG_NIMBLE_HOST)
    target_compile_definitions(at PRIVATE CONFIG_NIMBLE_HOST)
endif()
