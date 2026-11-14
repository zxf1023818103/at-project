/********************************************************************************************************
 * @file    dut_test_app_config.h
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



/*The DUT_SRAM and DUT_CACHE are temporary and not general items.These items are to screen out bad chips because ATE did not add test before .ATE will add cache and sram test later. */
/*The DUT_FLASH_SCRATCH are temporary items to filter 2M flash. The problematic chip packaging is TLSR9517D,TLSR9515R and TLSR9516A*/
#define DUT_FLASH                            1
#define DUT_RF                               2
#define DUT_GPIO                             3
#define DUT_PM                               4
#define DUT_FLASH_4WIRE_EN                   5
#define DUT_ADC_CALIB                        6
#define DUT_AUDIO                            7
#define DUT_GPIO_MULTI_MCU_MODULE            8
#define DUT_SRAM                             9
#define DUT_CACHE                            10
#define DUT_FW_ENCRYPT                       11
#define DUT_FLASH_TRIM                       12
#define DUT_FLASH_SCRATCH                    13
#define DUT_MAC_FILTER                       14
#define DUT_RF_FREQUENCY_OFFSET_CALIB_SLAVE  15
#define DUT_RF_FREQUENCY_OFFSET_CALIB_MASTER 16
#define DUT_OTP                              17
#define TEST_DEMO                            DUT_FLASH
#define BDT                                  1
#define JTAG                                 0

#if defined(MCU_CORE_B92)
    #define GPIO_VOLTAGE_MODE GPIO_VOLTAGE_3V3 /* GPIO_VOLTAGE_3V3;GPIO_VOLTAGE_1V8*/
#endif

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
