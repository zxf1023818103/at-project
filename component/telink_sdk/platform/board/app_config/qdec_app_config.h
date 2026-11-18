/********************************************************************************************************
 * @file    qdec_app_config.h
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
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif

#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PB6 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PB7 GPIO_PIN_UP_DOWN_FLOAT
#endif

#if defined(MCU_CORE_TL521X)
#define QDEC_MODULE QDEC0 // QDEC0 or QDEC1 for tl521x
#endif
#include "driver.h"


#define QDEC_32K_SPEED 32000 //pwm 32K

    enum
    {
        CLOCK_QDEC_32K_1S  = QDEC_32K_SPEED,
        CLOCK_QDEC_32K_1MS = (CLOCK_QDEC_32K_1S / 1000),
    };

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
