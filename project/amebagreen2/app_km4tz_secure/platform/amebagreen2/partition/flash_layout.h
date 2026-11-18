/* Green2 4 MiB layout. ROM boot at 0, APP_ALL slots at 0x10000/0x190000,
 * LittleFS at 0x300000..0x3effff; the top 64 KiB belongs exclusively to SPE.
 * TF-M is the variable-length IMG3 tail inside each APP_ALL slot, mapped at
 * 0x0c000000. NS KM4TZ executes at 0x04000000; BL2 is disabled. */
#ifndef __FLASH_LAYOUT_H__
#define __FLASH_LAYOUT_H__

/* Physical SPI-flash programming view (used by the flash driver for ITS/PS). */
#define FLASH_BASE_ADDRESS              (0x08000000)
#define FLASH_TOTAL_SIZE                (0x00400000)   /* 4 MB */
#define FLASH_AREA_IMAGE_SECTOR_SIZE    (0x1000)       /* 4 KB */
#define FLASH_DEV_NAME                  Driver_FLASH0

/* No BL2 (AmebaGreen2 ROM/bootloader own secure boot). */
#define FLASH_AREA_BL2_OFFSET           (0x0)
#define FLASH_AREA_BL2_SIZE             (0x0)

/* Code size limits; physical S/NS offsets depend on the packed load records.
 * BL2 flash-map macros below are unused and intentionally not exposed. */
#define FLASH_S_PARTITION_SIZE          (0x60000)
#define FLASH_NS_PARTITION_SIZE         (0x160000)
#define FLASH_MAX_PARTITION_SIZE        FLASH_NS_PARTITION_SIZE

/* Secure storage partitions, shared with the image builder and SAU/MPC. */
#define FLASH_ITS_AREA_OFFSET           (0x003F0000)
#define FLASH_ITS_AREA_SIZE             (0x4000)       /* 16 KB */

#define FLASH_PS_AREA_OFFSET            (FLASH_ITS_AREA_OFFSET + FLASH_ITS_AREA_SIZE)
#define FLASH_PS_AREA_SIZE              (0xA000)       /* 40 KB (2 * 20 KB block) */

#define FLASH_OTP_NV_COUNTERS_AREA_OFFSET (FLASH_PS_AREA_OFFSET + FLASH_PS_AREA_SIZE)
#define FLASH_OTP_NV_COUNTERS_AREA_SIZE   (FLASH_AREA_IMAGE_SECTOR_SIZE * 2)
#define FLASH_OTP_NV_COUNTERS_SECTOR_SIZE FLASH_AREA_IMAGE_SECTOR_SIZE

/* Driver_FLASH0 takes physical offsets and programs/erases in secure SRAM. */
#define TFM_HAL_ITS_FLASH_DRIVER        Driver_FLASH0
/* Byte programming is handled by the bounded NOR page-program driver. */
#define TFM_HAL_ITS_PROGRAM_UNIT        (0x1)
#define TFM_HAL_ITS_FLASH_AREA_ADDR     (FLASH_ITS_AREA_OFFSET)
#define TFM_HAL_ITS_FLASH_AREA_SIZE     (FLASH_ITS_AREA_SIZE)
/* Two 8 KiB logical blocks (one data, one scratch). */
#define TFM_HAL_ITS_SECTORS_PER_BLOCK   (2)

#define TFM_HAL_PS_FLASH_DRIVER         Driver_FLASH0
#define TFM_HAL_PS_PROGRAM_UNIT         (0x1)
#define TFM_HAL_PS_FLASH_AREA_ADDR      (FLASH_PS_AREA_OFFSET)
#define TFM_HAL_PS_FLASH_AREA_SIZE      (FLASH_PS_AREA_SIZE)
/* Two 20 KiB blocks; enough for 4 KiB encrypted records plus metadata. */
#define TFM_HAL_PS_SECTORS_PER_BLOCK    (5)

#define TFM_OTP_NV_COUNTERS_AREA_SIZE   (FLASH_OTP_NV_COUNTERS_AREA_SIZE / 2)
#define TFM_OTP_NV_COUNTERS_AREA_ADDR   FLASH_OTP_NV_COUNTERS_AREA_OFFSET
#define TFM_OTP_NV_COUNTERS_SECTOR_SIZE FLASH_OTP_NV_COUNTERS_SECTOR_SIZE
#define TFM_OTP_NV_COUNTERS_BACKUP_AREA_ADDR (TFM_OTP_NV_COUNTERS_AREA_ADDR + \
                                              TFM_OTP_NV_COUNTERS_AREA_SIZE)

#endif /* __FLASH_LAYOUT_H__ */
