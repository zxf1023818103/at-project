# component/amebad/ota_flash_port — AmebaD (RTL8721D KM4) impl of the shared OTA slot writer
# (component/ota_flash_config, ota_flash.h) over the Realtek flash HAL: detects the inactive OTA
# slot, validates the 32-byte OTA header + checksum, writes the boot signature last, runs the
# KM0-XIP spin-park during page programs. Consumers (shared ota_http / the AWS OTA PAL /
# mcumgr smp_img_ota) call ota_flash_begin/feed/finish — strong refs, so the archive member is
# pulled wherever any of them links. Mirrors component/bouffalo_sdk/ota_flash_port. KM4-only.
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

add_library(ota_flash_port STATIC ota_flash_amebad.c)

# ota_flash.h (the shared contract) + board_flash_lock.h (unlock/relock around slot writes) +
# the SDK flash HAL headers + FreeRTOS (task delays) + at_log.h.
target_link_libraries(ota_flash_port PUBLIC ota_flash_config)
target_link_libraries(ota_flash_port PRIVATE amebad_sdk_includes rxi_log FreeRTOS board_flash_lock)  # board_flash_lock.h
