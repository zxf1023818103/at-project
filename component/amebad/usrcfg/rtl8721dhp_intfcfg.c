/**
  ******************************************************************************
  * @file    rtl8721dlp_intfcfg.c
  * @author
  * @version V1.0.0
  * @date    2016-05-17
  * @brief   This file provides firmware functions to manage the following 
  *          functionalities:
  *           - uart mbed function config
  ******************************************************************************
  * @attention
  *
  * This module is a confidential and proprietary property of RealTek and
  * possession or use of this module requires written permission of RealTek.
  *
  * Copyright(c) 2015, Realtek Semiconductor Corporation. All rights reserved.
  ****************************************************************************** 
  */

#include "ameba_soc.h"
#include "autoconf.h"


PSRAMCFG_TypeDef psram_dev_config = {
#if (defined(CONFIG_REPEATER) && CONFIG_REPEATER) || \
	((defined(CONFIG_MATTER_SECURE) && CONFIG_MATTER_SECURE))
	.psram_dev_enable = TRUE,			//enable psram
	.psram_dev_cal_enable = TRUE,			//enable psram calibration function
	.psram_dev_retention = TRUE,			//enable psram retention
#else
	.psram_dev_enable = FALSE,			//enable psram
	.psram_dev_cal_enable = FALSE,			//enable psram calibration function
	.psram_dev_retention = FALSE,			//enable psram retention	
#endif
};

SDIOHCFG_TypeDef sdioh_config = {
	.sdioh_bus_speed = SD_SPEED_HS,				//SD_SPEED_DS or SD_SPEED_HS
	.sdioh_bus_width = SDIOH_BUS_WIDTH_4BIT, 	//SDIOH_BUS_WIDTH_1BIT or SDIOH_BUS_WIDTH_4BIT
	.sdioh_cd_pin = _PB_25,						//_PB_25/_PA_6/_PNC
	.sdioh_wp_pin = _PNC,						//_PB_24/_PA_5/_PNC
};

/* FTL (flash translation layer) removed — unused in this driver-only build:
 * nothing calls ftl_*; the ftl component + its config were dropped. */

/******************* (C) COPYRIGHT 2016 Realtek Semiconductor *****END OF FILE****/
