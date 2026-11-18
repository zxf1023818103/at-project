# component/ota_flash_config - the shared OTA slot-writer interface (ota_flash.h): the streaming
# begin/feed/finish/reboot + the ota_lock_* mutual exclusion that ota_http / ota_pal (AWS) / smp
# (mcumgr) all drive. Header-only: each platform provides the strong implementation of the interface
# (AmebaD project/amebad/app_km4/ota_flash.c over flash_api; Bouffalo bouffalo_sdk/ota_flash_port over
# bflb_flash + pt_table). The firmware-region unlock/relock around the write burst is the shared
# board_flash_* (component/flash_cli_config). EXCLUDE_FROM_ALL cost model: header-only INTERFACE.
add_library(ota_flash_config INTERFACE)
target_include_directories(ota_flash_config INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
