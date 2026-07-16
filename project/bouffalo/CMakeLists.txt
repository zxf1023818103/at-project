cmake_minimum_required(VERSION 3.20)
# ASM enabled at top level (the deleted root CMakeLists used to via project(at_project C CXX ASM)):
# component's universal configs (freertos-kernel etc.) + bouffalo_sdk have .S sources, and a late
# enable_language(ASM) in a subdir scope leaves CMAKE_ASM_COMPILE_OBJECT unset for them.
project(at C CXX ASM)

include(proj.conf)

# The repo-root CMakeLists was deleted (each image is its own project now), so this entry adds the
# shared subtrees directly (like project/ameba): component self-adds the universal *_config + the
# BOUFFALO branch's bouffalo_sdk; src exports AT_USER_SOURCES. AT_ROOT anchors the repo root.
get_filename_component(AT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(AT_ROOT "${AT_ROOT}" CACHE INTERNAL "at-project repo root")
add_compile_options("-fmacro-prefix-map=${AT_ROOT}/src/=")   # __FILE__ basename (moved from the deleted root)
add_subdirectory(${AT_ROOT}/component ${CMAKE_BINARY_DIR}/component)   # universal configs (self) + bouffalo_sdk
add_subdirectory(${AT_ROOT}/src       ${CMAKE_BINARY_DIR}/src)        # exports AT_USER_SOURCES

# Low-memory network profile for the connect-only images (AWS / MINIMAL), same gate as amebad's
# app_km4: AT_NET_LOW_MEM shrinks the FreeRTOS-Plus-TCP per-socket stream buffers
# (FreeRTOSIPConfig.h: RX 4*1460 / TX 2*1460 instead of the 24*1460/24*1460 throughput tier).
# The throughput tier let ONE TLS socket claim ~70 KB of stream buffer: bl616cl (~85 KB total
# TLSF heap) couldn't even open the MQTT TLS (prvTCPCreateStream malloc fail -> the socket is
# force-closed, recv returns ENOMEM/ENOTCONN mid-handshake), and on bl616 the post-takeover S3
# second-TLS handshake OOMed. bl602/bl702 have their own (tighter still) tier hard-gated by chip
# macro in the header. iperf/QUIC throughput images keep the big windows.
if(AT_ENABLE_AWS OR AT_ENABLE_MINIMAL)
    foreach(_net_tgt freertos_plus_tcp bouffalo_freertos_tcp)
        if(TARGET ${_net_tgt})
            target_compile_definitions(${_net_tgt} PRIVATE AT_NET_LOW_MEM)
        endif()
    endforeach()
endif()

bflb_add_executable(at ${AT_USER_SOURCES})

target_include_directories(at PRIVATE
    ${AT_USER_SRC_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
)

# The Bouffalo platform bodies (WiFi event handlers, wifi_port_* HAL, network-up hook, and the
# app_platform_* / app_console_register_platform / app_network_up_start_services overrides) live in
# src/main.cpp itself, behind the BL* chip #ifdefs (main.cpp is already compiled into `at`).
target_link_libraries(at PRIVATE bouffalo_sdk)

# Shared `ota_http <url>` command (component/ota_http_config): compiled into the app (per-image macros),
# over coreHTTP + llhttp + the shared TLS transport, streaming into the BL602 OTA slot writer
# (bouffalo_sdk/ota_flash_port). main() registers it from app_console_register_platform(). bl602 only:
# its https path needs the mbedTLS TLS transport (TLS_FreeRTOS_*), which - like aws_iot - is only
# linked with the AWS stack (AT_ENABLE_AWS). AWS=OFF (e.g. a pure MCUMGR image) drops ota_http with it;
# main.cpp gates ota_http_register() on the matching AT_PROJECT_AWS. ota_flash_port (no TLS) rides on.
if((BOUFFALO_CHIP STREQUAL "bl602" OR BOUFFALO_CHIP STREQUAL "bl616" OR BOUFFALO_CHIP STREQUAL "bl616cl") AND AT_ENABLE_AWS)
    target_sources(at PRIVATE ${OTA_HTTP_SOURCES})
    # basename-only __FILE__ in the rxi log (feedback_log_basename_prefix_map); -w: vendored urlparse.
    set_source_files_properties("${AT_ROOT}/component/ota_http_config/ota_http.c" PROPERTIES
        COMPILE_OPTIONS "-fmacro-prefix-map=${AT_ROOT}/component/ota_http_config/=")
    set_source_files_properties("${AT_ROOT}/component/ota_http_config/urlparse.c" PROPERTIES
        COMPILE_OPTIONS "-w;-fmacro-prefix-map=${AT_ROOT}/component/ota_http_config/=")
    # corehttp_config propagates all coreHTTP/llhttp include dirs (incl. the transport-interface
    # dir upstream keeps on a separate target); network_transport_headers carries transport_tls.h.
    target_link_libraries(at PRIVATE ota_http_config corehttp_config network_transport_headers)
endif()

# NimBLE host: main.cpp starts the shared nimble_start() from a boot task under this macro.
if(AT_ENABLE_NIMBLE)
    target_compile_definitions(at PRIVATE AT_PROJECT_NIMBLE)
endif()
