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
add_library(amebad_lfs_port STATIC EXCLUDE_FROM_ALL lfs_port.c lfs_selftest.c)
target_include_directories(amebad_lfs_port PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# PUBLIC: lfs.h (littlefs) + the geometry/param fill (lfs_config) are part of the
# port's own API surface (lfs_port.h includes lfs.h). PRIVATE: the flash HAL +
# SDK headers (flash_api.h via amebad_mbed, device_lock.h + FreeRTOS via
# amebad_sdk_includes) are implementation detail.
target_link_libraries(amebad_lfs_port PUBLIC  littlefs lfs_config)
target_link_libraries(amebad_lfs_port PRIVATE amebad_sdk_includes amebad_mbed)
