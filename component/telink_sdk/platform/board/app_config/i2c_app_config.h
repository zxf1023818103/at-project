/********************************************************************************************************
 * @file    i2c_app_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2026
 *
 * @par     Copyright (c) 2026, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

#define I2C_NUM I2C0

#if (!defined(MCU_CORE_TL752X))
#define I2C_MASTER_WRITE_READ_NO_DMA 1        //this mode can read and write data from fifo one byte a time
#define I2C_MASTER_WRITE_READ_DMA    2        //this mode can read and write data through DMA, four byte a time
#if !(defined(MCU_CORE_B91)||defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL521X))
    #define I2C1_M_MASTER_WRITE_READ_NO_DMA 3 //this mode can read and write data from fifo one byte a time. Only supports master.
#endif

#define I2C_MASTER_WRITE_READ_MODE I2C_MASTER_WRITE_READ_NO_DMA
#else
#define I2C_MASTER_POLLING    0
#define I2C_SLAVE_POLLING     1
#define I2C_MASTER_INTERRUPT  2
#define I2C_SLAVE_INTERRUPT   3

#define I2C_MODE      I2C_MASTER_POLLING

#endif
#if defined(MCU_CORE_B91)
    #define I2C_GPIO_SDA_PIN I2C_GPIO_SDA_B3
    #define I2C_GPIO_SCL_PIN I2C_GPIO_SCL_B2
#elif defined(MCU_CORE_B92)
    #define I2C_GPIO_SDA_PIN GPIO_FC_PB3
    #define I2C_GPIO_SCL_PIN GPIO_FC_PB2
#elif defined(MCU_CORE_TL7518)
    #if (I2C_MASTER_WRITE_READ_MODE == I2C1_M_MASTER_WRITE_READ_NO_DMA)
        #define I2C_GPIO_SDA_PIN I2C1_GPIO_SDA_B6
        #define I2C_GPIO_SCL_PIN I2C1_GPIO_SCL_B7
    #else
        #define I2C_GPIO_SDA_PIN I2C_GPIO_SDA_B6
        #define I2C_GPIO_SCL_PIN I2C_GPIO_SCL_B7
    #endif
#elif defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PB6 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB7 GPIO_PIN_UP_DOWN_FLOAT

    #if (I2C_MASTER_WRITE_READ_MODE == I2C1_M_MASTER_WRITE_READ_NO_DMA)
        #define I2C_GPIO_SDA_PIN I2C1_GPIO_SDA_B6
        #define I2C_GPIO_SCL_PIN I2C1_GPIO_SCL_B7
    #else
        #define I2C_GPIO_SDA_PIN I2C_GPIO_SDA_B6
        #define I2C_GPIO_SCL_PIN I2C_GPIO_SCL_B7
    #endif
#elif defined(MCU_CORE_TL721X)
    #define I2C_GPIO_SDA_PIN GPIO_FC_PB3
    #define I2C_GPIO_SCL_PIN GPIO_FC_PB2
#elif defined(MCU_CORE_TL321X)|| defined(MCU_CORE_TL323X) || defined(MCU_CORE_TL521X)
    #define I2C_GPIO_SDA_PIN GPIO_FC_PB5
    #define I2C_GPIO_SCL_PIN GPIO_FC_PB6
#elif defined(MCU_CORE_TL322X)|| defined(MCU_CORE_TL522X)
#define PULL_WAKEUP_SRC_PE0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PE1 GPIO_PIN_UP_DOWN_FLOAT
#define I2C_GPIO_SDA_PIN GPIO_FC_PE0
#define I2C_GPIO_SCL_PIN GPIO_FC_PE1

#elif defined(MCU_CORE_TL753X)
#define PULL_WAKEUP_SRC_PG0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG1 GPIO_PIN_UP_DOWN_FLOAT
#if (I2C_MASTER_WRITE_READ_MODE == I2C1_M_MASTER_WRITE_READ_NO_DMA)
    #define I2C_GPIO_SDA_PIN I2C1_GPIO_SDA_B6
    #define I2C_GPIO_SCL_PIN I2C1_GPIO_SCL_B7
#else
    #define I2C_GPIO_SDA_PIN I2C_GPIO_SDA_B4
    #define I2C_GPIO_SCL_PIN I2C_GPIO_SCL_B3
#endif
#endif


#include "driver.h"

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
