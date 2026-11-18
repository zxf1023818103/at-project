/********************************************************************************************************
 * @file    can_app_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2025
 *
 * @par     Copyright (c) 2025, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
extern "C" {
#endif

#if defined(MCU_CORE_TL322X)
    #define   CAN0_GPIO_TX_PIN             GPIO_FC_PB3
    #define   CAN0_GPIO_RX_PIN             GPIO_FC_PB2
    #define   CAN1_GPIO_TX_PIN             GPIO_FC_PB5
    #define   CAN1_GPIO_RX_PIN             GPIO_FC_PB4
#elif defined(MCU_CORE_TL521X)
    #define   CAN0_GPIO_TX_PIN             GPIO_FC_PB6
    #define   CAN0_GPIO_RX_PIN             GPIO_FC_PB7
#else
#endif

#define   PULL_WAKEUP_SRC_PB2   GPIO_PIN_UP_DOWN_FLOAT
#define   PULL_WAKEUP_SRC_PB3   GPIO_PIN_UP_DOWN_FLOAT
#define   PULL_WAKEUP_SRC_PB4   GPIO_PIN_UP_DOWN_FLOAT
#define   PULL_WAKEUP_SRC_PB5   GPIO_PIN_UP_DOWN_FLOAT

#define   MB_CAN_MODE                         0
#define   MB_CAN_FD_MODE                      1
#define   LEGACY_RXFIFO_MODE                  2
#define   ENHANCED_RXFIFO_MODE                3
#define   REMOTE_FRAME_MODE                   4
#define   CAN_PN_MODE                         5
#define   CAN_TX_MODE                         6
#define   CAN_TX_FD_MODE                      7
#define   CAN_LOOPBACK_MODE                   8

#define   CAN_TEST                             CAN_TX_FD_MODE

#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
