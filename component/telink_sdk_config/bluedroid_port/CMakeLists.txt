add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../storage" storage)
set(BLUEDROID_PORT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/esp_compat.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/memory.c" "${CMAKE_CURRENT_SOURCE_DIR}/hci.c"
)
set(BLUEDROID_PORT_LIBRARIES FreeRTOS lfs_port)
set(BLUEDROID_PORT_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../ble_controller")
set(BLUEDROID_PORT_CONFIG_HEADER "tl3238_bluedroid_config.h")
set(BLUEDROID_ENABLE_GATT ON)
set(BLUEDROID_ENABLE_COC ON)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../bluedroid_config" host)

# This Telink controller rejects Set Controller To Host Flow Control (0x0c31).
# RX already uses the on-chip LL FIFO. Disable HCI C2H credits and the related
# Host Buffer Size command rather than fabricating successful HCI responses.
# Generate two local overlays; preserve the shared upstream/Bouffalo sources.
set(_host "${CMAKE_CURRENT_SOURCE_DIR}/../../bluedroid_config/esp-idf/components/bt/host/bluedroid")
set(_target_header "${_host}/common/include/common/bt_target.h")
set(_controller "${_host}/device/controller.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_target_header}" "${_controller}")
file(READ "${_target_header}" _header)
if(NOT _header MATCHES "#define C2H_FLOW_CONTROL_INCLUDED TRUE")
    message(FATAL_ERROR "Review upstream C2H configuration overlay")
endif()
string(REPLACE "#define C2H_FLOW_CONTROL_INCLUDED TRUE"
    "#define C2H_FLOW_CONTROL_INCLUDED FALSE" _header "${_header}")
string(REPLACE "\"bluedroid_user_config.h\"" "\"common/bluedroid_user_config.h\"" _header "${_header}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/common/bt_target.h" CONTENT "${_header}" @ONLY)
target_include_directories(bluedroid_config BEFORE PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/include")
file(READ "${_controller}" _source)
set(_start "    // Tell the controller about our buffer sizes and buffer counts next")
set(_end "    // Read the local version info off the controller next, including")
string(FIND "${_source}" "${_start}" _found_start)
string(FIND "${_source}" "${_end}" _found_end)
if(_found_start EQUAL -1 OR _found_end LESS _found_start)
    message(FATAL_ERROR "Review upstream Host Buffer Size overlay")
endif()
string(REPLACE "${_start}" "#if (C2H_FLOW_CONTROL_INCLUDED == TRUE)\n${_start}" _source "${_source}")
string(REPLACE "${_end}" "#endif\n${_end}" _source "${_source}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/controller.c" CONTENT "${_source}" @ONLY)
get_target_property(_sources bluedroid_config SOURCES)
list(FILTER _sources EXCLUDE REGEX "/device/controller[.]c$")
set_property(TARGET bluedroid_config PROPERTY SOURCES "${_sources}")
target_sources(bluedroid_config PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/controller.c")
target_compile_definitions(bluedroid_config PRIVATE
    malloc=tl3238_bd_malloc calloc=tl3238_bd_calloc
    realloc=tl3238_bd_realloc free=tl3238_bd_free
    printf=tl3238_bd_printf vprintf=tl3238_bd_vprintf
    BLE_COC_DEVICE_NAME="TL3238-Bluedroid" BLE_COC_HEAP_RESERVE=8192
    BLE_COC_BLUEDROID_STACK_WORDS=768 BLE_COC_BLUEDROID_PRIORITY=2)
