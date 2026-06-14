/*
 * RTL8721D (Ameba-D) KM4 — TF-M flash layout (BL2 OFF / TF-M = IMG3).
 *
 * Ameba-D SPI flash is at 0x08000000 (4 MB), executed-in-place via the KM4 XIP
 * cache window at 0x0E000000. The existing partition (see the KM4 layout ld +
 * reference_amebad_flash_layout) reserves:
 *   0x08000000 KM0 boot | 0x08004000 KM4 boot | 0x0E000000(XIP) KM4 image2(OTA1)
 *
 * For TF-M we split the KM4 image XIP region into a Secure part (TF-M SPE, low)
 * and a Non-secure part (the existing driver-only app, after it); the IDAU/SAU
 * (target_cfg.c) attribute the Secure XIP sub-range. There is NO MCUboot BL2 —
 * Ameba-D ROM authenticates IMG1 and the KM4 bootloader loads the images.
 *
 * NOTE (bring-up): these offsets are the first-pass partition for the port. The
 * S/NS XIP split + ITS/PS placement must still be reconciled with the AmebaD
 * image pipeline (component/amebad/image_tool/build_image_all.py) and RSIP; the
 * flash driver for ITS/PS is a follow-on. Sizes are sector-aligned (4 KB).
 */
#ifndef __FLASH_LAYOUT_H__
#define __FLASH_LAYOUT_H__

/* Physical SPI-flash programming view (used by the flash driver for ITS/PS). */
#define FLASH_BASE_ADDRESS              (0x08000000)
#define FLASH_TOTAL_SIZE                (0x00400000)   /* 4 MB */
#define FLASH_AREA_IMAGE_SECTOR_SIZE    (0x1000)       /* 4 KB */
#define FLASH_DEV_NAME                  Driver_FLASH0

/* No BL2 (Ameba-D ROM/bootloader own secure boot). */
#define FLASH_AREA_BL2_OFFSET           (0x0)
#define FLASH_AREA_BL2_SIZE             (0x0)

/* ---- KM4 image2 XIP region split into S (TF-M) + NS (app) ----
 * KM4 image2 XIP base = 0x0E000000. Give TF-M SPE the low 256 KB, the NS app
 * the rest. (Single image each, no MCUboot slots — BL2 OFF.) */
#define FLASH_S_PARTITION_SIZE          (0x60000)      /* 384 KB TF-M SPE (mbedcrypto) — matches region_defs S_CODE_SIZE */
#define FLASH_NS_PARTITION_SIZE         (0x160000)     /* NS app (informational; real layout is dynamic) */
#define FLASH_MAX_PARTITION_SIZE        FLASH_NS_PARTITION_SIZE

#define FLASH_AREA_0_OFFSET             (0x00004000)   /* KM4 image flash start (after KM0/KM4 boot) */
#define FLASH_AREA_0_SIZE               (FLASH_S_PARTITION_SIZE)
#define FLASH_AREA_1_OFFSET             (FLASH_AREA_0_OFFSET + FLASH_AREA_0_SIZE)
#define FLASH_AREA_1_SIZE               (FLASH_NS_PARTITION_SIZE)

#define S_IMAGE_PRIMARY_PARTITION_OFFSET   (FLASH_AREA_0_OFFSET)
#define NS_IMAGE_PRIMARY_PARTITION_OFFSET  (FLASH_AREA_1_OFFSET)

/* ---- TF-M internal/protected storage + NV counters (carved from a free
 * tail region of flash; reconcile with the AmebaD partition before use). ---- */
#define FLASH_ITS_AREA_OFFSET           (0x003F0000)
#define FLASH_ITS_AREA_SIZE             (0x4000)       /* 16 KB */

#define FLASH_PS_AREA_OFFSET            (FLASH_ITS_AREA_OFFSET + FLASH_ITS_AREA_SIZE)
#define FLASH_PS_AREA_SIZE              (0xA000)       /* 40 KB (2 * 20 KB block) */

#define FLASH_OTP_NV_COUNTERS_AREA_OFFSET (FLASH_PS_AREA_OFFSET + FLASH_PS_AREA_SIZE)
#define FLASH_OTP_NV_COUNTERS_AREA_SIZE   (FLASH_AREA_IMAGE_SECTOR_SIZE * 2)
#define FLASH_OTP_NV_COUNTERS_SECTOR_SIZE FLASH_AREA_IMAGE_SECTOR_SIZE

/* TF-M's flash-backed ITS/PS map onto the above. The *_FLASH_AREA_ADDR is the
 * RAW SPI-flash offset (NOT the 0x08000000 alias): the TF-M flash FS feeds it
 * straight to Driver_FLASH0.ReadData/ProgramData/EraseSector, and our driver
 * (cmsis_drivers/Driver_Flash.c) calls the ROM FLASH_* primitives with raw
 * offsets. SECTORS_PER_BLOCK = area_size / 4 KB sector. */
#define TFM_HAL_ITS_FLASH_DRIVER        Driver_FLASH0
#define TFM_HAL_ITS_PROGRAM_UNIT        (0x1)
#define TFM_HAL_ITS_FLASH_AREA_ADDR     (FLASH_ITS_AREA_OFFSET)
#define TFM_HAL_ITS_FLASH_AREA_SIZE     (FLASH_ITS_AREA_SIZE)
/* SECTORS_PER_BLOCK sets the FS logical block: num_blocks = area / (spb*sector).
 * The ITS flash FS requires num_blocks == 2 or >= 4 (its_flash_fs.c rejects 1 and
 * 3). 16 KB area / (2 * 4 KB) = 2 blocks of 8 KB -> valid. (Was area/sector = 4 ->
 * 1 block -> FS init fails -> SPM panic -> SPE hangs at boot.) */
#define TFM_HAL_ITS_SECTORS_PER_BLOCK   (2)

#define TFM_HAL_PS_FLASH_DRIVER         Driver_FLASH0
#define TFM_HAL_PS_PROGRAM_UNIT         (0x1)
#define TFM_HAL_PS_FLASH_AREA_ADDR      (FLASH_PS_AREA_OFFSET)
#define TFM_HAL_PS_FLASH_AREA_SIZE      (FLASH_PS_AREA_SIZE)
/* 40 KB area / (5 * 4 KB) = 2 blocks of 20 KB -> valid, and stays on the
 * HW-verified num_blocks==2 branch (data colocated with metadata in block 0,
 * block 1 = scratch). Usable PS data ~= one 20 KB block minus metadata (~19.5 KB)
 * -> holds several AWS-IoT-cert-sized objects with margin. Block size (20 KB)
 * must exceed PS_MAX_OBJECT_SIZE (header + PS_MAX_ASSET_SIZE 4 KB + tag ~= 4.2 KB);
 * 20 KB >> 4.2 KB. (Was 20 KB area / SPB=2 -> 2*8 KB blocks, 4 KB wasted, ~7.5 KB
 * usable.) */
#define TFM_HAL_PS_SECTORS_PER_BLOCK    (5)

#define TFM_OTP_NV_COUNTERS_AREA_SIZE   (FLASH_OTP_NV_COUNTERS_AREA_SIZE / 2)
#define TFM_OTP_NV_COUNTERS_AREA_ADDR   FLASH_OTP_NV_COUNTERS_AREA_OFFSET
#define TFM_OTP_NV_COUNTERS_SECTOR_SIZE FLASH_OTP_NV_COUNTERS_SECTOR_SIZE
#define TFM_OTP_NV_COUNTERS_BACKUP_AREA_ADDR (TFM_OTP_NV_COUNTERS_AREA_ADDR + \
                                              TFM_OTP_NV_COUNTERS_AREA_SIZE)

#endif /* __FLASH_LAYOUT_H__ */
