# The radio remains on KM4NS; NAN control and IP run on KM4TZ under FreeRTOS.
if(AMEBAGREEN2_CORE STREQUAL "km4ns")
    target_sources(amebagreen2_vendor PRIVATE vendor/ameba_xtal_trackingcfg.c nan_ipc_device.c
        fixes/whc_dev_nan_restart_fix.c)
    set_target_properties(amebagreen2_coex PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_coex.a)
    set_target_properties(amebagreen2_wifi_common PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_wifi_common_km4ns.a)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(nan_radio "${CMAKE_CURRENT_BINARY_DIR}/lib_wifi_whc_np_nan.a")
    add_custom_command(OUTPUT ${nan_radio}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/fixes/make_nan_fixed_library.py
            --cross-compile ${AMEBAGREEN2_TOOLCHAIN_ROOT}/bin/arm-none-eabi-
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_wifi_whc_np.a ${nan_radio}
        DEPENDS vendor/lib_wifi_whc_np.a
            fixes/make_nan_fixed_library.py fixes/make_nan_bootstrap_fix.py
            fixes/make_nan_faw_id_fix.py fixes/make_nan_ndl_peer_fix.py
            fixes/whc_dev_nan_pasn_cipher_fix.c fixes/whc_dev_nan_bootstrap_fix.c
            fixes/whc_dev_nan_auth_peer_fix.c
            fixes/whc_dev_nan_ndl_peer_fix.c fixes/whc_dev_nan_ndl_immutable_fix.c
            fixes/whc_dev_nan_ndc_control_fix.c
        VERBATIM)
    add_custom_target(green2_nan_radio DEPENDS ${nan_radio})
    set_target_properties(amebagreen2_wifi_whc_np PROPERTIES IMPORTED_LOCATION ${nan_radio})
    add_dependencies(amebagreen2_wifi_whc_np green2_nan_radio)
else()
    set_target_properties(amebagreen2_wifi_whc_ap PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_whc_ap.a)
    set_target_properties(amebagreen2_wifi_common PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_wifi_common_km4tz.a)
    set_target_properties(amebagreen2_wifi_rtk_app PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_wifi_rtk_app.a)
    set_target_properties(amebagreen2_wpa_lite PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib_wpa_lite.a)
    target_sources(amebagreen2_vendor PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host.c
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host_trx.c
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host_msg_queue.c
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host_api_basic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host_api_ext.c
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host_api.c
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/whc_ipc_host_nan.c)
    target_include_directories(amebagreen2_vendor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
    if(AT_NAN_USB_BRIDGE)
        add_library(green2_nan STATIC nan_transport.c usb_bridge/green2_bridge.c)
        target_link_libraries(green2_nan PRIVATE nan_usb)
    else()
        include(host.cmake)
        nan_add_freertos_host(green2_nan)
        target_sources(green2_nan PRIVATE nan_transport.c nan_p256.c)
        target_link_libraries(green2_nan PRIVATE green2_pasn)
    endif()
    target_include_directories(green2_nan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(green2_nan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
    target_link_libraries(green2_nan PRIVATE amebagreen2_sdk FreeRTOS monitor freertos_plus_tcp mbedcrypto lfs_port tlsf_config)
endif()
