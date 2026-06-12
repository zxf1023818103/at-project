# component/amebad/lfs_port — AmebaD (RTL8721D) flash adapter for LittleFS: the
# four lfs_config diskio callbacks over the SDK flash HAL + the thin amebad_lfs_mount
# wrappers that delegate to the shared lfs_config mount lifecycle, plus vfs_lfs_mount
# (the syscall_config VFS hook). KM4-only (the filesystem + its consumers are HP-core).
#
# The portable LittleFS self-tests (lfs_selftest.c + cpp_selftest.cpp) moved to the
# shared component/lfs_config; their .text rides liblfs_config.a, which the ld script
# (rlx8721d_img2_is.ld) already pins to the .xip_image2.text tail alongside
# libamebad_lfs_port.a / liblittlefs.a — so the boot/WiFi addresses stay put.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(amebad_lfs_port STATIC EXCLUDE_FROM_ALL lfs_port.c)
target_include_directories(amebad_lfs_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# PUBLIC: lfs.h (littlefs) + the geometry/param fill + mount lifecycle (lfs_config)
# are part of the port's own API surface (lfs_port.h includes lfs.h). PRIVATE: the
# flash HAL + SDK headers (flash_api.h via amebad_mbed, device_lock.h + FreeRTOS via
# amebad_sdk_includes) are implementation detail.
target_link_libraries(amebad_lfs_port PUBLIC  littlefs lfs_config)
target_link_libraries(amebad_lfs_port PRIVATE amebad_sdk_includes amebad_mbed)

# AmebaD LittleFS self-test gates, attached onto the shared lfs_config (which carries
# lfs_selftest.c always + cpp_selftest.cpp on demand — see component/lfs_config). Kept
# here, in the AmebaD lfs component, not in the component/amebad umbrella. Both are
# compile-time only; the liblfs_config.a archive is already in app_km4's link group.
#   VFS self-test: FreeRTOS+TCP sockets (amebad_freertos_tcp) + syscall_config's syscalls.h.
#   C++ self-test: adds cpp_selftest.cpp (CXX, XIP-tail-pinned via liblfs_config.a).
if(AMEBAD_VFS_SELFTEST)
    target_compile_definitions(lfs_config PRIVATE AMEBAD_VFS_SELFTEST)
    target_link_libraries(lfs_config PRIVATE amebad_freertos_tcp)
    target_include_directories(lfs_config PRIVATE "${CMAKE_SOURCE_DIR}/component/syscall_config")
endif()
if(AMEBAD_CPP_SELFTEST)
    target_compile_definitions(lfs_config PRIVATE AMEBAD_CPP_SELFTEST)
    target_sources(lfs_config PRIVATE "${CMAKE_SOURCE_DIR}/component/lfs_config/cpp_selftest.cpp")
endif()
