/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Physical NOR offsets, independent of the ROM's active XIP bank. */
#define TL_FLASH_SIZE       0x100000u
#define TL_FLASH_SECTOR     0x1000u
#define TL_FLASH_PAGE       256u
#define TL_FLASH_BANK       0x80000u
#define TL_FLASH_IMAGE_MAX  0x70000u
#define TL_FLASH_META       0x70000u
#define TL_FLASH_B_META     (TL_FLASH_BANK + TL_FLASH_META)
#define TL_LFS_BASE         0x71000u
#define TL_LFS_SIZE         0xf000u
#define TL_LFS_BLOCKS       (TL_LFS_SIZE / TL_FLASH_SECTOR)
#define TL_LFS_CACHE        256u
#define TL_FLASH_RESERVED_BASE 0xf1000u
#define TL_FLASH_CALIB_BASE    0xfe000u
#define TL_FLASH_MAC_BASE      0xff000u

/* P25Q80SU individual-lock geometry differs from erase geometry: the first
 * and last 64 KiB use 4 KiB locks, while middle blocks use 64 KiB locks.
 * A metadata and LittleFS share the lock at 0x70000..0x80000. These constants
 * describe the current layout; they do not enable hardware write protection.
 * A resident mcumgr bootloader can reuse the A image area and place its
 * single App in B, but needs a dedicated handoff and update implementation. */

_Static_assert(TL_LFS_BASE == TL_FLASH_META + TL_FLASH_SECTOR, "LFS overlaps OTA metadata");
_Static_assert(TL_LFS_BASE + TL_LFS_SIZE == TL_FLASH_BANK, "LFS overlaps bank B");
_Static_assert(TL_FLASH_IMAGE_MAX <= TL_FLASH_META, "image overlaps metadata");
_Static_assert(TL_FLASH_B_META + TL_FLASH_SECTOR == TL_FLASH_RESERVED_BASE, "B metadata overlaps reserved tail");
_Static_assert(TL_FLASH_MAC_BASE + TL_FLASH_SECTOR == TL_FLASH_SIZE, "invalid Flash end");
_Static_assert(TL_FLASH_CALIB_BASE + TL_FLASH_SECTOR == TL_FLASH_MAC_BASE, "invalid calibration/MAC layout");
