# component/dsleep_config - the cross-platform `dsleep [uart] [rtc <seconds>]` command: a full
# low-power sleep that wakes on a UART-RX edge and/or an RTC/timer timeout, over the platform
# primitive board_dsleep_enter (Bouffalo board_dsleep_port = bl616 PDS15 / bl602 PDS_Mode_Enter;
# AmebaD board_dsleep_port = SleepCG retention). app_console_register_platform() calls
# dsleep_cli_register(). Mirrors flash_cli_config. EXCLUDE_FROM_ALL - costs nothing unlinked.

# Header-only contract (dsleep.h): the per-platform board_dsleep_port impls (board_dsleep_enter) need
# it WITHOUT pulling dsleep_cli.c (the command); the impl symbol resolves at the final image link.
# Link this instead of reaching into this dir by path.
add_library(board_dsleep INTERFACE)
target_include_directories(board_dsleep INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(dsleep_cli STATIC EXCLUDE_FROM_ALL dsleep_cli.c)
target_include_directories(dsleep_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# FreeRTOS_CLI.h (the command) + FreeRTOS.h; freertos_cli brings both transitively. Both platforms
# already link it for the shared console.
target_link_libraries(dsleep_cli PUBLIC freertos_cli)
