target_include_directories(telink_sdk_headers SYSTEM INTERFACE
    "${TELINK_BLE_DIR}" "${TELINK_BLE_DIR}/common" "${TELINK_BLE_DIR}/vendor/common")
target_compile_definitions(telink_sdk_headers INTERFACE
    CHIP_TYPE=CHIP_TYPE_TL323X TL3238_ENABLE_BLE=1 TLKAPI_DEBUG_ENABLE=0
    HCI_TR_EN=0 FREERTOS_ENABLE=0)
target_compile_options(telink_sdk_headers INTERFACE -fshort-wchar)
target_compile_options(telink_sdk PRIVATE -fno-builtin)
if(TL3238_ENABLE_CS)
    target_compile_definitions(telink_sdk_headers INTERFACE TL3238_ENABLE_CS=1 MAX_ANT_PATHS_SUPPORT=1)
endif()
if(TL3238_ENABLE_COC)
    target_compile_definitions(telink_sdk_headers INTERFACE TL3238_ENABLE_COC=1)
endif()
if(TL3238_ENABLE_OTA)
    target_compile_definitions(telink_sdk_headers INTERFACE TL3238_ENABLE_OTA=1
        TL3238_OTA_BUILD_NUMBER=${TL3238_OTA_BUILD_NUMBER})
endif()
target_sources(telink_sdk PRIVATE
    "${TELINK_BLE_DIR}/common/sdk_version.c" "${TELINK_BLE_DIR}/common/utility.c"
    "${TELINK_BLE_DIR}/vendor/common/app_common.c"
    "${TELINK_BLE_DIR}/vendor/common/ble_flash.c"
    "${TELINK_BLE_DIR}/vendor/common/tlkapi_debug.c")
if(EXISTS "${TELINK_BLE_DIR}/common/tl_string.c")
    target_sources(telink_sdk PRIVATE "${TELINK_BLE_DIR}/common/tl_string.c")
endif()
# Keep RF and drop old platform objects from a build-local controller archive.
set(_archive "${CMAKE_CURRENT_BINARY_DIR}/libtl3238_controller.a")
configure_file("${TELINK_BLE_DIR}/proj_lib/liblt_TL323X.a" "${_archive}" COPYONLY)
execute_process(COMMAND "${CMAKE_AR}" t "${TELINK_PLATFORM_DIR}/drivers/lib/libdriver.a"
    OUTPUT_VARIABLE _members COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" _members "${_members}")
list(FILTER _members EXCLUDE REGEX "^$")
list(FILTER _members EXCLUDE REGEX "^rf_")
execute_process(COMMAND "${CMAKE_AR}" d "${_archive}" ${_members}
    COMMAND_ERROR_IS_FATAL ANY)
add_library(telink_controller_blob STATIC IMPORTED GLOBAL)
set_target_properties(telink_controller_blob PROPERTIES IMPORTED_LOCATION "${_archive}")
# Preserve archive order: controller first, platform fallback second.
set_property(TARGET telink_sdk PROPERTY INTERFACE_LINK_LIBRARIES telink_sdk_headers)
target_link_libraries(telink_sdk PUBLIC telink_controller_blob telink_driver_blob m)
if(TL3238_ENABLE_CS)
    add_library(telink_cs_algorithm STATIC IMPORTED GLOBAL)
    set_target_properties(telink_cs_algorithm PROPERTIES IMPORTED_LOCATION
        "${TELINK_BLE_DIR}/algorithm/hadm/tlk_algo1/lib/libcs_tlk1.a")
    target_link_libraries(telink_sdk PUBLIC telink_cs_algorithm dsp)
endif()
