# lfs_easyflash — EasyFlash KV API implemented on top of upstream
# littlefs. Extracted from
# bouffalo_sdk/components/fs/littlefs/easyflash_port/.
#
# Sits at component/ top-level (sibling to component/lwip_dhcpd) because
# the KV-on-files mapping is platform-neutral — only the block-device
# layer underneath needs to be chip-specific. Today the only block-device
# backend in the tree is bflb_littlefs_port (Bouffalo XIP flash); when
# the Ameba adapters land, they can plug into the same lfs_easyflash
# above them. See doc/kv.md for the cross-platform /KV/<name> layout
# this honours so that one lfs.bin works across all three vendors.
#
# What it provides:
#   easyflash.h API   ef_get_env_blob / ef_set_env_blob / EfErrCode / …
#   internal init     wires lfs_xip_init + sets up a /KV namespace
#
# Headline implementation choice: each key becomes a single file at
# /KV/<name> on the mounted lfs. LFS_EF_NAMESPACE overrides the prefix
# at build time if a project wants a different root.

add_library(lfs_easyflash STATIC
    lfs_easyflash.c
)

target_include_directories(lfs_easyflash PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Direct deps: the lfs block-device backend (today Bouffalo's
# bflb_littlefs_port, which also pulls in upstream littlefs transitively),
# bflb_log for the DBG_TAG=LFS prints, FreeRTOS for the env_giant_lock.
#
# When we add an Ameba backend, the right move is to either swap
# bflb_littlefs_port for the right backend at link time or make this
# CMakeLists pick one based on a CONFIG_* gate. Punt on that until the
# second backend actually exists.
target_link_libraries(lfs_easyflash PUBLIC
    bflb_littlefs_port
    bflb_log
    freertos_kernel
)
