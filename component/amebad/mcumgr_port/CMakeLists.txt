# mcumgr_port - AmebaD KM4 SMP-over-BLE transport (smp_bt.c) bridging the NimBLE GATT host to the
# mcumgr core (component/mcumgr_config). Built only for the KM4 MCUMGR image.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()
if(NOT AMEBAD_ENABLE_MCUMGR)
    return()
endif()

add_library(mcumgr_port STATIC EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/smp_bt.c"
)

# Strip the source-dir prefix from __FILE__ so the rxi log shows the basename (smp_bt.c) not the
# absolute path.
target_compile_options(mcumgr_port PRIVATE "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

# Include-only deps (NOT link deps): mcumgr_port rides the image's cross_refs LINK_GROUP alongside
# mcumgr / rxi_log / nimble archives, so linking the umbrellas here would cycle. The group resolves
# the symbols at link; here we only need the headers.
target_include_directories(mcumgr_port PRIVATE
    $<TARGET_PROPERTY:mcumgr,INTERFACE_INCLUDE_DIRECTORIES>          # mcumgr_smp_process + mgmt/smp + tinycbor prefix
    $<TARGET_PROPERTY:nimble_config,INTERFACE_INCLUDE_DIRECTORIES>  # host/ble_hs.h, ble_gatt, ble_att, os_mbuf
    $<TARGET_PROPERTY:rxi_log_config,INTERFACE_INCLUDE_DIRECTORIES> # at_log.h
    $<TARGET_PROPERTY:FreeRTOS,INTERFACE_INCLUDE_DIRECTORIES>
)
