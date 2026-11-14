/********************************************************************************************************
 * @file    audio_app_config_1v2.h
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


#if defined(__cplusplus)
extern "C"
{
#endif

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PA2 GPIO_PIN_UP_DOWN_FLOAT

#define PULL_WAKEUP_SRC_PC0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC3 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC4 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC5 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC6 GPIO_PIN_UP_DOWN_FLOAT

#define PULL_WAKEUP_SRC_PF6 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PF7 GPIO_PIN_UP_DOWN_FLOAT

#define PULL_WAKEUP_SRC_PG0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG3 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG4 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG5 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG6 GPIO_PIN_UP_DOWN_FLOAT
#endif

#include "driver.h"

#define I2S_DEMO           (1)
#define CODEC_DEMO         (2)
#define ASRC_DEMO          (3)
#if defined(MCU_CORE_TL751X)
#define ANC_DEMO           (4)
#define EQ_DEMO            (5)
#define SIDETONE_DEMO      (6)
#define AUDIO_MIX_DEMO     (7)
#endif
#define AUDIO_MODE CODEC_DEMO

#if defined(__cplusplus)
}
#endif
