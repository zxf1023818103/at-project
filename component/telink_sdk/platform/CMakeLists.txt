set(_chip "${CMAKE_CURRENT_SOURCE_DIR}/drivers")
if(TL3238_ENABLE_BLE)
    # Quoted relative RF includes must resolve to the selected controller ABI.
    set(_chip "${CMAKE_CURRENT_BINARY_DIR}/drivers/TL323X")
    set(_overlay_files)
    file(GLOB_RECURSE _inputs CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/drivers"
        "${CMAKE_CURRENT_SOURCE_DIR}/drivers/*")
    foreach(_file IN LISTS _inputs)
        if(NOT _file MATCHES "^lib/include/rf/")
            list(APPEND _overlay_files "${_file}")
            configure_file("${CMAKE_CURRENT_SOURCE_DIR}/drivers/${_file}" "${_chip}/${_file}" COPYONLY)
        endif()
    endforeach()
    foreach(_dir ext_driver lib/include/rf)
        file(GLOB_RECURSE _inputs CONFIGURE_DEPENDS RELATIVE "${TELINK_BLE_DIR}/drivers/TL323X/${_dir}"
            "${TELINK_BLE_DIR}/drivers/TL323X/${_dir}/*")
        foreach(_file IN LISTS _inputs)
            list(APPEND _overlay_files "${_dir}/${_file}")
            configure_file("${TELINK_BLE_DIR}/drivers/TL323X/${_dir}/${_file}"
                "${_chip}/${_dir}/${_file}" COPYONLY)
        endforeach()
    endforeach()
    # Switching CS in an existing build must not retain extensions or RF
    # headers that only existed in the previous controller version.
    file(GLOB_RECURSE _old_files RELATIVE "${_chip}" "${_chip}/*")
    foreach(_file IN LISTS _old_files)
        if(NOT _file IN_LIST _overlay_files)
            file(REMOVE "${_chip}/${_file}")
        endif()
    endforeach()
    target_include_directories(telink_sdk_headers SYSTEM INTERFACE
        "${CMAKE_CURRENT_BINARY_DIR}" "${_chip}" "${_chip}/lib/include")
else()
    target_include_directories(telink_sdk_headers SYSTEM INTERFACE
        "${CMAKE_CURRENT_BINARY_DIR}" "${_chip}" "${_chip}/lib/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/common" "${CMAKE_CURRENT_SOURCE_DIR}/board"
        "${CMAKE_CURRENT_SOURCE_DIR}/calibration")
    target_compile_definitions(telink_sdk_headers INTERFACE MCU_CORE_TL323X=1 MCU_STARTUP_FLASH=1)
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build_config.h"
        CONTENT "#pragma once\n#define CURRENT_BUILD_tl3238_freertos 1\n" @ONLY)
    target_sources(telink_sdk PRIVATE board/common.c board/hal_driver/flash/hal_flash.c
        calibration/calibration.c common/sdk_version.c)
endif()
file(GLOB_RECURSE _drivers CONFIGURE_DEPENDS "${_chip}/*.c")
target_sources(telink_sdk PRIVATE ${_drivers})
add_library(telink_driver_blob STATIC IMPORTED GLOBAL)
set_target_properties(telink_driver_blob PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/drivers/lib/libdriver.a")
target_link_libraries(telink_sdk PUBLIC telink_driver_blob)
