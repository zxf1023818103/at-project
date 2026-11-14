set(IDF_ROOT "${CMAKE_CURRENT_LIST_DIR}/esp-idf")
set(CONFIG_BT_BLUEDROID_ENABLED ON)
if(CONFIG_PAN_BLUEDROID_COC OR BLUEDROID_ENABLE_COC)
    set(CONFIG_BT_BLE_L2CAP_COC_ENABLED ON)
endif()
add_subdirectory(${IDF_ROOT}/components/bt/host/bluedroid ${CMAKE_CURRENT_BINARY_DIR}/upstream)
# Keep the upstream source manifest, omitting optional profile implementations
# which this PAN image does not configure (some have ESP VFS dependencies).
if(CONFIG_PAN_GATT_BR OR CONFIG_PAN_GATT_LE OR CONFIG_PAN_BLUEDROID_COC OR BLUEDROID_ENABLE_GATT OR BLUEDROID_ENABLE_COC)
    set(omit_gatt "")
else()
    set(omit_gatt "gatt|")
endif()
list(FILTER bluedroid_host_srcs EXCLUDE REGEX "/bta/(ar|av|${omit_gatt}hf_ag|hf_client|hd|hh|jv|pba|opp|sdp)/")
list(FILTER bluedroid_host_srcs EXCLUDE REGEX "/btc/profile/std/(a2dp|avrc|${omit_gatt}hf_ag|hf_client|hid|spp|sdp|l2cap|pbap|opp)/")
list(FILTER bluedroid_host_srcs EXCLUDE REGEX "/api/esp_(a2dp|avrc|hidd|hidh|hf_|spp|sdp|l2cap|pbac|opp)")
if(CONFIG_PAN_SPP)
    # Direct stack-level SPP server; the ESP VFS/JV wrapper is not needed.
    sdk_add_compile_definitions(-DRFCOMM_INCLUDED=TRUE)
endif()
set(BT_COMMON ${IDF_ROOT}/components/bt/common)
file(GLOB osi_sources CONFIGURE_DEPENDS ${BT_COMMON}/osi/*.c)
file(GLOB btc_sources CONFIGURE_DEPENDS ${BT_COMMON}/btc/core/*.c)
if(NOT BLUEDROID_PORT_SOURCES)
    set(BLUEDROID_PORT_SOURCES esp_compat.c bouffalo_hci.c)
    set(BLUEDROID_PORT_LIBRARIES sdk_intf_lib)
endif()
add_library(bluedroid_config STATIC EXCLUDE_FROM_ALL
    ${BLUEDROID_PORT_SOURCES} nvs_lfs.c bond_config.c
    ${bluedroid_host_srcs} ${osi_sources} ${btc_sources}
    ${BT_COMMON}/osal/src/bt_osal_freertos.c
    ${IDF_ROOT}/components/bt/ble_profiles/common/src/bt_prf_task.c
)
if(NOT TARGET ble_store_lfs)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ble_store_config" ble_store)
endif()
target_link_libraries(bluedroid_config PUBLIC ${BLUEDROID_PORT_LIBRARIES} ble_store_lfs lfs_port)
# All ports have durable LFS NVS. Enable the Host's bond save/load path too;
# upstream defaults this off independently of the NVS implementation.
target_compile_definitions(bluedroid_config PUBLIC CONFIG_BT_BLE_SMP_BOND_NVS_FLASH=1 CONFIG_BT_SMP_MAX_BONDS=${BLE_BOND_MAX_BONDS})
if(BLUEDROID_PORT_INCLUDE_DIRS)
    target_include_directories(bluedroid_config PUBLIC ${BLUEDROID_PORT_INCLUDE_DIRS})
endif()
if(BLUEDROID_PORT_CONFIG_HEADER)
    target_compile_definitions(bluedroid_config PUBLIC
        BLUEDROID_PORT_CONFIG_HEADER="${BLUEDROID_PORT_CONFIG_HEADER}")
endif()
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

if(CONFIG_PAN_GATT_BR OR CONFIG_PAN_GATT_LE)
    target_sources(bluedroid_config PRIVATE gatt_control.c)
endif()

if(CONFIG_PAN_BLUEDROID_COC OR BLUEDROID_ENABLE_COC)
    if(NOT TARGET ble_coc_bluedroid)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ble_coc_config" ble_coc_config)
    endif()
    target_include_directories(bluedroid_config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../ble_coc_config/include")
    if(CONFIG_PAN_BLUEDROID_COC)
        target_compile_definitions(bluedroid_config PRIVATE BLE_COC_TX_QUEUE_LIMIT=8
            BLE_COC_TX_SDU_MAX=1024 BLE_COC_DEVICE_NAME="BL618DG-CoC-BD")
    endif()
    target_link_libraries(bluedroid_config PRIVATE ble_coc_bluedroid)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/bond_overlays.cmake")
set(_bond_host "${IDF_ROOT}/components/bt/host/bluedroid")
bluedroid_generate_bond_overlays("${_bond_host}" "${CMAKE_CURRENT_BINARY_DIR}")
get_target_property(_sources bluedroid_config SOURCES)
list(REMOVE_ITEM _sources "${_bond_host}/btc/core/btc_config.c"
    "${_bond_host}/btc/core/btc_dm.c" "${_bond_host}/stack/smp/smp_act.c")
set_property(TARGET bluedroid_config PROPERTY SOURCES "${_sources}")
target_sources(bluedroid_config PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/btc_config_lfs.c"
    "${CMAKE_CURRENT_BINARY_DIR}/btc_dm_bonds.c"
    "${CMAKE_CURRENT_BINARY_DIR}/smp_act_bonds.c")
