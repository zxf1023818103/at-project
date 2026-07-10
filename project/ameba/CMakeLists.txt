add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src
                 ${CMAKE_BINARY_DIR}/src)
# Cross-platform third-party libs, each in its own component/<name>_config
# (submodule + wrapper). This build doesn't go through the repo-root CMakeLists,
# so the universal ones are added here (out-of-tree adds need explicit binary dirs).
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../component/zlib_config
                 ${CMAKE_BINARY_DIR}/component/zlib_config)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../component/cjson_config
                 ${CMAKE_BINARY_DIR}/component/cjson_config)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../component/littlefs_config
                 ${CMAKE_BINARY_DIR}/component/littlefs_config)
# Shared cross-platform stdio console dispatcher (component/console). The ameba
# build doesn't go through the repo-root CMakeLists, so add it here.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../component/console
                 ${CMAKE_BINARY_DIR}/component/console)

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
        ${CMAKE_CURRENT_LIST_DIR}/ameba_console_shim.c   # shared-console adoption (--wrap _read/_write)
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
# at_console: the shared fd 0/1/2 dispatcher (console.h + at_console_read/write),
# backed by ameba_console_shim.c's __real-based LOGUART backend.
target_link_libraries(${c_CURRENT_TARGET_NAME} PRIVATE zlibstatic at_console)

# The Bouffalo / Ameba-newer platform PORT COMPONENT (component/at_app_port/at_app_port.cpp) compiled as
# its own TU; its Ameba-newer branch provides app_pre_example. AT_SDK_OWNS_MAIN tells the neutral
# src/main.cpp NOT to emit main() -- the Realtek SDK provides it for these chips.
# NOTE (Ameba-newer follow-up): this TU pulls Bouffalo/AmebaD component headers (board_flash_lock.h, ...)
# that are not yet on the ameba include path; add flash_cli_config etc. before this builds clean.
target_sources(${c_CURRENT_TARGET_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../component/at_app_port/at_app_port.cpp)
target_compile_definitions(${c_CURRENT_TARGET_NAME} PRIVATE AT_SDK_OWNS_MAIN)

# Route newlib's console syscalls through the shared dispatcher without editing the
# SDK submodule: wrap _read/_write at the final link so they resolve to the
# __wrap_* in ameba_console_shim.c (the SDK's originals stay as __real_*). INTERFACE
# so the option lands on the final image link (mirrors bflb_libc's -Wl,-wrap,*).
target_link_options(${c_CURRENT_TARGET_NAME} INTERFACE
    -Wl,--wrap=_read
    -Wl,--wrap=_write)
