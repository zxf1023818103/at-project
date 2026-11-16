add_library(lfs_port STATIC lfs_port.c)
target_include_directories(lfs_port PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(lfs_port PUBLIC littlefs lfs_config PRIVATE amebagreen2_sdk FreeRTOS)
