# component/flash_cli_config - the cross-platform firmware flash write-protect: the lock/unlock/status
# helpers, the boot status print (board_flash_log_status), and the shared `flash` console command, all
# over the platform primitives board_flash_protect / board_flash_read_status (AmebaD monitor_port;
# Bouffalo board_flash_bouffalo). main() calls board_flash_lock_fw() + board_flash_log_status() at boot;
# app_console_register_platform() calls flash_cli_register(). EXCLUDE_FROM_ALL - costs nothing unlinked.
# rxi log lines print __FILE__: map this dir's prefix away so they show basenames.
add_compile_options("-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")

# Header-only contract (board_flash_lock.h): the per-platform board_flash_port impls
# (board_flash_protect / board_flash_read_status) and the shared ota_flash_port unlock/relock both
# need it WITHOUT pulling flash_cli.c (the command) or each other — the impl symbols resolve at the
# final image link. Mirrors ota_flash_config. Link this instead of reaching into this dir by path.
add_library(board_flash_lock INTERFACE)
target_include_directories(board_flash_lock INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(flash_cli STATIC EXCLUDE_FROM_ALL flash_cli.c)
target_include_directories(flash_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# FreeRTOS_CLI.h (the command) + FreeRTOS.h; at_log.h (rxi log). freertos_cli brings FreeRTOS + the CLI
# headers transitively; rxi_log the logger. Both platforms already link these for the shared console.
target_link_libraries(flash_cli PUBLIC freertos_cli rxi_log)
