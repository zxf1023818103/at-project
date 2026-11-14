/********************************************************************************************************
 * @file    common.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2023
 *
 * @par     Copyright (c) 2023, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#pragma once
#if !(defined(MCU_CORE_TL752X)||defined(MCU_CORE_TL651X))
#include "printf.h"
#endif
#include "app_config/app_config.h"

#if !(defined(MCU_CORE_TL752X)||defined(MCU_CORE_TL651X))
#include "auto_test/dut_cmd.h"
//#include "auto_test/pc_interface.h"
#include "calibration.h"
#include <string.h>
#include "hal_driver/flash/hal_flash.h"
#include "gpio.h"
#else
#include "calibration.h"
#include "soc_printf.h"
#include "tl_log_transport.h"
#include "tl_log.h"
#endif

#if defined(MCU_CORE_TL752X)
#include "hal_driver/flash/hal_flash.h"
#include "calibration.h"
#include "hal/hal_clock.h"
#include "hal/hal_gpio.h"
#include "hal/hal_flash.h"
#include "hal/clic.h"
#endif
/**
    ===============================================================================
                         ##### platform init and clock init #####
    ===============================================================================
    flash_protect_en assignment description, the default is 1, use the following rules:
    1. By default, the application needs to enable write protection in all cases, this parameter is reserved for some internal special test cases;
    2. Some demos have the operation of writing flash, remember to remove the write protection before erasing, and add the write protection after erasing;
    3. There are some demos that do not need to add write protection, such as dut-related, internal testing;

    @note if flash protection fails, LED1 lights up long, and keeps while.
    ===============================================================================
*/

extern volatile unsigned int g_debug_flag;
#define  DEBUG_DOT()    do{g_debug_flag = (0xff000000 | __LINE__);}while(0)

#if defined(MCU_CORE_B91)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P4_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT CCLK_24M_HCLK_24M_PCLK_24M
    #endif
#elif defined(MCU_CORE_B92)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, gpio_voltage_e gpio_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P4_LDO_2P0, VBAT_MAX_VALUE_GREATER_THAN_3V6, GPIO_VOLTAGE_3V3, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT CCLK_24M_HCLK_24M_PCLK_24M
    #endif
#elif defined(MCU_CORE_TL7518)
void platform_init(vbat_type_e vbat_v, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(VBAT_MAX_VALUE_GREATER_THAN_3V6, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT CCLK_96M_HCLK_96M_PCLK_24M_MSPI_48M
    #endif
#elif defined(MCU_CORE_TL751X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_AVDD_LDO_DVDD, VBAT_MAX_VALUE_GREATER_THAN_3V6, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT PLL_264M_D25F_DSP_132M_HCLK_66M_PCLK_66M_MSPI_44M_WT_11M
    #endif
#elif defined(MCU_CORE_TL721X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_0P94_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT PLL_240M_CCLK_120M_HCLK_60M_PCLK_60M_MSPI_48M
    #endif
#elif defined(MCU_CORE_TL321X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P25_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT PLL_192M_CCLK_48M_HCLK_48M_PCLK_48M_MSPI_48M
    #endif
#elif defined(MCU_CORE_W92)
void platform_init(vbat_type_e vbat_v, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(VBAT_MAX_VALUE_GREATER_THAN_3V6, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT CCLK_96M_HCLK_96M_PCLK_24M_MSPI_48M
    #endif
#elif defined(MCU_CORE_TL322X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P25_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT        PLL_144M_D25F_72M_HCLK_N22_36M_PCLK_36M_MSPI_48M
    #endif
#elif defined(MCU_CORE_TL522X)
void platform_init(unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT
    #endif
#elif defined(MCU_CORE_TL323X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P25_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT        PLL_192M_CCLK_48M_HCLK_48M_PCLK_48M_MSPI_48M
    #endif
#elif defined(MCU_CORE_TL753X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_AVDD_LDO_DVDD, VBAT_MAX_VALUE_GREATER_THAN_3V6, 0)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT        PLL_192M_D25F_DSP_96M_HCLK_48M_PCLK_48M_MSPI_48M_N22_48M
    #endif
#elif defined(MCU_CORE_TL521X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
     #define PLATFORM_INIT platform_init(LDO_1P25_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 1)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT PLL_144M_CCLK_72M_HCLK_36M_PCLK_36M_MSPI_48M
    #endif
#elif defined (MCU_CORE_TL752X)
#include "tl_chip.h"
void platform_init(unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(0)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT PLL0_BBPLL_768M_MCLK_128M_D25F_128M_N22_64M_DSP_128M_MSPI_32M
    #endif
#elif defined (MCU_CORE_TL651X)
#include "epm_chip.h"
void platform_init(void);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT   platform_init()
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT
    #endif
#elif defined(MCU_CORE_TL711X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_AVDD_LDO_DVDD, VBAT_MAX_VALUE_GREATER_THAN_3V6, 0)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT
    #endif
#elif defined(MCU_CORE_TL523X)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, cap_typedef_e cap, unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
     #define PLATFORM_INIT platform_init(LDO_1P25_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6, INTERNAL_CAP_XTAL24M, 0)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT DOUBLER_48M_SYSCLK_48M
    #endif
#else
void platform_init(unsigned char flash_protect_en);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(0)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT
    #endif
#endif

