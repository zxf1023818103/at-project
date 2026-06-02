add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src
                 ${CMAKE_BINARY_DIR}/src)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../component/thirdparty
                 ${CMAKE_BINARY_DIR}/component/thirdparty)

set(private_compile_options
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=unused-function>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)

set(private_definitions KERNEL=1)

set(private_includes
    ${AT_USER_SRC_DIR}
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

ameba_add_internal_library(at_user
    p_SOURCES
        ${AT_USER_SOURCES}
    p_INCLUDES
        ${private_includes}
    p_DEFINITIONS
        ${private_definitions}
    p_COMPILE_OPTIONS
        ${private_compile_options}
)

# Pull zlib's headers and static archive into at_user. ameba_add_internal_library
# wraps the real target name into ${c_CURRENT_TARGET_NAME} (e.g.
# "at_user_target_img2_km4tz"); target_link_libraries on the bare "at_user"
# fails because that name isn't a real target.
target_link_libraries(${c_CURRENT_TARGET_NAME} PRIVATE zlibstatic)
