/********************************************************************************************************
 * @file    n22_stimer_app_config.h
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

#define PWM0_PIN                GPIO_FC_PB0
#define INPUT_CAPT_PIN          GPIO_PD0
#define CAPT_DMA_CHN            DMA0
#define CAPT_DMA_STATUS         BIT(CAPT_DMA_CHN)


#define STIMER_DELAY            1
#define STIMER_IRQ_D25_N22_DSP  2
#define STIMER_IRQ_N22          3
#if !defined(MCU_CORE_TL752X)
#define STIMER_GET_32K_TICK     4
#define STIMER_SET_32K_TICK     5
#define STIMER_SET_32K_TICK_LVL 6
#define STIMER_TRACK_32K_TICK   7
#define STIMER_GET_TICK         8
#if defined(MCU_CORE_TL322X)
    #define STIMER_INPUT_CAPTURE              9
    #define STIMER_INPUT_CAPTURE_DMA          10
    #define STIMER_INPUT_CAPTURE_DMA_LLP_MODE 11
#endif
#endif

#define STIMER_MODE STIMER_IRQ_N22

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PB0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD0 GPIO_PIN_UP_DOWN_FLOAT
#endif

#include "driver.h"

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
