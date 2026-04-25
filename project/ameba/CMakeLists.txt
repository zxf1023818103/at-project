set(private_sources)                 #private source files, NOTE: relative path is OK
set(private_includes)                #private include directories, NOTE: relative path is OK
set(private_definitions)             #private definitions
set(private_compile_options)         #private compile_options

ameba_list_append(private_compile_options -std=c++17 -Wno-error=unused-function -Wno-register)

ameba_list_append(private_definitions KERNEL=1)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../src)

file(GLOB private_sources
    CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.c"
    RELATIVE ${CMAKE_CURRENT_LIST_DIR}
    LIST_DIRECTORIES false)

ameba_list_append(private_includes
    ${SRC_DIR}
    ${c_FREERTOS_DIR}/include
    ${c_FREERTOS_DIR}/portable/GCC/ARM_CA7
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/inc/app
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/inc/platform
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/inc/os
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/inc/stack
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/inc/bluetooth/gap
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/inc/bluetooth/profile
    ${c_CMPT_BLUETOOTH_DIR}/rtk_stack/platform/${c_SOC_TYPE}/lib
    ${c_CMPT_BLUETOOTH_DIR}/api/rtk_stack
    ${c_CMPT_BLUETOOTH_DIR}/rtk_coex
)

ameba_add_internal_library(at
    p_SOURCES
        ${private_sources}
    p_INCLUDES
        ${private_includes}
    p_DEFINITIONS
        ${private_definitions}
    p_COMPILE_OPTIONS
        ${private_compile_options}
)
