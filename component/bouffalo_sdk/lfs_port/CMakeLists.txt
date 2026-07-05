# component/bouffalo_sdk/lfs_port — Bouffalo XIP-flash block-device backend for
# upstream littlefs (target lfs_port), plus the shared fd-VFS mount hook
# vfs_lfs_mount(). Mirrors component/amebad/lfs_port (same target name: lfs_port). Extracted
# from bouffalo_sdk/components/fs/littlefs/port/.
#
# What it provides:
#   lfs_xip_flash_{read,prog,erase,sync}   the lfs_config callbacks
#   lfs_xip_init                            convenience initializer
#   struct lfs_context                      lfs_t + per-fs lock + flash
#                                           addr / partition name
#   <lfs_port.h>                            the public header above
#
# Backing storage:
#   - If CONFIG_LITTLEFS_FLASH_ADDRESS is defined, the fs uses a hardcoded
#     absolute flash offset (also requires LITTLEFS_FLASH_SIZE).
#   - Otherwise lfs_xip_init resolves the partition by name (default
#     "PSM") via bflb_partition / bflb_mtd at runtime.
#
# CONFIG_FREERTOS enables LFS_THREADSAFE + the per-fs giant-lock
# semaphore. Off for bare-metal — none of our build paths exercise that.

add_library(lfs_port STATIC
    lfs_xip_flash.c
    vfs_lfs_mount.c   # the shared fd-VFS mount hook (was easyflash_init's mount)
)

target_include_directories(lfs_port PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Upstream littlefs needs LFS_THREADSAFE defined to compile the locking
# hooks; same flag flips bflb's giant-lock paths on. Always on in our
# build (every consumer here is on FreeRTOS).
target_compile_definitions(lfs_port PUBLIC
    LFS_THREADSAFE
)

# bflb_flash / bflb_l1c / bflb_mtd come from lhal + bflb_mtd; lfs.h from
# upstream littlefs; log.h from bflb_log; FreeRTOS from the kernel.
target_link_libraries(lfs_port PUBLIC
    littlefs
    bflb_lhal
    bflb_mtd
    bflb_log
    freertos_kernel
)
