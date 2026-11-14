/********************************************************************************************************
 * @file    emi_bqb_app_config.h
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

#define EMI_DEMO  0
#define BQB_DEMO  1
#define TAHDT_EMI_DEMO  2
#define TEST_DEMO EMI_DEMO

/**
 * @brief Generate EMI binary that support setting, Open by default
 * @note  1.The emi bin provided in the BDT tool is compiled under the EMI_SUPPORT_SETTING option.
 *        2.When compiling sram bin, you need to delete (NOLOAD) after the sbss and bss segments in ram_boot.link.
 *          At the same time, delete the AT instruction content after sdk_version segment.
 *          (The ram_boot.link file does not need to be modified when compiling the flash bin)
 */
#define EMI_SUPPORT_SETTING 0

#if (TEST_DEMO == EMI_DEMO)

    #if EMI_SUPPORT_SETTING
    /**
 * @brief Structure to save general setting.
 */
    typedef struct
    {
        unsigned char ptr[0];
        unsigned char calib_pos            : 2;
        unsigned char pa_bypass_en         : 1;
        unsigned char                      : 1;
        unsigned char swire_through_usb_en : 1;
        unsigned char power_mode           : 2;
        unsigned char cap                  : 1;
    } general_setting_t, *general_setting_t_ptr;

    /**
 * @brief Structure to save emi configuration.
 */
    typedef struct
    {
        unsigned int      pa_setting_pos;
        general_setting_t general_setting;
    } emi_setting_t, *emi_setting_t_ptr;
    #endif
#else // TEST_DEMO == BQB_DEMO
    /* set uart port */
    #if defined(MCU_CORE_B91)
        #define BQB_UART_TX_PORT UART0_TX_PB2
        #define BQB_UART_RX_PORT UART0_RX_PB3
    #elif defined(MCU_CORE_B92)
        #define BQB_UART_TX_PORT GPIO_FC_PB1
        #define BQB_UART_RX_PORT GPIO_FC_PB2
    #elif defined(MCU_CORE_TL721X)
        #define BQB_UART_TX_PORT GPIO_FC_PF5
        #define BQB_UART_RX_PORT GPIO_FC_PF6
    #elif defined(MCU_CORE_TL321X)
        #define BQB_UART_TX_PORT GPIO_FC_PC4
        #define BQB_UART_RX_PORT GPIO_FC_PC5
    #elif defined(MCU_CORE_TL751X)
        #define PULL_WAKEUP_SRC_PB5 GPIO_PIN_UP_DOWN_FLOAT
        #define PULL_WAKEUP_SRC_PB6 GPIO_PIN_UP_DOWN_FLOAT

        #define BQB_UART_TX_PORT GPIO_FC_PB5
        #define BQB_UART_RX_PORT GPIO_FC_PB6
    #elif defined(MCU_CORE_TL752X)
        typedef unsigned int gpio_pin_name_t;
        typedef unsigned int gpio_port_name_t;
        #define BQB_UART_TX_PIN GPIO_PIN_0
        #define BQB_UART_RX_PIN GPIO_PIN_1
        #define BQB_UART_RX_PORT GPIO_PORT_A
    #elif defined(MCU_CORE_TL322X)
        #define BQB_UART_TX_PORT GPIO_FC_PB5
        #define BQB_UART_RX_PORT GPIO_FC_PB6
    #elif defined(MCU_CORE_TL523X)
        #define BQB_UART_TX_PORT GPIO_FC_PB5
        #define BQB_UART_RX_PORT GPIO_FC_PB6
    #endif
    #define BQB_UART_BAUD 500000 //TODO: The baud of the TL752X needs to be divisible by MCLK0.

#endif

/**
 * @brief Define where the configuration data will be saved in the binary file.
 */
#define VERSION_ADDR         (0x02)
#define PA_SETTING_ADDR      (0x04)
#define GENERAL_SETTING_ADDR (0x16)

#if defined(MCU_CORE_TL721X)
    #define rf_stimer_get_tick()     rf_bb_timer_get_tick()
    #define RF_SYSTEM_TIMER_TICK_1US BB_TIMER_TICK_1US
    #define RF_SYSTEM_TIMER_TICK_1MS BB_TIMER_TICK_1MS
#else
    #define rf_stimer_get_tick()     stimer_get_tick()
    #define RF_SYSTEM_TIMER_TICK_1US SYSTEM_TIMER_TICK_1US
    #define RF_SYSTEM_TIMER_TICK_1MS SYSTEM_TIMER_TICK_1MS
#endif

#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
