set(IDF_ROOT "${CMAKE_CURRENT_LIST_DIR}/esp-idf")
set(CONFIG_BT_BLUEDROID_ENABLED ON)
add_subdirectory(${IDF_ROOT}/components/bt/host/bluedroid ${CMAKE_CURRENT_BINARY_DIR}/upstream)
# Keep the upstream source manifest, omitting optional profile implementations
# which this PAN image does not configure (some have ESP VFS dependencies).
list(FILTER bluedroid_host_srcs EXCLUDE REGEX "/bta/(ar|av|gatt|hf_ag|hf_client|hd|hh|jv|pba|opp|sdp)/")
list(FILTER bluedroid_host_srcs EXCLUDE REGEX "/btc/profile/std/(a2dp|avrc|gatt|hf_ag|hf_client|hid|spp|sdp|l2cap|pbap|opp)/")
list(FILTER bluedroid_host_srcs EXCLUDE REGEX "/api/esp_(a2dp|avrc|hidd|hidh|hf_|spp|sdp|l2cap|pbac|opp)")
if(CONFIG_PAN_SPP)
    # Direct stack-level SPP server; the ESP VFS/JV wrapper is not needed.
    sdk_add_compile_definitions(-DRFCOMM_INCLUDED=TRUE)
endif()
set(BT_COMMON ${IDF_ROOT}/components/bt/common)
file(GLOB osi_sources CONFIGURE_DEPENDS ${BT_COMMON}/osi/*.c)
file(GLOB btc_sources CONFIGURE_DEPENDS ${BT_COMMON}/btc/core/*.c)
add_library(bluedroid_config STATIC EXCLUDE_FROM_ALL
    esp_compat.c nvs_easyflash.c bouffalo_hci.c
    ${bluedroid_host_srcs} ${osi_sources} ${btc_sources}
    ${BT_COMMON}/osal/src/bt_osal_freertos.c
    ${IDF_ROOT}/components/bt/ble_profiles/common/src/bt_prf_task.c
)
target_link_libraries(bluedroid_config PUBLIC sdk_intf_lib)
target_compile_options(bluedroid_config PRIVATE "SHELL:-include string.h")
target_include_directories(bluedroid_config PUBLIC
    include
    ${IDF_ROOT}/components/esp_common/include
    ${bluedroid_host_include_dirs} ${bluedroid_host_priv_include_dirs}
    ${BT_COMMON}/include ${BT_COMMON}/osi/include ${BT_COMMON}/btc/include
    ${BT_COMMON}/osal/include ${BT_COMMON}/hci_log/include
    ${BT_COMMON}/api/include/api ${BT_COMMON}/btc/profile/esp/include
    ${BT_COMMON}/btc/profile/esp/blufi/include
    ${IDF_ROOT}/components/bt/ble_profiles/common/include
)
if(TARGET freertos_plus_tcp)
    add_library(bluedroid_pan_tcp STATIC EXCLUDE_FROM_ALL pan_freertos_tcp.c)
    target_link_libraries(bluedroid_pan_tcp PUBLIC bluedroid_config freertos_plus_tcp)
endif()
