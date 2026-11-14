/********************************************************************************************************
 * @file    printf.h
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
#define DEBUG_MODE 1
#if !(defined(MCU_CORE_TL752X) ||defined(MCU_CORE_TL651X))
#include "gpio.h"
#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL323X)|| defined(MCU_CORE_TL322X)|| defined(MCU_CORE_TL521X) || defined(MCU_CORE_TL711X)|| defined(MCU_CORE_TL523X)|| defined(MCU_CORE_TL522X)
    #include "lib/include/stimer.h"
#else
    #include "stimer.h"
#endif
#include <stdio.h>


#if (DEBUG_MODE == 1)

    #define UART_PRINT_DEBUG_ENABLE 0
    #define USB_PRINT_DEBUG_ENABLE  1

    #ifndef DEBUG_BUS
        #define DEBUG_BUS UART_PRINT_DEBUG_ENABLE
    #endif
    /**
 * @brief      This function serves to printf string.
 * @param[in]  *format  -  format string need to print
 * @param[in]  ...      -  variable number of data
 * @return     none.
 */
    #if (DEBUG_BUS == USB_PRINT_DEBUG_ENABLE)
        #define FIFOTHRESHOLD 4
        #define BLOCK_MODE    0
        #define EDPS_DAT      reg_usb_ep8_dat
        #define USBFIFO       reg_usb_ep8_fifo_mode


    #elif (DEBUG_BUS == UART_PRINT_DEBUG_ENABLE)

        #define PRINT_BAUD_RATE   115200   //1M baud rate,should Not bigger than 1Mb/s

        #if defined(MCU_CORE_TL521X)
            #define DEBUG_INFO_TX_PIN GPIO_PB2 //uart tx pin
            #define PULL_WAKEUP_SRC_PB2 GPIO_PIN_UP_DOWN_FLOAT
            #define PB2_FUNC            AS_GPIO
            #define PB2_INPUT_ENABLE    0
            #define PB2_OUTPUT_ENABLE   1
            #define PB2_DATA_OUT        0
        #else
            #define DEBUG_INFO_TX_PIN GPIO_PA0 //uart tx pin
            
            #define PULL_WAKEUP_SRC_PA0 GPIO_PIN_UP_DOWN_FLOAT
            #define PA0_FUNC            AS_GPIO
            #define PA0_INPUT_ENABLE    0
            #define PA0_OUTPUT_ENABLE   1
            #define PA0_DATA_OUT        0

        #endif

        #if defined(MCU_CORE_B91) || defined(MCU_CORE_B92)
            #define TX_PIN_OUTPUT_REG reg_gpio_out(DEBUG_INFO_TX_PIN)
        #else
            #define TX_PIN_OUTPUT_REG reg_gpio_out_set_clear(DEBUG_INFO_TX_PIN)
        #endif

        #define BIT_INTERVAL (SYSTEM_TIMER_TICK_1S / PRINT_BAUD_RATE)

    #endif
#endif
#else

#endif

