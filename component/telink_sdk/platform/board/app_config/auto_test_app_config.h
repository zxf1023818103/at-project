/********************************************************************************************************
 * @file    auto_test_app_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2019
 *
 * @par     Copyright (c) 2019, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif

#define TxIO         GPIO_PD4
#define RxIO         GPIO_PD5

#define MSPI_CLK_24M CCLK_TO_MSPI_CLK

/**
    ===============================================================================
                         ##### LED and KEY #####
    ===============================================================================
    The default configuration of LEDs and KEYs match the following hardware revisions:
    B91 C1T213A20_V1_3_20200727
    B92 C1T266A20_V1_3_20220722

*/
#if defined(MCU_CORE_B91)
    #define LED1 GPIO_PB4
    #define LED2 GPIO_PB5
    #define LED3 GPIO_PB6
    #define LED4 GPIO_PB7
    /*
 * Button matrix table:
 *          KEY3    KEY4
 *  KEY1    SW2     SW3
 *  KEY2    SW4     SW5
 */
    #define KEY1 GPIO_PC2
    #define KEY2 GPIO_PC0
    #define KEY3 GPIO_PC3
    #define KEY4 GPIO_PC1
#elif defined(MCU_CORE_B92)
    #define LED3 GPIO_PD0
    #define LED4 GPIO_PD1
    #define LED1 GPIO_PE6
    #define LED2 GPIO_PE7
    /*
 * Button matrix table:
 *          KEY3    KEY4
 *  KEY1    SW2     SW3
 *  KEY2    SW4     SW5
 */
    #define KEY1 GPIO_PE2
    #define KEY2 GPIO_PE3
    #define KEY3 GPIO_PE4
    #define KEY4 GPIO_PF0
#else

#endif
/**
    ===============================================================================
                         ##### platform init and clock init #####
    ===============================================================================

*/
#if defined(MCU_CORE_B91)
    void platform_init(power_mode_e power_mode, vbat_type_e vbat_v);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P4_LDO_1P8, VBAT_MAX_VALUE_GREATER_THAN_3V6)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT CCLK_24M_HCLK_24M_PCLK_24M
    #endif
#elif defined(MCU_CORE_B92)
void platform_init(power_mode_e power_mode, vbat_type_e vbat_v, gpio_voltage_e gpio_v);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init(LDO_1P4_LDO_2P0, VBAT_MAX_VALUE_GREATER_THAN_3V6, GPIO_VOLTAGE_3V3)
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT CCLK_24M_HCLK_24M_PCLK_24M
    #endif
#elif defined(MCU_CORE_TL321X)||defined(MCU_CORE_TL323X)||defined(MCU_CORE_TL521X)
#elif defined(MCU_CORE_TL721X)
#elif defined(MCU_CORE_TL322X)
#else
void platform_init(void);
    #ifndef PLATFORM_INIT
        #define PLATFORM_INIT platform_init()
    #endif
    #ifndef CLOCK_INIT
        #define CLOCK_INIT
    #endif
#endif

#if defined(MCU_CORE_B91)
    #define RF_POWER RF_POWER_P6p98dBm
#elif defined(MCU_CORE_B92)
    #define RF_POWER RF_POWER_P7p72dBm
#elif defined(MCU_CORE_TL7518)
    #define RF_POWER RF_POWER_P4p92dBm
#elif defined(MCU_CORE_TL721X)
    #define RF_POWER RF_POWER_P4p79dBm
#elif defined(MCU_CORE_TL321X)
    #define RF_POWER RF_POWER_P4p52dBm
#elif defined(MCU_CORE_TL322X)
    #define RF_POWER RF_POWER_P10p00dBm
#endif

#if defined(MCU_CORE_TL721X)
#define rf_timer_get_tick            rf_bb_timer_get_tick
#else
#define rf_timer_get_tick            stimer_get_tick
#endif



#include "driver.h"
#include "calibration.h"
#include "printf.h"
#include <string.h>
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
