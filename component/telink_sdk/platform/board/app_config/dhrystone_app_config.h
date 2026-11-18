/********************************************************************************************************
 * @file    dhrystone_app_config.h
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
#include "driver.h"

#define MANUAL_MODE           1 // For internal testing, users need not care
#define AUTO_MODE             2
#define DHRYSTONE_TEST_MODE   MANUAL_MODE

#if defined(MCU_CORE_TL321X)
    #define MANUAL_TEST_MODE           1 // For internal testing, users need not care
    #define AUTO_TEST_MODE             2 // For internal testing, users need not care
    #define GPIO_TEST_MODE             MANUAL_TEST_MODE


    #define CURRENT_PER_MHZ_TEST       0

    #define CCLK_24M_HCLK_12M_PCLK_12M 1
    #define CCLK_24M_HCLK_24M_PCLK_12M 2
    #define CCLK_24M_HCLK_24M_PCLK_24M 3
    #define CCLK_48M_HCLK_24M_PCLK_24M 4
    #define CCLK_48M_HCLK_48M_PCLK_24M 5
    #define CCLK_96M_HCLK_48M_PCLK_24M 6
    #define CCLK_48M_HCLK_48M_PCLK_48M 7
    #define CCLK_96M_HCLK_48M_PCLK_48M 8
    #define CLOCK_FREQUENCY            CCLK_24M_HCLK_24M_PCLK_12M

#endif

#if defined(MCU_CORE_TL323X)||defined(MCU_CORE_TL521X)
    #define MANUAL_TEST_MODE           1 // For internal testing, users need not care
    #define AUTO_TEST_MODE             2 // For internal testing, users need not care
    #define GPIO_TEST_MODE             MANUAL_TEST_MODE


    #define CURRENT_PER_MHZ_TEST       1

    #define CCLK_24M_HCLK_24M_PCLK_24M 1
    #define CCLK_48M_HCLK_24M_PCLK_24M 2
    #define CCLK_48M_HCLK_24M_PCLK_12M 3
    #define CCLK_96M_HCLK_48M_PCLK_48M 4
    #define CCLK_96M_HCLK_96M_PCLK_48M 5
    #define CCLK_48M_HCLK_48M_PCLK_48M 6
    #define CCLK_96M_HCLK_96M_PCLK_96M 7
    #define CCLK_48M_HCLK_48M_PCLK_24M 8
    #define CLOCK_FREQUENCY            CCLK_48M_HCLK_24M_PCLK_24M

#endif
    extern void  dhry_main(void);
    extern int Dhrystone_DMIPS_Per_MHz;


/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
