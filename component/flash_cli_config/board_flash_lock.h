/*
 * board_flash_lock.h - one cross-platform interface for SPI-NOR firmware write-protect.
 *
 * The bottom 3 MB of flash (boot + firmware image / OTA slots) is the "firmware" region; the top 1 MB
 * (LittleFS, and on AmebaD TF-M ITS) stays writable. Protection is the chip status-register BP/CMP
 * bits (SR1=0x14 + SR2 CMP=1 => bottom 3 MB locked; SR1=0x1C => whole chip). Both platforms drive the
 * SAME W25Q/GD25-family layout, so the values are identical - only the raw SR read/write differs
 * (AmebaD flash_api / FLASH_*; Bouffalo bflb_sflash). Each platform implements the two primitives
 * below; the helpers + the `flash` console command + the boot status print are shared (flash_cli.c).
 */
#ifndef BOARD_FLASH_LOCK_H
#define BOARD_FLASH_LOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_FLASH_UNLOCK   = 0,   /* clear all protection                          */
    BOARD_FLASH_LOCK_FW  = 1,   /* lock the firmware region (bottom 3 MB)         */
    BOARD_FLASH_LOCK_ALL = 2,   /* lock the whole chip                           */
} board_flash_prot_t;

/* ---- platform primitives (one implementation per platform) ---------------------------------- */

/* Apply `mode`. Optional out-params (NULL to ignore) report the before/after SR1/SR2 bytes. Returns
 * 1 if the write verified, else 0. Serializes against XIP internally (AmebaD wraps FLASH_Write_Lock). */
int board_flash_protect(board_flash_prot_t mode,
                        uint8_t *sr1_before, uint8_t *sr1_after,
                        uint8_t *sr2_before, uint8_t *sr2_after);

/* Read the current SR1/SR2 without changing protection. Returns 1 on success. */
int board_flash_read_status(uint8_t *sr1, uint8_t *sr2);

/* ---- shared helpers + console command (flash_cli.c) ----------------------------------------- */

void board_flash_lock_fw(void);   /* board_flash_protect(BOARD_FLASH_LOCK_FW) */
void board_flash_unlock(void);    /* board_flash_protect(BOARD_FLASH_UNLOCK)  */

/* 1 if the firmware region is currently write-protected (SR1=0x14 & SR2 CMP, or SR1=0x1C). */
int  board_flash_is_fw_locked(void);

/* Log the current lock state (called once at boot on both platforms). */
void board_flash_log_status(void);

/* Register the shared `flash` console command (lock / unlock / status). AmebaD keeps its raw
 * erase/read/write/status/cache debug ops under a separate `flashdbg` command. */
void flash_cli_register(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_FLASH_LOCK_H */
