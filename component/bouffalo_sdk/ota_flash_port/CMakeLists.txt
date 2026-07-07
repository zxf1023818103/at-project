# component/bouffalo_sdk/ota_flash_port - BL602 impl of the shared OTA slot writer (ota_flash.h) over
# bflb_flash + pt_table, with inline xz decompress. The xz-embedded decoder is pulled from the SDK tree
# (platform/bouffalo_sdk/components/lzma/xz), same sources boot2 uses. Linked into the bouffalo_sdk
# umbrella; the shared ota_http (compiled into the app) drives ota_flash_begin/feed/finish.
set(_xz_dir "${AT_ROOT}/platform/bouffalo_sdk/components/lzma/xz")

add_library(ota_flash_port STATIC
    ota_flash_bl602.c
    "${_xz_dir}/xz_crc32.c"
    "${_xz_dir}/xz_dec_lzma2.c"
    "${_xz_dir}/xz_dec_stream.c"
    "${_xz_dir}/xz_port.c")

target_include_directories(ota_flash_port PRIVATE
    "${_xz_dir}"                                          # xz.h / xz_config.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../ota_flash_config"  # ota_flash.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../flash_cli_config"  # board_flash_lock.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../bflb_partition"       # partition.h
    "${CMAKE_CURRENT_SOURCE_DIR}/../../rxi_log_config")   # at_log.h

# bflb_flash.h (LHAL) + FreeRTOS (ota_lock critical sections) + rxi log. bl_sys_reset_por / the
# pt_table + bflb_flash symbols resolve from the umbrella group at the image link.
target_link_libraries(ota_flash_port PRIVATE bflb_lhal FreeRTOS rxi_log)

# xz-embedded is vendored 3rd-party; silence its warnings. board_flash_lock.h basename map for the port.
target_compile_options(ota_flash_port PRIVATE -w)
