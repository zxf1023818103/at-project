set(AMEBAGREEN2_IMAGE_TOOL_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(amebagreen2_add_image name)
    # Config libraries have circular dependencies (TLS, libc, hardware entropy).
    # Group the complete link closure, including CMake's transitive archives.
    foreach(lang C CXX)
        set(CMAKE_${lang}_LINK_EXECUTABLE
            "<CMAKE_${lang}_COMPILER> <FLAGS> <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
            PARENT_SCOPE)
    endforeach()
    set(AMEBAGREEN2_IMAGE_TOOL_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(AMEBAGREEN2_PROJECT "${AT_ROOT}/project/amebagreen2")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(name STREQUAL "bootloader_km4tz")
        set(ld ameba_img1_all.ld)
        set(rom ameba_rom_symbol_bcut_s.ld)
        add_executable(${name} ${AMEBAGREEN2_IMAGE_TOOL_DIR}/empty.c)
    else()
        set(ld ameba_img2_all.ld)
        set(rom ameba_rom_symbol_bcut.ld)
        if(AMEBAGREEN2_CORE STREQUAL "km4tz")
            add_executable(${name} ${AT_ROOT}/src/main.cpp app_port.c ameba_app_start.c)
            target_include_directories(${name} PRIVATE ${AT_ROOT}/src ${AT_ROOT}/component/80211d_config)
            target_compile_definitions(${name} PRIVATE AT_AMEBAGREEN2_STANDALONE)
        else()
            add_executable(${name} main.c ameba_app_start.c)
        endif()
    endif()
    if(AT_ENABLE_NAN)
        list(APPEND AMEBAGREEN2_LD_DEFINES -DAT_ENABLE_NAN)
    endif()
    set(extra_rom)
    if(NOT name STREQUAL "bootloader_km4tz")
        list(APPEND extra_rom
            COMMAND ${Python3_EXECUTABLE} ${AMEBAGREEN2_IMAGE_TOOL_DIR}/build_image.py ld
                ${CMAKE_CURRENT_BINARY_DIR}/image.ld ${CMAKE_CURRENT_SOURCE_DIR}/ameba_rom_symbol_bcut_wifi.ld)
    endif()
    set_target_properties(${name} PROPERTIES SUFFIX .axf)
    foreach(f memchr memcmp memcpy memmove memset rand strcmp stricmp strlen strtoul strupr strcpy strncpy strtol)
        target_link_options(${name} PRIVATE -Wl,--wrap=${f})
    endforeach()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/image.ld
        COMMAND ${CMAKE_C_COMPILER} -E -P -x c ${AMEBAGREEN2_LD_DEFINES} -I${AMEBAGREEN2_PROJECT}
                -I${AMEBAGREEN2_PROJECT}/app_${AMEBAGREEN2_CORE}
                ${CMAKE_CURRENT_SOURCE_DIR}/${ld} -o ${CMAKE_CURRENT_BINARY_DIR}/image.ld
        COMMAND ${Python3_EXECUTABLE} ${AMEBAGREEN2_IMAGE_TOOL_DIR}/build_image.py ld
                ${CMAKE_CURRENT_BINARY_DIR}/image.ld ${CMAKE_CURRENT_SOURCE_DIR}/${rom}
        ${extra_rom}
        DEPENDS ${ld} ${rom} ${AMEBAGREEN2_PROJECT}/ameba_layout.ld
                ${AMEBAGREEN2_PROJECT}/app_km4tz/platform_autoconf.h)
    add_custom_target(${name}_ld DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/image.ld)
    add_dependencies(${name} ${name}_ld)
    target_link_options(${name} PRIVATE -T${CMAKE_CURRENT_BINARY_DIR}/image.ld
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/image.ld
        ${AMEBAGREEN2_IMAGE_TOOL_DIR}/build_image.py ${AMEBAGREEN2_PROJECT}/manifest.json5)
    target_link_libraries(${name} PRIVATE amebagreen2_vendor amebagreen2_sdk)
    set(libs amebagreen2_chipinfo)
    if(name STREQUAL "bootloader_km4tz")
        list(APPEND libs ${AMEBAGREEN2_PROJECT}/bootloader_km4tz/lib_bootloader.a)
        if(AT_ENABLE_TFM)
            target_link_options(${name} PRIVATE -Wl,--section-start=.gnu.sgstubs=0x1040b000)
        endif()
    else()
        list(APPEND libs FreeRTOS freertos_config tlsf_config)
        list(APPEND libs amebagreen2_pmc)
        if(AMEBAGREEN2_CORE STREQUAL "km4ns")
            target_link_libraries(${name} PRIVATE bootloader_km4ns)
            list(APPEND libs amebagreen2_wifi_whc_np amebagreen2_wifi_common
                ${AMEBAGREEN2_PROJECT}/app_km4ns/lib_wifi_fw.a amebagreen2_coex)
            if(AT_ENABLE_NAN)
                list(REMOVE_ITEM libs ${AMEBAGREEN2_PROJECT}/app_km4ns/lib_wifi_fw.a)
                list(APPEND libs ${AT_ROOT}/component/amebagreen2/nan/vendor/lib_wifi_fw.a)
                target_link_options(${name} PRIVATE
                    -Wl,--wrap=whc_ipc_dev_cfg80211_nan_cfgvendor_event_report
                    -Wl,--wrap=whc_ipc_dev_cfg80211_cfgvendor_send_cmd_reply
                    -Wl,--wrap=rtw_nan_ndp_response_prehandle
                    -Wl,--wrap=rtw_nan_pasn_rx_checking
                    -Wl,--wrap=rtw_nan_intfs_init
                    -Wl,--wrap=rtw_nan_start_api
                    -Wl,--wrap=rtw_nan_schdl_faw_act_add
                    -Wl,--wrap=rtw_nan_sdf_rx
                    -Wl,--wrap=rtw_nan_ndl_nego_rx
                    -Wl,--wrap=rtw_nan_ndl_attri_construct
                    -Wl,--wrap=rtw_nan_ndc_attri_contruct)
            endif()
            target_sources(${name} PRIVATE ${AT_ROOT}/component/amebagreen2/osdep/gcc_wrap.c)
            foreach(f malloc calloc realloc free _malloc_r _calloc_r _realloc_r _free_r printf)
                target_link_options(${name} PRIVATE -Wl,--wrap=${f})
            endforeach()
        else()
            foreach(l wifi_whc_ap wifi_common wifi_rtk_app wpa_lite wps eap p2p)
                list(APPEND libs amebagreen2_${l})
            endforeach()
            target_link_libraries(${name} PRIVATE amebagreen2_port syscall_config)
            list(APPEND libs FreeRTOS freertos_config monitor monitor_cmds lfs_port
                littlefs lfs_config freertos_plus_tcp wifi_service net_cli sntp_time rxi_log mbedtls_config mbedcrypto mbedx509 mbedtls at_console)
            include(${AMEBAGREEN2_PROJECT}/features.cmake)
            if(AT_ENABLE_USB_BENCH)
                list(APPEND libs green2_usb_bench)
            endif()
            if(AT_ENABLE_NAN)
                list(APPEND libs green2_nan)
            endif()
            target_link_options(${name} PRIVATE -Wl,--wrap=setenv -Wl,--wrap=unsetenv
                -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc)
        endif()
    endif()
    if(NOT name STREQUAL "bootloader_km4tz")
        list(APPEND libs c m nosys)
    endif()
    target_link_libraries(${name} PRIVATE -Wl,--start-group ${libs} gcc -Wl,--end-group)
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${AMEBAGREEN2_IMAGE_TOOL_DIR}/build_image.py image
            --elf $<TARGET_FILE:${name}> --core ${AMEBAGREEN2_CORE}
            --kind ${name} --objcopy ${CMAKE_OBJCOPY} --nm ${CMAKE_NM}
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>)
endfunction()
