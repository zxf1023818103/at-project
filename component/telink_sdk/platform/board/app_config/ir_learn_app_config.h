/********************************************************************************************************
 * @file    ir_learn_app_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2024
 *
 * @par     Copyright (c) 2024, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PB6 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB7 GPIO_PIN_UP_DOWN_FLOAT
#endif


#define IR_DIGITAL_TX_MODE 0 /* The digital pins output PWM waveforms just for sending to the IR. */
#define IR_DIGITAL_RX_MODE 1
#define IR_ANALOG_TX_MODE  2
#define IR_ANALOG_RX_MODE  3
#define IR_LEARN_MODE      IR_ANALOG_RX_MODE

#if ((IR_LEARN_MODE == IR_DIGITAL_RX_MODE) || (IR_LEARN_MODE == IR_ANALOG_RX_MODE))
    #define DMA_EN 0
    #if (DMA_EN)
        #define DMA_LLP_PINGPONG  1
        #define DMA_MODE_NO_CHAIN 2
        #define DMA_MODE          DMA_LLP_PINGPONG
    #endif
    #define IR_LEARN_RX_DATA_WIDTH 16 /* 16bits or 24bits. */
#endif

#if defined(MCU_CORE_TL322X)
#define IR_LEARN_PM_FUNCTION_EN 0
#else
#define IR_LEARN_PM_FUNCTION_EN 1
#endif

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PA0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB0 GPIO_PIN_UP_DOWN_FLOAT
#endif

#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
