/********************************************************************************************************
 * @file    keyscan_ana_app_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2024
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

#define KEYSCAN_TEST_1XADC_8K_ONCE          0//For internal testing only. Not available.
#define KEYSCAN_TEST_2XADC_8K_ONCE          1
#define KEYSCAN_TEST_2XADC_8K_TWICE_M1      2//For internal testing only. Not available.
#define KEYSCAN_TEST_2XADC_8K_TWICE_M2      3//For internal testing only. Not available.


#define KS_ANA_ADC0_DMA_CHN   DMA0
#define KS_ANA_ADC1_DMA_CHN   DMA1

/**
 * HALL_INIT_TIME is the time from power-on to operational readiness for the hall key,
 * user need to modify this value based on the material of the Hall keyboard.
 * t = (16 * HALL_INIT_TIME) * (1 / ks_ana_clock_freq), default ks_ana_clock_freq = 8MHz.
 */
#define HALL_INIT_TIME  0x14 //40us

#define KS_TEST_MODE                        KEYSCAN_TEST_2XADC_8K_ONCE

#define KS_DMA_LLP_ENABLE                   1

#define KS_TEST_THRESHOLD                   0

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
