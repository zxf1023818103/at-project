set(AMEBADPLUS_IMAGE_TOOL_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(amebadplus_add_image name)
    # Config libraries have circular dependencies (TLS, libc, hardware entropy).
    # Group the complete link closure, including CMake's transitive archives.
    foreach(lang C CXX)
        set(CMAKE_${lang}_LINK_EXECUTABLE
            "<CMAKE_${lang}_COMPILER> <FLAGS> <CMAKE_${lang}_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group"
            PARENT_SCOPE)
    endforeach()
    set(AMEBADPLUS_IMAGE_TOOL_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    set(AMEBADPLUS_PROJECT "${AT_ROOT}/project/amebadplus")
    set(AMEBADPLUS_SDK "${AT_ROOT}/platform/ameba-rtos")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(name STREQUAL "bootloader_km4")
        set(ld ameba_img1_all.ld)
        set(rom ameba_rom_symbol_acut_s.ld)
        add_executable(${name} ${AMEBADPLUS_IMAGE_TOOL_DIR}/empty.c)
    else()
        set(ld ameba_img2_all.ld)
        set(rom ameba_rom_symbol_acut.ld)
        if(AMEBADPLUS_CORE STREQUAL "km4" AND NOT AT_ENABLE_TFM)
            set(rom ameba_rom_symbol_acut_s.ld)
        endif()
        if(AMEBADPLUS_CORE STREQUAL "km4")
            add_executable(${name} ${AT_ROOT}/src/main.cpp app_port.c ameba_app_start.c)
            target_include_directories(${name} PRIVATE ${AT_ROOT}/src ${AT_ROOT}/component/80211d_config)
            target_compile_definitions(${name} PRIVATE AT_AMEBADPLUS_STANDALONE)
        else()
            add_executable(${name} main.c ameba_app_start.c)
        endif()
    endif()
    set_target_properties(${name} PROPERTIES SUFFIX .axf)
    foreach(f memchr memcmp memcpy memmove memset rand strcmp stricmp strlen strtoul strupr strcpy strncpy strtol)
        target_link_options(${name} PRIVATE -Wl,--wrap=${f})
    endforeach()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/image.ld
        COMMAND ${CMAKE_C_COMPILER} -E -P -x c ${AMEBADPLUS_LD_DEFINES} -I${AMEBADPLUS_PROJECT}
                -I${AMEBADPLUS_PROJECT}/app_${AMEBADPLUS_CORE}
                ${CMAKE_CURRENT_SOURCE_DIR}/${ld} -o ${CMAKE_CURRENT_BINARY_DIR}/image.ld
        COMMAND ${Python3_EXECUTABLE} ${AMEBADPLUS_IMAGE_TOOL_DIR}/build_image.py ld
                ${CMAKE_CURRENT_BINARY_DIR}/image.ld ${CMAKE_CURRENT_SOURCE_DIR}/${rom}
        DEPENDS ${ld} ${rom} ${AMEBADPLUS_PROJECT}/ameba_layout.ld
                ${AMEBADPLUS_PROJECT}/app_km4/platform_autoconf.h)
    add_custom_target(${name}_ld DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/image.ld)
    add_dependencies(${name} ${name}_ld)
    target_link_options(${name} PRIVATE -T${CMAKE_CURRENT_BINARY_DIR}/image.ld
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${name}.map)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/image.ld
        ${AMEBADPLUS_IMAGE_TOOL_DIR}/build_image.py ${AMEBADPLUS_PROJECT}/manifest.json5)
    target_link_libraries(${name} PRIVATE amebadplus_vendor amebadplus_sdk)
    set(libdir ${AMEBADPLUS_SDK}/component/soc/amebadplus/project/project_${AMEBADPLUS_CORE}/lib)
    set(libs ${libdir}/soc/lib_chipinfo.a ${libdir}/soc/lib_crashdump.a)
    if(name STREQUAL "bootloader_km4")
        list(APPEND libs ${libdir}/soc/lib_bootloader.a)
    else()
        list(APPEND libs FreeRTOS freertos_config tlsf_config)
        list(APPEND libs ${libdir}/soc/lib_pmc.a)
        if(AMEBADPLUS_CORE STREQUAL "km0")
            target_link_libraries(${name} PRIVATE bootloader_km0 bootloader_km0_2nd)
            foreach(l wifi_whc_np wifi_common wifi_fw coex)
                list(APPEND libs ${libdir}/application/lib_${l}.a)
            endforeach()
            target_sources(${name} PRIVATE ${AT_ROOT}/component/amebadplus/osdep/gcc_wrap.c)
            foreach(f malloc calloc realloc free _malloc_r _calloc_r _realloc_r _free_r printf)
                target_link_options(${name} PRIVATE -Wl,--wrap=${f})
            endforeach()
        else()
            foreach(l wifi_whc_ap wifi_com_sec wifi_rtk_app wpa_lite wps eap p2p)
                list(APPEND libs ${libdir}/application/lib_${l}.a)
            endforeach()
            target_link_libraries(${name} PRIVATE amebadplus_port syscall_config)
            list(APPEND libs FreeRTOS freertos_config monitor monitor_cmds lfs_port
                littlefs lfs_config freertos_plus_tcp wifi_service net_cli sntp_time rxi_log mbedtls_config mbedcrypto mbedx509 mbedtls at_console)
            include(${AMEBADPLUS_PROJECT}/features.cmake)
            target_link_options(${name} PRIVATE -Wl,--wrap=setenv -Wl,--wrap=unsetenv
                -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc)
        endif()
    endif()
    if(NOT name STREQUAL "bootloader_km4")
        list(APPEND libs c m nosys)
    endif()
    target_link_libraries(${name} PRIVATE -Wl,--start-group ${libs} gcc -Wl,--end-group)
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${AMEBADPLUS_IMAGE_TOOL_DIR}/build_image.py image
            --elf $<TARGET_FILE:${name}> --core ${AMEBADPLUS_CORE}
            --kind ${name} --objcopy ${CMAKE_OBJCOPY} --nm ${CMAKE_NM}
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${name}>)
endfunction()
