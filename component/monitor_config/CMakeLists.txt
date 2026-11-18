# component/monitor_config — the cross-platform interactive console.
#
#   monitor       the FreeRTOS-Plus-CLI engine: the line editor + FreeRTOS_CLIProcessCommand
#                 loop + the generic version/heap/heapreset/tasks/echo commands. All byte
#                 I/O goes through component/console (at_console_write_raw + the platform RX
#                 seam), so it is platform-neutral. Serves AmebaD (KM0+KM4) and Bouffalo.
#   monitor_cmds  the portable command modules (fs/env/time) — added by this file too (below).
#
# The monitor.h seam (monitor_port_init / monitor_core_name / monitor_rx_get /
# monitor_after_command) is satisfied by each platform's component/<platform>/monitor_port
# at the final image link (amebad: RX-IRQ + KM0/KM4 hand-off + reboot + ROM cmds; bl602: poll
# RX + reboot). Platforms register their command modules (fs/env/time + platform-specific)
# after monitor_start() via the *_cli_register() functions.

add_library(monitor STATIC EXCLUDE_FROM_ALL
    monitor.c
)
target_include_directories(monitor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")   # monitor.h
# FreeRTOS (+ FreeRTOSConfig.h) + the CLI core + the console dispatcher. The monitor.h seam
# symbols (monitor_port_init/…) resolve at the image link from the platform monitor_port.
target_link_libraries(monitor PUBLIC FreeRTOS freertos_cli at_console)

# monitor_cmds — the portable console command modules, registered via the *_cli_register()
# functions from each platform's main.c after monitor_start():
#   fs_cli.c    ls/cat/rm/rmdir/mkdir/mv/cp/touch/hexdump/df/mkfs/chmod + md5/sha*sum
#   env_cli.c   setenv/getenv/env/unsetenv
#   time_cli.c  date/uptime/sntp_update
# All over the shared POSIX VFS + <dirent.h> + env + time (component/syscall_config) and
# littlefs. Portable now that Bouffalo shares that libc. (mkfs uses the shared lfs_config
# format path; a platform whose VFS is not backed by lfs_config needs its own format hook.)
add_library(monitor_cmds STATIC EXCLUDE_FROM_ALL
    fs_cli.c
    env_cli.c
    time_cli.c
)
target_link_libraries(monitor_cmds PUBLIC
    FreeRTOS
    freertos_cli        # FreeRTOS_CLI.h + FreeRTOS_CLIRegisterCommand
    at_console          # at_console_write_raw (fs_cli streaming output)
    littlefs            # lfs.h + lfs_fs_size (df) / lfs_setattr (chmod)
    lfs_config          # lfs_config.h geometry (LFS_CFG_*) + lfs_config_format (mkfs)
)
# syscall_config is an OBJECT lib (force-included at the image link), so take only its headers
# (<dirent.h> + syscalls.h: vfs_lfs_mount / amebad_time_is_synced) here — linking it would
# archive its objects into this static lib and double-define them at the image link.
target_include_directories(monitor_cmds PRIVATE
    "$<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>")

# fs_cli's md5sum/sha*sum use upstream Mbed TLS (mbedcrypto). Default ON; a platform that links
# an ABI-incompatible mbedTLS instead (AmebaD ROMKAT = ROM mbedTLS 2.4.0, same symbol names)
# sets AT_FS_CLI_HASH=OFF before adding this component.
if(NOT DEFINED AT_FS_CLI_HASH)
    set(AT_FS_CLI_HASH ON)
endif()
if(AT_FS_CLI_HASH)
    set_property(SOURCE fs_cli.c APPEND PROPERTY COMPILE_DEFINITIONS FS_CLI_HASH=1)
    target_link_libraries(monitor_cmds PUBLIC mbedcrypto)
endif()

target_compile_options(monitor_cmds PRIVATE -Wno-unused-parameter -Wno-unused-variable)

# zip_cmd — OPTIONAL zip/unzip console commands (minizip over the POSIX VFS). Kept separate from
# monitor_cmds because it pulls minizip (~50 KB working set), so a platform links it only where the
# heap allows: bl602 (big TLSF heap) always; AmebaD only in the lighter non-AWS/QUIC images.
# Registered via zip_cli_register(). minizip's Z_BUFSIZE=2048 is set in component/zlib_config.
add_library(zip_cmd STATIC EXCLUDE_FROM_ALL zip_cli.c)
target_link_libraries(zip_cmd PUBLIC
    FreeRTOS
    freertos_cli        # FreeRTOS_CLI.h
    at_console          # at_console_write_raw (streaming output)
    minizip             # zip.h / unzip.h
    zlibstatic          # zlib (deflate)
)
target_compile_options(zip_cmd PRIVATE -Wno-unused-parameter -Wno-unused-variable)
