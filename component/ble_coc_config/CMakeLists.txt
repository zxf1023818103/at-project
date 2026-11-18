# Sources compile inside the consuming host target so its syscfg, NPL,
# allocator and SDK settings apply. No chip SDK or RTOS is selected here.
add_library(ble_coc_protocol INTERFACE)
target_sources(ble_coc_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/coc_stream.c")
target_include_directories(ble_coc_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(ble_coc_nimble INTERFACE)
target_sources(ble_coc_nimble INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nimble/ble_coc_nimble.c")
target_link_libraries(ble_coc_nimble INTERFACE ble_coc_protocol)

add_library(ble_coc_bluedroid INTERFACE)
target_sources(ble_coc_bluedroid INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/bluedroid/ble_coc_bluedroid.c")
target_link_libraries(ble_coc_bluedroid INTERFACE ble_coc_protocol)
