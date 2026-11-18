/********************************************************************************************************
 * @file    timer_app_config.h
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

#if defined(MCU_CORE_TL752X)

/**
 * When core voltage is 0.72~0.8V, the timer clock supports the maximum frequency up to 25MHz.
 * When core voltage is 0.81~0.99V, the timer clock supports the maximum frequency up to 50MHz.
 */
#define TIMER_TICK_CLK_25M  25000000
#define TIMER_TICK_CLK_50M  50000000
#define TIMER_TICK_CLK      TIMER_TICK_CLK_25M

#define TIMER_PERIOD_MODE            0
#define TIMER_PWM_MODE               1
#define TIMER_MODE              TIMER_PERIOD_MODE
#else
#if defined(MCU_CORE_TL751X)
    #define TIMER0_GPIO GPIO_PD0
    #define TIMER1_GPIO GPIO_PD1
#else
    #define TIMER0_GPIO GPIO_PA0
    #define TIMER1_GPIO GPIO_PA1
#endif

#if defined(MCU_CORE_TL751X)
    #define TIMER0_TRIG_PIN GPIO_PD2
    #define TIMER1_TRIG_PIN GPIO_PD3
#else
    #define TIMER0_TRIG_PIN GPIO_PA2
    #define TIMER1_TRIG_PIN GPIO_PA3
#endif

#define PWM_OUTPUT_PIN          GPIO_FC_PB0
#define TIMER0_CAPT_PIN         GPIO_PA1
#define TIMER1_CAPT_PIN         GPIO_PA3
#define TIMER0_COMP_PIN         GPIO_FC_PC0
#define TIMER1_COMP_PIN         GPIO_FC_PC1


#define TIMER_SYS_CLOCK_MODE    1
#define TIMER_GPIO_TRIGGER_MODE 2
#define TIMER_GPIO_WIDTH_MODE   3
#define TIMER_TICK_MODE         4
#define TIMER_WATCHDOG_MODE     5
#if !defined(MCU_CORE_B91)
    #define TIMER_32K_WATCHDOG_MODE 6
#endif
#if defined(MCU_CORE_TL721X)
    #define TIMER_INPUT_CAPTURE_MODE              7
    #define TIMER_INPUT_CAPTURE_MODE_WITH_DMA     8
    #define TIMER_INPUT_CAPTURE_MODE_WITH_DMA_LLP 9
#endif

#define TIMER_MODE             TIMER_SYS_CLOCK_MODE


#define WATCHDOG_32K_XTAL_MODE 1 //The TL7518 A0 version not support 32k xtal.
#define WATCHDOG_32K_RC_MODE   2

#define WATCHDOG_MODE          WATCHDOG_32K_RC_MODE

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PD0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD3 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PA1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PA3 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PC1 GPIO_PIN_UP_DOWN_FLOAT
#endif

#endif
#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
