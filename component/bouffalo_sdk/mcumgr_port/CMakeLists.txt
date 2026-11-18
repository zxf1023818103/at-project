# component/bouffalo_sdk/mcumgr_port — bl602 half of the mcumgr SMP-over-BLE DFU (MCUMGR=ON).
#
# The mcumgr integration splits into a platform-agnostic core (component/mcumgr_config: the mgmt/smp
# protocol + img/os/fs command groups) and a per-platform port. AmebaD's port is mcumgr_config's own
# mcumgr_port (smp_bt.c) + the SHARED img/os/fs impls (component/mcumgr_config/smp_img_ota.c + smp_fs.c).
# Those impl files are platform-AGNOSTIC — they ride the shared ota_flash.h slot writer (bl602
# implements it in ota_flash_port/ota_flash_bouffalo.c: ota_flash_begin/feed/finish + the OTA_SRC_BLE
# lock) and the LittleFS POSIX VFS (syscall_config) — so bl602 compiles the SAME files by path here
# rather than copying them, mirroring how bl602's nimble_port compiles the shared nimble_app.c by path
# instead of linking the amebad-gated nimble_config lib.
#
# Sources:
#   smp_bt.c          the SMP GATT service + BLE transport (component/mcumgr_config; registered with the
#                     NimBLE host by nimble_app.c under AT_PROJECT_MCUMGR — same shared call site as AmebaD)
#   smp_img_ota.c     img_mgmt + os_mgmt impls -> ota_flash slot writer (component/mcumgr_config, shared)
#   smp_fs.c          fs_mgmt impl over the LittleFS POSIX VFS (component/mcumgr_config, shared)
#   mcumgr_reset_bflb.c   bl602 sys_reset() shim (smp_img_ota.c reboots via sys_reset, an AmebaD symbol)
#
# Built only for AT_ENABLE_MCUMGR; EXCLUDE_FROM_ALL until the bouffalo_sdk umbrella links it into the
# image inside its --start-group (cross-refs: mcumgr <-> tinycbor, mcumgr_port -> mcumgr/nimble/
# ota_flash_port/syscall_config resolve at the final link).

add_library(mcumgr_port STATIC EXCLUDE_FROM_ALL
    "${AT_ROOT}/component/mcumgr_config/smp_bt.c"
    "${AT_ROOT}/component/mcumgr_config/smp_img_ota.c"
    "${AT_ROOT}/component/mcumgr_config/smp_fs.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mcumgr_reset_bflb.c"
)

# Include-only deps (NOT link edges): the archives ride the umbrella --start-group, so a link dep on
# the *_config umbrellas here would form a target-dependency cycle with the group. Genex target
# properties resolve at generate time, so referencing `nimble` (defined later in bouffalo_sdk) is fine.
target_include_directories(mcumgr_port PRIVATE
    $<TARGET_PROPERTY:mcumgr,INTERFACE_INCLUDE_DIRECTORIES>            # mgmt/smp + img/os/fs headers + mcumgr_compat.h + tinycbor/ prefix
    $<TARGET_PROPERTY:nimble,INTERFACE_INCLUDE_DIRECTORIES>           # host/ble_hs.h, ble_gatt, ble_att, os_mbuf, syscfg
    $<TARGET_PROPERTY:ota_flash_config,INTERFACE_INCLUDE_DIRECTORIES> # ota_flash.h
    $<TARGET_PROPERTY:rxi_log_config,INTERFACE_INCLUDE_DIRECTORIES>   # at_log.h
    $<TARGET_PROPERTY:FreeRTOS,INTERFACE_INCLUDE_DIRECTORIES>         # FreeRTOS.h / task.h
    $<TARGET_PROPERTY:syscall_config,INTERFACE_INCLUDE_DIRECTORIES>   # smp_fs.c: <fcntl.h>/<unistd.h> VFS decls
)

# -w: the shared sources carry warnings the bl602 flag stack would trip (they target AmebaD's -w'd
# build). Basename maps so rxi log lines show plain filenames, not the AmebaD/mcumgr_config paths.
target_compile_options(mcumgr_port PRIVATE -w
    "-fmacro-prefix-map=${AT_ROOT}/project/amebad/app_km4/="
    "-fmacro-prefix-map=${AT_ROOT}/component/mcumgr_config/="
    "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=")
