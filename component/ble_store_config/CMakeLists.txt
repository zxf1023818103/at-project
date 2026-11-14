# Common codec and file transactions are Host independent. Compile each Host
# adapter in its Host target so its config, ABI and allocator stay identical.
add_library(ble_store_lfs STATIC lfs_blob.c ble_bond_store.c)
target_include_directories(ble_store_lfs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ble_store_lfs PUBLIC littlefs)
target_compile_options(ble_store_lfs PRIVATE -Os)

set(BLE_BOND_MAX_BONDS 4 CACHE STRING "Common persistent BLE bond capacity")
set(BLE_BOND_MAX_CCCDS 16 CACHE STRING "Common persistent BLE subscription capacity")
target_compile_definitions(ble_store_lfs PUBLIC
    BLE_BOND_MAX_BONDS=${BLE_BOND_MAX_BONDS} BLE_BOND_MAX_CCCDS=${BLE_BOND_MAX_CCCDS})
