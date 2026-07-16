/*
 * board_flash_amebad.c — the AmebaD impl of the cross-platform firmware write-protect primitive
 * (component/flash_cli_config, board_flash_lock.h), mirroring bouffalo's board_flash_port.
 * Moved out of monitor_port/monitor_rom.c (which keeps only the AmebaD raw ROM debug ops);
 * compiled by the monitor_port lib on KM4 (same archive as before, same link semantics).
 *
 * Drives the status-register BP/CMP bits: BOARD_FLASH_LOCK_FW => SR1=0x14 + SR2 CMP=1 (bottom
 * 3 MB: boot + both OTA slots locked, top 1 MB LittleFS/ITS writable); BOARD_FLASH_LOCK_ALL =>
 * SR1=0x1C (whole chip); BOARD_FLASH_UNLOCK => no protection. SR2/CMP via the chip-standard RDSR2
 * 0x35 / WRSR2 0x31 UNCONDITIONALLY (flash_init_para.FLASH_Status2_exist reads 0 on this SiP even
 * though the part HAS SR2, so gating on it would skip CMP and protect the wrong region). Wrapped
 * in FLASH_Write_Lock so the raw RDSR/WRSR don't collide with XIP. The shared helpers
 * (board_flash_lock_fw / _unlock), the `flash` command, and the boot status print live in
 * flash_cli.c.
 */
#include "ameba_soc.h"
#include "board_flash_lock.h"   /* board_flash_prot_t + the board_flash_* interface this file implements */

int board_flash_protect(board_flash_prot_t mode, u8 *o_sr1b, u8 *o_sr1a, u8 *o_sr2b, u8 *o_sr2a)
{
	u8 rdsr2 = flash_init_para.FLASH_cmd_rd_status2 ? flash_init_para.FLASH_cmd_rd_status2 : 0x35;
	u8 wrsr2 = flash_init_para.FLASH_cmd_wr_status2 ? flash_init_para.FLASH_cmd_wr_status2 : 0x31;
	u8 sr1_cur = 0, sr2_cur = 0, sr1_new, sr2_new, v1 = 0, v2 = 0;

	FLASH_Write_Lock();
	FLASH_RxCmd(flash_init_para.FLASH_cmd_rd_status, 1, &sr1_cur);
	FLASH_RxCmd(rdsr2, 1, &sr2_cur);

	board_flash_sr_for_mode(mode, sr2_cur, &sr1_new, &sr2_new);   /* shared mode->SR encoding */

	FLASH_SetStatus(flash_init_para.FLASH_cmd_wr_status, 1, &sr1_new);
	FLASH_SetStatus(wrsr2, 1, &sr2_new);

	FLASH_RxCmd(flash_init_para.FLASH_cmd_rd_status, 1, &v1);
	FLASH_RxCmd(rdsr2, 1, &v2);
	FLASH_Write_Unlock();

	if (o_sr1b) *o_sr1b = sr1_cur;
	if (o_sr1a) *o_sr1a = v1;
	if (o_sr2b) *o_sr2b = sr2_cur;
	if (o_sr2a) *o_sr2a = v2;
	return (v1 == sr1_new) && (v2 == sr2_new);
}

/* Read SR1/SR2 without changing protection. */
int board_flash_read_status(u8 *sr1, u8 *sr2)
{
	u8 rdsr2 = flash_init_para.FLASH_cmd_rd_status2 ? flash_init_para.FLASH_cmd_rd_status2 : 0x35;
	u8 s1 = 0, s2 = 0;

	FLASH_Write_Lock();
	FLASH_RxCmd(flash_init_para.FLASH_cmd_rd_status, 1, &s1);
	FLASH_RxCmd(rdsr2, 1, &s2);
	FLASH_Write_Unlock();

	if (sr1) *sr1 = s1;
	if (sr2) *sr2 = s2;
	return 1;
}
