/********************************************************************************************************
 * @file    bqb_app_config.h
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

#define ACCESS_CODE              0x29417671
#define BQB_UART_BAUD            115200 //(9600)//(57600)//(1000000)
#define SWITCH_POWER_MODE        0x00   /**< Power mode of the chip.(LDO_LDO:0x00;DCDC_LDO:0x01;DCDC_DCDC:0x03)*/
#define SWITCH_INTERNAL_CAP      0      /**< Capacitor selection: 0 for internal cap, 1 for external cap. */
#define SWITCH_CALI_POSITION     0      /**< Calibration position: 0 for FLASH, 1 for OTP. */
#define SWITCH_GPIO_VOLTAGE      0      /**< The configuration of GPIO voltage. (0:GPIO voltage 3.3V 1:GPIO voltage 1.8V)*/
#define PA_FUNCTION_ENABLE       0      /**< PA enable flag (0: disabled,1: enabled). */
#define PA_BYPASS_ENABLE         0      /**< PA bypass enable flag (0: disabled,1: enabled).*/
#define SWIRE_THROUGH_USB_ENABLE 0      /**< Enable Swire through USB(0: disabled,1: enabled) */
#define POWER_SLICE_EN           0      /**< Power slice enable flag (0: disabled,1: enabled)*/
#define BQB_VOLTAGE_TYPE         1      /**< RF tx power voltage type (1:vbat,0:vant)*/

/* set TX return result */
#define RETURN_TX_LEN_EN 0 //1: return tx length, 0:do not return tx length

/* set power of Tx */
#if defined(MCU_CORE_B91)
    #define BQB_TX_POWER RF_POWER_P6p98dBm
#elif defined(MCU_CORE_TL321X)
    #define BQB_TX_POWER RF_POWER_P7p19dBm
#elif defined(MCU_CORE_TL721X)
    #define BQB_TX_POWER RF_POWER_P7p52dBm
#elif defined(MCU_CORE_TL751X)
    #define BQB_TX_POWER RF_POWER_P10p00dBm
#elif defined(MCU_CORE_TL322X)
    #define BQB_TX_POWER RF_POWER_P10p00dBm
#elif defined(MCU_CORE_TL521X)
    #define BQB_TX_POWER RF_POWER_P8p00dBm
#elif defined(MCU_CORE_TL753X)
    #define BQB_TX_POWER RF_POWER_P10p00dBm
#else
    #define BQB_TX_POWER RF_POWER_P7p00dBm
#endif

/* set uart port */
#if defined(MCU_CORE_B91)
    #define BQB_UART_TX_PORT UART0_TX_PA3
    #define BQB_UART_RX_PORT UART0_RX_PA4
#elif defined(MCU_CORE_B92)
    #define BQB_UART_TX_PORT GPIO_FC_PA4
    #define BQB_UART_RX_PORT GPIO_FC_PA3
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
#elif defined(MCU_CORE_TL322X)
    #define BQB_UART_TX_PORT GPIO_FC_PD6
    #define BQB_UART_RX_PORT GPIO_FC_PD7
#elif defined(MCU_CORE_TL323X)
    #define BQB_UART_TX_PORT GPIO_FC_PD1
    #define BQB_UART_RX_PORT GPIO_FC_PA1
#elif defined(MCU_CORE_TL521X)
    #define BQB_UART_TX_PORT GPIO_FC_PB2
    #define BQB_UART_RX_PORT GPIO_FC_PB3
#elif defined(MCU_CORE_TL753X)
    #define PULL_WAKEUP_SRC_PB5 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PB6 GPIO_PIN_UP_DOWN_FLOAT

    #define BQB_UART_TX_PORT GPIO_FC_PF3
    #define BQB_UART_RX_PORT GPIO_FC_PF4
#endif


#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
