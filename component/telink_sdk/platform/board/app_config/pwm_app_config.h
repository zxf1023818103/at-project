/********************************************************************************************************
 * @file    pwm_app_config.h
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
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif


#define PWM_CONTINUE 1
#define PWM_COUNT    2
#define PWM_IR       3
#define PWM_IR_FIFO  4
#define PWM_IR_DMA   5
#if !defined(MCU_CORE_B91)
    #define PWM_CENTER_ALIGNED 6
#endif
#define SET_PWM_MODE   PWM_CONTINUE


#if defined(MCU_CORE_TL751X)
#define PWM_PCLK_SPEED 66000000
#elif defined(MCU_CORE_TL721X)
#define PWM_PCLK_SPEED 60000000
#else
#define PWM_PCLK_SPEED 24000000
#endif

#define PWM_32K_SPEED  32000    //pwm 32K

    /**
 * This configure is for PWM Ticks.
 */
    enum
    {
        CLOCK_PWM_CLOCK_1S  = PWM_PCLK_SPEED,
        CLOCK_PWM_CLOCK_1MS = (CLOCK_PWM_CLOCK_1S / 1000),
        CLOCK_PWM_CLOCK_1US = (CLOCK_PWM_CLOCK_1S / 1000000),
    };

    enum
    {
        CLOCK_PWM_32K_1S  = PWM_32K_SPEED,
        CLOCK_PWM_32K_1MS = (CLOCK_PWM_32K_1S / 1000),
    };

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PB5 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB6 GPIO_PIN_UP_DOWN_FLOAT
#endif


#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
