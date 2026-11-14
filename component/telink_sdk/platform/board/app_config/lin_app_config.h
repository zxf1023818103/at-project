/********************************************************************************************************
 * @file    lin_app_config.h
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

#if defined(MCU_CORE_TL322X)
#define PULL_WAKEUP_SRC_PB0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB3 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD4 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PD5 GPIO_PIN_UP_DOWN_FLOAT
#endif

#include "driver.h"

#if defined(MCU_CORE_TL322X)
    #define LIN0_EN_PIN GPIO_FC_PB2
    #define LIN0_TX_PIN GPIO_FC_PB3
    #define LIN0_RX_PIN GPIO_FC_PB1

    #define LIN1_EN_PIN GPIO_FC_PD4
    #define LIN1_TX_PIN GPIO_FC_PB0
    #define LIN1_RX_PIN GPIO_FC_PD5
#endif

#define LIN0_TEST_EN_PIN    LIN0_EN_PIN
#define LIN0_TEST_TX_PIN    LIN0_TX_PIN
#define LIN0_TEST_RX_PIN    LIN0_RX_PIN

#define LIN1_TEST_EN_PIN    LIN1_EN_PIN
#define LIN1_TEST_TX_PIN    LIN1_TX_PIN
#define LIN1_TEST_RX_PIN    LIN1_RX_PIN

#define LIN_TEST_BAUDRATE   20000 /*1k~20k*/
#define LIN_TEST_TIMEBASE   LIN_TIME_BASE_5MS
#define LIN_TEST_BREAK_LEN  LIN_BREAK13
#define LIN_TEST_BREAK1_LEN LIN_BREAK1_1

#define LIN_TEST_MASTER                 1 /*Test LIN Master Node*/
#define LIN_TEST_MASTER_SCHEDULE        2 /*Test LIN Master Node - using hardware schedule*/
#define LIN_TEST_SLAVE                  3 /*Test LIN Slave Node*/
#define LIN_TEST_SLAVE_FILTER           4 /*Test LIN Slave Node - using filter PID*/
#define LIN_TEST_MASTER_SLAVE           5 /*Test LIN0 Master Node, LIN1 Slave Node*/
#define LIN_TEST_MASTER_WEAKUP          6 /*Test LIN Master Sleep and Weak Up test*/
#define LIN_TEST_SLAVE_WEAKUP           7 /*Test LIN Slave Sleep and Weak Up test*/

#define LIN_TEST_MODE                   LIN_TEST_MASTER

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
