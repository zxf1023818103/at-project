/********************************************************************************************************
 * @file    i3c_app_config.h
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
extern "C" {
#endif


#define  NO_DMA_MASTER            0
#define  NO_DMA_SLAVE             1
#define  DMA_MASTER               2
#define  DMA_SLAVE                3
#define  TRANSFER_BUS_CONTROL     4
#define  TIMING_CONTROL           5
#define  TARGET_RESET             6
#define  GROUP_ADDRESS            7
#define  MAPPED_ADDRESS           8
#define  I3C_MODE                 NO_DMA_MASTER

#if defined(MCU_CORE_TL322X)
   #define I3C_GPIO_SDA_PIN GPIO_FC_PE0
   #define I3C_GPIO_SCL_PIN GPIO_FC_PE2
#elif defined(MCU_CORE_TL753X)
   #define I3C_GPIO_SDA_PIN GPIO_FC_PA2
   #define I3C_GPIO_SCL_PIN GPIO_FC_PA1
#elif defined(MCU_CORE_TL521X)
   #define I3C_GPIO_SDA_PIN GPIO_FC_PE0
   #define I3C_GPIO_SCL_PIN GPIO_FC_PE1
#endif

#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
