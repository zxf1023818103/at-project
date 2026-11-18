add_subdirectory("${AT_ROOT}/component/littlefs_config" littlefs)
target_compile_definitions(littlefs PUBLIC LFS_NO_MALLOC LFS_NAME_MAX=32
    LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
target_compile_options(littlefs PRIVATE -Os)
add_subdirectory("${AT_ROOT}/component/ble_store_config" ble_store)
add_library(lfs_port STATIC lfs_storage.c flash_port.c)
target_include_directories(lfs_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lfs_port PUBLIC ble_store_lfs FreeRTOS PRIVATE telink_sdk_headers)
