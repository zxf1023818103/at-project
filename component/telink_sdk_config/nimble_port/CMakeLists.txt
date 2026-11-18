set(_nb "${TL3238_NIMBLE_ROOT}")
if(NOT EXISTS "${_nb}/nimble/host/src/ble_hs.c")
    message(FATAL_ERROR "Missing local NimBLE 1.10 component: ${_nb}")
endif()
file(GLOB _sources CONFIGURE_DEPENDS
    "${_nb}/porting/nimble/src/*.c"
    "${_nb}/porting/npl/freertos/src/*.c"
    "${_nb}/nimble/src/*.c"
    "${_nb}/nimble/host/src/*.c"
    "${_nb}/nimble/host/util/src/*.c"
    "${_nb}/nimble/host/services/gap/src/*.c"
    "${_nb}/nimble/host/services/gatt/src/*.c"
    "${_nb}/nimble/transport/src/*.c")
# The direct LittleFS backend below supplies all store callbacks; Mynewt's
# config subsystem and the old zero-capacity RAM fixture are not linked.
list(REMOVE_ITEM _sources "${_nb}/porting/nimble/src/hal_timer.c"
    "${_nb}/porting/nimble/src/os_cputime.c" "${_nb}/porting/nimble/src/os_cputime_pwr2.c")
if(TL3238_ENABLE_CS)
    list(REMOVE_ITEM _sources "${_nb}/nimble/host/src/ble_cs.c")
    list(APPEND _sources "${CMAKE_CURRENT_SOURCE_DIR}/ble_cs.c")
    set(_ras "${AT_ROOT}/component/nimble_config/ras")
    list(APPEND _sources "${_ras}/ras_codec.c" "${_ras}/ras_control.c"
        "${_ras}/cs_distance.c" "${_ras}/ras_selftest.c" "${_ras}/ble_ras.c")
endif()
# Keep the RPA fallback at the Host connection-complete boundary. Renaming
# this one translation unit works with LTO and leaves the upstream tree intact.
set_source_files_properties("${_nb}/nimble/host/src/ble_gap.c" PROPERTIES
    COMPILE_DEFINITIONS "ble_gap_rx_conn_complete=tl3238_gap_rx_conn_complete")
add_library(tl3238_nimble STATIC ${_sources} transport.c memory.c privacy.c)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../storage" storage)
set(_storage "${CMAKE_CURRENT_SOURCE_DIR}/../storage")
set(_bond_store "${AT_ROOT}/component/nimble_config/store")
target_sources(tl3238_nimble PRIVATE "${_storage}/storage_port.c"
    "${_bond_store}/ble_store_littlefs.c")
target_include_directories(tl3238_nimble PUBLIC "${_storage}" "${_bond_store}")
target_link_libraries(tl3238_nimble PUBLIC lfs_port PRIVATE freertos-cli_config)
if(TL3238_ENABLE_COC)
    if(NOT TARGET ble_coc_nimble)
        add_subdirectory("${AT_ROOT}/component/ble_coc_config" ble_coc_config)
    endif()
    target_sources(tl3238_nimble PRIVATE ble_coc.c) # CLI and logger only
    target_include_directories(tl3238_nimble PUBLIC
        "${AT_ROOT}/component/ble_coc_config/include" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(tl3238_nimble PRIVATE ble_coc_nimble freertos-cli_config)
endif()
if(TL3238_ENABLE_CS)
    target_include_directories(tl3238_nimble PUBLIC "${_ras}")
endif()
set(_mb "${AT_ROOT}/component/mbedtls_config/mbedtls")
add_library(tl3238_crypto STATIC)
foreach(_src aes cipher cipher_wrap cmac bignum bignum_core bignum_mod bignum_mod_raw
             ecp ecp_curves ecdh platform platform_util constant_time)
    target_sources(tl3238_crypto PRIVATE "${_mb}/library/${_src}.c")
endforeach()
target_include_directories(tl3238_crypto SYSTEM PUBLIC "${_mb}/include" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(tl3238_crypto PUBLIC MBEDTLS_CONFIG_FILE="tl3238_mbedtls_config.h")
if(TL3238_ENABLE_OTA)
    target_sources(tl3238_crypto PRIVATE "${_mb}/library/sha256.c")
    target_compile_definitions(tl3238_crypto PUBLIC TL3238_ENABLE_OTA=1)
endif()
target_link_libraries(tl3238_nimble PUBLIC tl3238_crypto)
target_include_directories(tl3238_nimble SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${_nb}/porting/nimble/include" "${_nb}/porting/npl/freertos/include"
    "${_nb}/nimble/include" "${_nb}/nimble/host/include"
    "${_nb}/nimble/host/util/include" "${_nb}/nimble/host/store/config/include"
    "${_nb}/nimble/host/services/gap/include" "${_nb}/nimble/host/services/gatt/include"
    "${_nb}/nimble/transport/include")
target_include_directories(tl3238_nimble PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ble_controller")
target_include_directories(tl3238_nimble PRIVATE "${_nb}/nimble/host/src")
target_link_libraries(tl3238_nimble PUBLIC FreeRTOS)
target_compile_definitions(tl3238_nimble PRIVATE
    malloc=tl3238_nimble_malloc calloc=tl3238_nimble_calloc
    realloc=tl3238_nimble_realloc free=tl3238_nimble_free)

add_subdirectory("${AT_ROOT}/component/tinycbor_config" tinycbor)
add_subdirectory("${AT_ROOT}/component/mcumgr_config" mcumgr)
# SMP runs synchronously on the host task while its BLE reassembly buffer is
# stable. Avoid a second 1152-byte request copy; other platforms retain it.
target_compile_definitions(mcumgr PRIVATE MCUMGR_SMP_BORROW_REQUEST=1)
# The existing OS group carries AmebaD product strings. Generate a build-local
# copy with truthful TL3238 information; keep the vendored submodule untouched.
get_target_property(_mgmt_sources mcumgr SOURCES)
set(_os_src "${AT_ROOT}/component/mcumgr_config/mcumgr/cmd/os_mgmt/src/os_mgmt.c")
list(REMOVE_ITEM _mgmt_sources "${_os_src}")
set_property(TARGET mcumgr PROPERTY SOURCES "${_mgmt_sources}")
file(READ "${_os_src}" _os)
string(REPLACE "FreeRTOS V11.1.0 RTL8721D" "FreeRTOS V11.1.0 TL3238 NimBLE 1.10.0" _os "${_os}")
string(REPLACE "#define OS_MGMT_BOOTLOADER_NAME   \"MCUboot\""
    "#define OS_MGMT_BOOTLOADER_NAME   \"Telink ROM (no image management)\"" _os "${_os}")
if(TL3238_ENABLE_OTA)
    string(REPLACE "Telink ROM (no image management)" "Telink ROM A/B (permanent)" _os "${_os}")
    set(_img_src "${AT_ROOT}/component/mcumgr_config/mcumgr/cmd/img_mgmt/src")
    foreach(_file img_mgmt img_mgmt_state)
        list(REMOVE_ITEM _mgmt_sources "${_img_src}/${_file}.c")
    endforeach()
    set_property(TARGET mcumgr PROPERTY SOURCES "${_mgmt_sources}")
    file(READ "${_img_src}/img_mgmt.c" _img)
    set(_read_info "int\nimg_mgmt_read_info(")
    string(FIND "${_img}" "${_read_info}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Review mcumgr native image-information override")
    endif()
    string(REPLACE "${_read_info}" "__attribute__((weak))\n${_read_info}" _img "${_img}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/img_mgmt.c" CONTENT "${_img}" @ONLY)
    file(READ "${_img_src}/img_mgmt_state.c" _state)
    set(_hash_check "    if (hash_len == 0) {")
    string(FIND "${_state}" "${_hash_check}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "Review mcumgr image state validation")
    endif()
    string(REPLACE "${_hash_check}"
        "    if (hash_len != 0 && hash_len != IMAGE_HASH_LEN) return MGMT_ERR_EINVAL;\n    if (!confirm) return MGMT_ERR_ENOTSUP;\n${_hash_check}"
        _state "${_state}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/img_mgmt_state.c" CONTENT "${_state}" @ONLY)
    target_sources(mcumgr PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/img_mgmt.c"
        "${CMAKE_CURRENT_BINARY_DIR}/img_mgmt_state.c")
    target_include_directories(mcumgr PRIVATE "${_img_src}")
    set(_ota "${CMAKE_CURRENT_SOURCE_DIR}/../ota")
    target_sources(tl3238_nimble PRIVATE "${_ota}/ota_storage.c" "${_ota}/ota_port.c")
    target_sources(mcumgr PRIVATE "${_ota}/ota_mcumgr.c")
    target_include_directories(mcumgr PRIVATE "${_ota}")
    target_include_directories(tl3238_nimble PRIVATE "${_ota}")
    target_link_libraries(tl3238_nimble PRIVATE freertos-cli_config)
endif()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/os_mgmt.c" CONTENT "${_os}" @ONLY)
target_sources(mcumgr PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/os_mgmt.c")
target_sources(tl3238_nimble PRIVATE "${AT_ROOT}/component/mcumgr_config/smp_bt.c")
target_link_libraries(tl3238_nimble PUBLIC mcumgr tinycbor_config)
