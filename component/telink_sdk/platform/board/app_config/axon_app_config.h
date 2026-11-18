/********************************************************************************************************
 * @file    axon_app_config.h
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
/**
 * Copyright (c) 2020-2021, Atlazo Inc.
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *     2. Unless for usage inside an authorized Axon-enabled integrated circuit, redistributions
 *     in binary form must reproduce the above copyright notice, this list of
 *     conditions and the following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 *     3. Neither the name of ATLAZO, nor the names of its contributors may be
 *     used to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 *     4. This software, with or without modification, must only be used with an authorized
 *     Axon-enabled integrated circuit. All other usages are subject to written permission
 *     from ATLAZO and different commercial license may apply.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. *
 */

#pragma once
//#include "../../drivers.h"
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif

#define NPE_BASE_ADDR     0x112000


#define GPIO_LED_WHITE    GPIO_PB6
#define GPIO_LED_GREEN    GPIO_PB5
#define GPIO_LED_BLUE     GPIO_PB4
#define GPIO_LED_RED      GPIO_PB7
#define LED_ON_LEVAL      1 //gpio output high voltage to turn on led

#define PB7_FUNC          AS_GPIO
#define PB6_FUNC          AS_GPIO
#define PB5_FUNC          AS_GPIO
#define PB4_FUNC          AS_GPIO

#define PB7_OUTPUT_ENABLE 1
#define PB6_OUTPUT_ENABLE 1
#define PB5_OUTPUT_ENABLE 1
#define PB4_OUTPUT_ENABLE 1


/*
 * configure PE[7:4] as JTAG interface
 * note: the "AS_Txx" doesn't really matter as long as it isn't "AS_GPIO"
 */
#define PE4_FUNC          AS_TDI
#define PE4_INPUT_ENABLE  1
#define PE4_OUTPUT_ENABLE 1

#define PE5_FUNC          AS_TDO
#define PE5_INPUT_ENABLE  1
#define PE5_OUTPUT_ENABLE 1

#define PE6_FUNC          AS_TMS
#define PE6_INPUT_ENABLE  1
#define PE6_OUTPUT_ENABLE 1

#define PE7_FUNC          AS_TCK
#define PE7_INPUT_ENABLE  1
#define PE7_OUTPUT_ENABLE 1


#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
