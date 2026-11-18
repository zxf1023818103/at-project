/********************************************************************************************************
 * @file    ctb_app_config_1V1.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2026
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
#include "driver.h"

/* CTB touch channel bitmask definitions, each channel corresponds to one bit */
#define CTB_CHANNEL_0   BIT(0)
#define CTB_CHANNEL_1   BIT(1)
#define CTB_CHANNEL_2   BIT(2)
#define CTB_CHANNEL_3   BIT(3)
#define CTB_CHANNEL_4   BIT(4)
#define CTB_CHANNEL_5   BIT(5)
#define CTB_CHANNEL_6   BIT(6)
#define CTB_CHANNEL_7   BIT(7)
#define CTB_CHANNEL_8   BIT(8)
#define CTB_CHANNEL_9   BIT(9)
#define CTB_CHANNEL_10  BIT(10)
#define CTB_CHANNEL_11  BIT(11)
#define CTB_CHANNEL_12  BIT(12)
#define CTB_CHANNEL_13  BIT(13)
#define CTB_CHANNEL_14  BIT(14)
#define CTB_CHANNEL_15  BIT(15)

#define ONE_CHN_MODE  0
#define MUL_CHN_MODE  1

#define CTB_MODE MUL_CHN_MODE

#define CTB_SENSITIVITY_MANUAL_OFF 0
#define CTB_SENSITIVITY_MANUAL_ON  1

#define CTB_SENSITIVITY_MANUAL CTB_SENSITIVITY_MANUAL_OFF
