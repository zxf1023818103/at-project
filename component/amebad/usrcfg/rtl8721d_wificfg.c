/**
  ******************************************************************************
  * @file    rtl8721d_wificfg.c
  * @author
  * @version V1.0.0
  * @date    2016-05-17
  * @brief   This file provides firmware functions to manage the wifi configration 
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

WIFICFG_TypeDef rtk_wifi_config =
{
	.wifi_app_ctrl_tdma = FALSE,
	.wifi_ultra_low_power = FALSE, /* default is FALSE */
	.km4_cache_enable = TRUE, /* default is TRUE */
	.km0_dslp_force_reinit = TRUE, /*default is TURE*/
};

/* The newer TizenRT lib_wlan_fpu.a references the config struct as `wifi_config`
 * (the older lib used `rtk_wifi_config`); WIFICFG_TypeDef is byte-identical. The
 * `extern` makes this an alias DEFINITION rather than a tentative def + alias
 * (which the compiler rejects as "defined both normally and as alias"). */
extern WIFICFG_TypeDef wifi_config __attribute__((alias("rtk_wifi_config")));

/******************* (C) COPYRIGHT 2016 Realtek Semiconductor *****END OF FILE****/
