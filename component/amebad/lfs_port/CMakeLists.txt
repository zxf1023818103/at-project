# component/amebad/lfs_port — AmebaD (RTL8721D) flash adapter for LittleFS: the
# four lfs_config diskio callbacks over the SDK flash HAL + the thin amebad_lfs_mount
# wrappers that delegate to the shared lfs_config mount lifecycle, plus vfs_lfs_mount
# (the syscall_config VFS hook). KM4-only (the filesystem + its consumers are HP-core).
#
# The portable LittleFS self-tests (lfs_selftest.c + cpp_selftest.cpp) moved to the
# shared component/lfs_config; their .text rides liblfs_config.a, which the ld script
# (rlx8721d_img2_is.ld) already pins to the .xip_image2.text tail alongside
# liblfs_port.a / liblittlefs.a — so the boot/WiFi addresses stay put.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(lfs_port STATIC EXCLUDE_FROM_ALL lfs_port.c)
target_include_directories(lfs_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# PUBLIC: lfs.h (littlefs) + the geometry/param fill + mount lifecycle (lfs_config)
# are part of the port's own API surface (lfs_port.h includes lfs.h). PRIVATE: the
# flash HAL + SDK headers (flash_api.h via amebad_mbed, device_lock.h + FreeRTOS via
# amebad_sdk_includes) are implementation detail.
target_link_libraries(lfs_port PUBLIC  littlefs lfs_config)
target_link_libraries(lfs_port PRIVATE amebad_sdk_includes amebad_mbed)

# (The LittleFS self-test gates that attach onto the shared lfs_config — VFS + C++ — moved to the
# consumer that owns the options, project/amebad/app_km4, per littlefs_config's design comment.
# lfs_port stays a pure library: just the diskio callbacks + mount wrappers.)
