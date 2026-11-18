# AmebaGreen2 hardware libraries. Executables and memory maps live in project/amebagreen2.
set(AMEBAGREEN2_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(AMEBAGREEN2_PROJECT "${AT_ROOT}/project/amebagreen2")
option(AT_ENABLE_NAN "Standalone Wi-Fi Aware on FreeRTOS" OFF)
if(AT_ENABLE_NAN)
    add_compile_definitions(AT_ENABLE_NAN CONFIG_WIFI_NAN_ENABLE CONFIG_NAN CONFIG_NAN_V2
        CONFIG_NAN_PAIRING NAN_CUSTOMER_NANDOW CONFIG_WIFI_NAN_HOST_APP)
endif()
foreach(owner fwlib mbed usrcfg string_rom cmsis_rom dlist platform_stdlib osdep monitor_port pmu
              wifi wlan wpa_supplicant freertos_tcp_port)
    add_subdirectory(${owner})
endforeach()
include(${CMAKE_CURRENT_LIST_DIR}/${AMEBAGREEN2_IMAGE}_sources.cmake)
# NP supplies its wrapper; AP uses the shared syscall implementation.
list(FILTER AMEBAGREEN2_VENDOR_SOURCES EXCLUDE REGEX "string_rom/gcc_wrap.c$")
if(AT_ENABLE_TFM AND AMEBAGREEN2_IMAGE STREQUAL "bootloader_km4tz")
    list(FILTER AMEBAGREEN2_VENDOR_SOURCES EXCLUDE REGEX "ameba_boot_trustzonecfg.c$")
    list(APPEND AMEBAGREEN2_VENDOR_SOURCES ${AMEBAGREEN2_PROJECT}/bootloader_km4tz/tfm_trustzonecfg.c)
endif()
if(AT_ENABLE_EAP)
    list(FILTER AMEBAGREEN2_VENDOR_SOURCES EXCLUDE REGEX "wifi_feature_dis_eap.c$")
endif()
if(AT_ENABLE_WPS AND AMEBAGREEN2_IMAGE STREQUAL "app_km4tz")
    set(_wps_source ${AMEBAGREEN2_COMPONENT_DIR}/wpa_supplicant/wpa_supplicant/wifi_wps_config.c)
    file(READ ${_wps_source} _wps)
    string(REPLACE "setting.password);" "\"[redacted]\");" _wps "${_wps}")
    string(REPLACE "wifi->password);" "\"[redacted]\");" _wps "${_wps}")
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/wifi_wps_config.c CONTENT "${_wps}" @ONLY)
    list(REMOVE_ITEM AMEBAGREEN2_VENDOR_SOURCES ${_wps_source})
    list(APPEND AMEBAGREEN2_VENDOR_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/wifi_wps_config.c)
endif()
if(AT_ENABLE_NAN AND NOT AMEBAGREEN2_IMAGE STREQUAL "bootloader_km4tz")
    # Match the NAN archive's feature-disable ABI. The older WTN stubs omit
    # rtw_macid_wtn_mgt_init, so unused R-Mesh initialization overwrites the
    # NAN MACID capacity with two entries and rejects all NAN peer MACIDs.
    list(REMOVE_ITEM AMEBAGREEN2_VENDOR_SOURCES
        ${AMEBAGREEN2_COMPONENT_DIR}/wifi/wifi_feature_dis_wtn.c)
    list(APPEND AMEBAGREEN2_VENDOR_SOURCES
        ${AMEBAGREEN2_COMPONENT_DIR}/nan/vendor/wifi_feature_dis_wtn.c)
    list(REMOVE_ITEM AMEBAGREEN2_VENDOR_SOURCES ${AMEBAGREEN2_COMPONENT_DIR}/usrcfg/ameba_wificfg.c
        ${AMEBAGREEN2_COMPONENT_DIR}/wifi/wifi_feature_dis_mcc.c)
    list(APPEND AMEBAGREEN2_VENDOR_SOURCES ${AMEBAGREEN2_COMPONENT_DIR}/nan/vendor/ameba_wificfg.c
        ${AMEBAGREEN2_COMPONENT_DIR}/nan/vendor/rtw_task_size.c)
endif()
add_library(amebagreen2_sdk INTERFACE)
if(AT_ENABLE_NAN)
    target_compile_definitions(amebagreen2_sdk INTERFACE AT_ENABLE_NAN CONFIG_WIFI_NAN_ENABLE
        CONFIG_NAN CONFIG_NAN_V2 CONFIG_NAN_PAIRING NAN_CUSTOMER_NANDOW CONFIG_WIFI_NAN_HOST_APP)
    target_include_directories(amebagreen2_sdk SYSTEM BEFORE INTERFACE ${AMEBAGREEN2_COMPONENT_DIR}/nan/vendor)
endif()
target_include_directories(amebagreen2_sdk SYSTEM INTERFACE
    ${AMEBAGREEN2_PROJECT}/app_${AMEBAGREEN2_CORE}
    ${AMEBAGREEN2_PROJECT}/bootloader_km4tz
    ${AMEBAGREEN2_COMPONENT_DIR}/fwlib/rom
    ${AMEBAGREEN2_COMPONENT_DIR}/lfs_port)
target_link_libraries(amebagreen2_sdk INTERFACE
    amebagreen2_fwlib_headers amebagreen2_mbed_headers amebagreen2_usrcfg_headers
    amebagreen2_string_rom_headers amebagreen2_cmsis_rom_headers amebagreen2_dlist_headers
    amebagreen2_platform_stdlib_headers amebagreen2_osdep_headers
    amebagreen2_monitor_port_headers amebagreen2_pmu_headers amebagreen2_wifi_headers
    amebagreen2_wlan_headers amebagreen2_wpa_supplicant_headers amebagreen2_freertos_tcp_port_headers)
target_compile_definitions(amebagreen2_sdk INTERFACE DM_ODM_SUPPORT_TYPE=32)
if(AT_ENABLE_HCI)
    target_compile_definitions(amebagreen2_sdk INTERFACE AT_GREEN2_HCI=1)
endif()
if(AT_ENABLE_WPS)
    target_compile_definitions(amebagreen2_sdk INTERFACE CONFIG_ENABLE_WPS=1)
endif()
if((AT_ENABLE_NIMBLE OR AT_ENABLE_HCI) AND NOT AMEBAGREEN2_IMAGE STREQUAL "bootloader_km4tz")
    target_compile_definitions(amebagreen2_sdk INTERFACE CONFIG_BT=1 CONFIG_BT_COEXIST=1)
    if(AMEBAGREEN2_CORE STREQUAL "km4ns")
        target_compile_definitions(amebagreen2_sdk INTERFACE CONFIG_COEXIST_DEV=1)
    else()
        target_compile_definitions(amebagreen2_sdk INTERFACE CONFIG_COEXIST_HOST=1)
        foreach(source rtw_coex_ipc_host rtw_coex_host_api_com rtw_coex_host_api_bt
                       rtw_coex_host_api_ext rtw_coex_host_api_wp rtw_coex_ipc_host_api)
            list(APPEND AMEBAGREEN2_VENDOR_SOURCES ${AMEBAGREEN2_COMPONENT_DIR}/wlan/${source}.c)
        endforeach()
    endif()
endif()
add_library(amebagreen2_vendor OBJECT ${AMEBAGREEN2_VENDOR_SOURCES})
if(AMEBAGREEN2_IMAGE STREQUAL "bootloader_km4tz")
    target_compile_definitions(amebagreen2_vendor PRIVATE CONFIG_NON_OS)
endif()
if(AT_ENABLE_WPS AND AMEBAGREEN2_IMAGE STREQUAL "app_km4tz")
    target_include_directories(amebagreen2_vendor PRIVATE
        ${AMEBAGREEN2_COMPONENT_DIR}/wpa_supplicant/wpa_supplicant)
endif()
target_link_libraries(amebagreen2_vendor PUBLIC amebagreen2_sdk FreeRTOS)
target_compile_options(amebagreen2_vendor PRIVATE -w)
if(NOT AMEBAGREEN2_IMAGE STREQUAL "bootloader_km4tz")
    target_sources(amebagreen2_vendor PRIVATE osdep/os_wrapper_memory.c osdep/heap.c osdep/assert.c
        ${AMEBAGREEN2_COMPONENT_DIR}/osdep/os_wrapper_static_functions.c)
    target_compile_options(amebagreen2_vendor PRIVATE -include ${CMAKE_CURRENT_LIST_DIR}/osdep/heap.h)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/image_tool/image.cmake)
if(AT_ENABLE_NAN AND NOT AMEBAGREEN2_IMAGE STREQUAL "bootloader_km4tz")
    add_subdirectory(nan)
endif()
if(AMEBAGREEN2_CORE STREQUAL "km4ns")
    add_library(bootloader_km4ns OBJECT ${AMEBAGREEN2_PROJECT}/bootloader_km4ns/bootloader_km4ns.c)
    target_link_libraries(bootloader_km4ns PRIVATE amebagreen2_sdk FreeRTOS)
endif()

if(AMEBAGREEN2_IMAGE STREQUAL "app_km4tz")
    # Armv8.1-M does not select monitor's Armv8-M default. Certificate commands
    # need the same bounded line buffer as AmebaD/Dplus, including base64 data.
    target_compile_definitions(monitor PRIVATE MONITOR_CLI_MAX_INPUT=2560)
    if(AT_ENABLE_NIMBLE OR AT_ENABLE_HCI)
        add_subdirectory(hci)
    endif()
    if(AT_ENABLE_NIMBLE)
        add_subdirectory(nimble_port)
    endif()
    add_subdirectory(mbedtls_port)
    add_subdirectory(lfs_port)
    if(AT_ENABLE_AWS OR AT_ENABLE_MCUMGR)
        add_subdirectory(ota_flash_port)
    endif()
    if(AT_ENABLE_WPS OR AT_ENABLE_EAP)
        add_subdirectory(wifi_provision)
    endif()
    add_library(amebagreen2_port OBJECT syscall_port/console_loguart.c syscall_port/rtc_time.c monitor_port/monitor_port.c
        freertos_tcp_port/packet.c freertos_tcp_port/network.c wifi/policy.c wifi/optional.c wifi/country.c)
    target_link_libraries(amebagreen2_port PUBLIC amebagreen2_sdk at_console FreeRTOS monitor freertos_plus_tcp wifi_service 80211d)
    target_include_directories(amebagreen2_port PRIVATE ${AT_ROOT}/component/syscall_config)
    target_sources(amebagreen2_port PRIVATE board_dsleep_port/board_dsleep_green2.c)
    target_link_libraries(amebagreen2_port PRIVATE board_dsleep)
endif()
