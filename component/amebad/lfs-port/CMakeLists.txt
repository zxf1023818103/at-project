# component/amebad/lfs-port — AmebaD (RTL8721D) flash adapter for LittleFS:
# the lfs_config diskio callbacks over the SDK flash HAL + a mount helper.
# KM4-only (the filesystem + its consumers live on the HP core).
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

# lfs_selftest.c lives here (not app_km4) so ALL LittleFS .text is in this one
# archive — the ld script (rlx8721d_img2_is.ld) pins libamebad_lfs_port.a /
# liblfs_config.a / liblittlefs.a to the tail of .xip_image2.text to keep the
# boot + WiFi code at its known-good XIP addresses (see that file's comment).
add_library(amebad_lfs_port STATIC EXCLUDE_FROM_ALL lfs_port.c lfs_selftest.c cpp_selftest.cpp)
target_include_directories(amebad_lfs_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# PUBLIC: lfs.h (littlefs) + the geometry/param fill (lfs_config) are part of the
# port's own API surface (lfs_port.h includes lfs.h). PRIVATE: the flash HAL +
# SDK headers (flash_api.h via amebad_mbed, device_lock.h + FreeRTOS via
# amebad_sdk_includes) are implementation detail.
target_link_libraries(amebad_lfs_port PUBLIC  littlefs lfs_config)
target_link_libraries(amebad_lfs_port PRIVATE amebad_sdk_includes amebad_mbed)

# Optional POSIX-fd VFS self-test (vfs_selftest, gated in lfs_selftest.c). Needs
# FreeRTOS_Sockets.h (amebad_freertos_tcp's PUBLIC includes) + the define; both are
# compile-time only here (the archive is already in app_km4's link group).
if(AMEBAD_VFS_SELFTEST)
    target_compile_definitions(amebad_lfs_port PRIVATE AMEBAD_VFS_SELFTEST)
    target_link_libraries(amebad_lfs_port PRIVATE amebad_freertos_tcp)
endif()

# Optional C++ std::filesystem / iostream self-test (cpp_selftest.cpp, gated in
# that file). It is in THIS archive so its .text — and, via the ld pin, the
# libstdc++ .text it pulls — lands at the XIP tail and does not shift boot/WiFi.
# C++17 / -lstdc++ are already configured for the KM4 image.
if(AMEBAD_CPP_SELFTEST)
    target_compile_definitions(amebad_lfs_port PRIVATE AMEBAD_CPP_SELFTEST)
endif()
