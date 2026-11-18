/*
 * Copyright (c) 2024 Realtek Semiconductor Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ameba_soc.h"

PRAM_START_FUNCTION BOOT_SectionInit(void)
{
	return (PRAM_START_FUNCTION)__image2_entry_func__;
}
/**
 * @brief It is forbidden to use any print function in BOOT_WakeFromPG,
 * because print function (including rodata section) is in flash at this time, but flash has
 * not been woken up.
 */
SRAM_ONLY_TEXT_SECTION
void BOOT_WakeFromPG(void)
{
	PRAM_START_FUNCTION Image2EntryFun = (PRAM_START_FUNCTION)__image2_entry_func__;

	RSIP_OTF_Load_Key(); //Wait RSIP AES Ready

	/* we should Cache_Flush when we wake */
	Cache_Enable(ENABLE);

	SCB->VTOR = Image2EntryFun->VectorNS;

	Image2EntryFun->RamWakeupFun();
}

/* Dplus ROM enters this table; stage 2 remains embedded in KM0 image2. */
extern void BOOT_Image2(void);
void BOOT_Image1(void)
{
    Cache_Enable(ENABLE);
    BOOT_Image2();
}

IMAGE1_ENTRY_SECTION
RAM_FUNCTION_START_TABLE RamStartTable = {
	.RamStartFun = NULL,
	.RamWakeupFun = BOOT_WakeFromPG,
	.RamPatchFun0 = NULL,
	.RamPatchFun1 = NULL,
	.RamPatchFun2 = NULL,
	.FlashStartFun = BOOT_Image1,
	.Img1ValidCode = NULL,
};
