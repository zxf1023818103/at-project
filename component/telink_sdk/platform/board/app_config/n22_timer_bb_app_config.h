/********************************************************************************************************
 * @file    n22_timer_bb_app_config.h
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


//TIMER_BB does not support GPIO_TRIGGER_MODE and GPIO_WIDTH_MODE modes.
#define TIMER_BB_SYS_CLOCK_MODE 1
#define TIMER_BB_TICK_MODE      2

#define TIMER_BB_MODE           TIMER_BB_SYS_CLOCK_MODE

#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
