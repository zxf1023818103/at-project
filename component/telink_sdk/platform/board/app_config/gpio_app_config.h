/********************************************************************************************************
 * @file    gpio_app_config.h
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

#define NORMAL_MODE 1
#define TEST_MODE   2 // For internal testing, users need not care
#define DEMO_MODE   NORMAL_MODE

#if (DEMO_MODE == NORMAL_MODE)
    #define IRQ_PIN           KEY1
    #define GPIO_HIGH_RESISTOR 1
    #if defined(MCU_CORE_B91) || defined(MCU_CORE_B92) || defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL721X)
        #define GPIO_IRQ       2
        #define GPIO_IRQ_RISC0 3
        #define GPIO_IRQ_RISC1 4
        #if defined(MCU_CORE_B92) || defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL721X)
            #define GPIO_SEL_IRQ_SRC 5
        #endif
        /**
 * Do not recommended that two or more GPIOs be set to the same interrupt source, as the interrupt is uncertain if triggered at the same time.
 */
        #define GPIO_MODE GPIO_IRQ

    #elif defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL523X) || defined(MCU_CORE_TL753X)

        //GPIO_MODE can be configured as GPIO_IRQ0~7, 8 interrupts can be used at the same time.
        #define GPIO_IRQ_NUM0 6
        #define GPIO_IRQ_NUM1 7
        /**
 * Do not recommended that two or more GPIOs be set to the same interrupt source, as the interrupt is uncertain if triggered at the same time.
 */
        #define GPIO_MODE     GPIO_IRQ_NUM0
    #endif


    #define GPIO_DEMO_KEY         1 //Short press SW2 intermittent trigger interrupts,short press KEY3 to generate an edge signal.
    #define GPIO_DEMO_SQUARE_WAVE 2 //long presses SW2 to continuously trigger interrupts,IRQ_PIN connects to KEY3, toggle KEY3 to generate a square wave signal.
    #define GPIO_DEMO_MODE        GPIO_DEMO_KEY

#endif
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
