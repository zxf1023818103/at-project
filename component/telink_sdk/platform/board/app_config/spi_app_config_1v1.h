/********************************************************************************************************
 * @file    spi_app_config_1v1.h
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
#if defined(__cplusplus)
extern "C"
{
#endif


    /* Enable C linkage for C++ Compilers: */

#if defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL753X)
#define PULL_WAKEUP_SRC_PA0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PA1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PA2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE2 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE3 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE4 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE5 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE6 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE7 GPIO_PIN_UP_DOWN_FLOAT

#endif

#define SPI_NDMA_MODE 1        //spi master and slave demo without DMA
#define SPI_DMA_MODE  2        //spi master and slave demo with DMA
#if !defined(MCU_CORE_B92)
    #define SPI_DMA_LLP_MODE 3 //spi rx with DMA chain transmission
#endif

#define SPI_MODE SPI_DMA_MODE


#include "driver.h"

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
