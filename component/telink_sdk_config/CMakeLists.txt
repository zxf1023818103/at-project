# Application/RTOS adapters. All vendor inputs are owned by component/telink_sdk.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../telink_sdk" vendor)
if(TL3238_ENABLE_BLE)
    target_sources(telink_sdk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ble_controller/controller.c")
    target_include_directories(telink_sdk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ble_controller")
    target_link_libraries(telink_sdk PUBLIC FreeRTOS)
    if(TL3238_ENABLE_CS)
        target_sources(telink_sdk PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/ble_controller/channel_sounding.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/ble_controller/cs_hci.c")
    endif()
endif()
set(TL3238_LINKER_SCRIPT "${TL3238_LINKER_SCRIPT}" PARENT_SCOPE)
