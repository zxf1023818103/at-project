# component/bouffalo_sdk/ota_flash_port - Bouffalo (BL602/BL616/BL616CL) impl of the shared OTA slot
# writer (ota_flash.h) over bflb_flash + pt_table, with inline xz decompress. The xz-embedded decoder
# is pulled from the SDK tree (platform/bouffalo_sdk/components/lzma/xz), same sources boot2 uses.
# Linked into the bouffalo_sdk umbrella; the shared ota_http (compiled into the app) drives
# ota_flash_begin/feed/finish (bl616 HTTPS OTA runs end-to-end through this file too).
set(_xz_dir "${AT_ROOT}/platform/bouffalo_sdk/components/lzma/xz")

add_library(ota_flash_port STATIC
    ota_flash_bouffalo.c
    "${AT_ROOT}/component/ota_flash_config/ota_lock.c"   # shared OTA mutual-exclusion lock
    "${_xz_dir}/xz_crc32.c"
    "${_xz_dir}/xz_dec_lzma2.c"
    "${_xz_dir}/xz_dec_stream.c"
    "${_xz_dir}/xz_port.c")

# Cross-component headers come via target_link_libraries (below); only the SDK xz tree and the
# bouffalo_sdk-internal bflb_partition are reached by path.
target_include_directories(ota_flash_port PRIVATE
    "${_xz_dir}"                                     # xz.h / xz_config.h (SDK tree)
    "${CMAKE_CURRENT_SOURCE_DIR}/../bflb_partition")  # partition.h (bouffalo_sdk-internal)

# ota_flash.h (shared contract) + board_flash_lock.h (unlock/relock) via their header targets;
# bflb_flash.h (LHAL) + FreeRTOS (ota_lock critical sections) + rxi log. bl_sys_reset_por / pt_table /
# bflb_flash symbols resolve from the umbrella group at the image link.
target_link_libraries(ota_flash_port PRIVATE ota_flash_config board_flash_lock bflb_lhal FreeRTOS rxi_log)

# xz-embedded is vendored 3rd-party; silence its warnings. The prefix-map strips the absolute build
# path from __FILE__ so the rxi log shows just "ota_flash_bouffalo.c:NNN" (same idiom as the other
# components, see the feedback_log_basename_prefix_map memory).
target_compile_options(ota_flash_port PRIVATE -w "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")
