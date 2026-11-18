/********************************************************************************************************
 * @file    stimer_app_config.h
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


#define N22_TEST 0
#define DSP_TEST 0

#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X)
    #define PWM0_PIN        GPIO_FC_PA1
    #define INPUT_CAPT_PIN  GPIO_PD5
    #define CAPT_DMA_CHN    DMA0
    #define CAPT_DMA_STATUS BIT(CAPT_DMA_CHN)
#endif


#define STIMER_DELAY                          1

#if defined(MCU_CORE_B91) || defined(MCU_CORE_B92) || defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL523X)
    #define STIMER_IRQ                        2
#elif defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL752X) || defined(MCU_CORE_TL522X)
    #define STIMER_IRQ_D25                    3
    #define STIMER_IRQ_D25_N22_DSP            4
#endif

#if !defined(MCU_CORE_TL752X)
#define STIMER_GET_32K_TICK                   5
#if !defined(MCU_CORE_TL523X)
#define STIMER_SET_32K_TICK                   6
#endif
#if !defined(MCU_CORE_B91)
    #define STIMER_SET_32K_TICK_LVL           7
#endif

#define STIMER_TRACK_32K_TICK                 8
#define STIMER_GET_TICK                       9

#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X)|| defined(MCU_CORE_TL323X)  || defined(MCU_CORE_TL521X)
    #define STIMER_INPUT_CAPTURE              10
    #define STIMER_INPUT_CAPTURE_DMA          11
    #define STIMER_INPUT_CAPTURE_DMA_LLP_MODE 12
#endif

#endif
#define STIMER_TIMEOUT_IRQ                    13

#define STIMER_MODE                           STIMER_DELAY

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PA1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD4 GPIO_PIN_UP_DOWN_FLOAT
#endif

#include "driver.h"

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
