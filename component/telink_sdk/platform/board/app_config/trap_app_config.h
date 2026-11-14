/********************************************************************************************************
 * @file    trap_app_config.h
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

#include "driver.h"

#define INTERRUPT_NON_NESTED_DEMO 1 // external interrupt non nested
#define INTERRUPT_NESTED_DEMO     2 // external interrupt  nested

#define INTERRUPT_MTIME_DEMO      3 // machine time interrupt
#define INTERRUPT_SWI_DEMO        4 // software interrupt
#define EXT_MTIME_SWI_NESTED_DEMO 5 // external, mtime and swi interrupt

#define HSP_DEMO                  6 // Hardware Stack Protection
#define WFI_DEMO                  7 // (Wait-For-Interrupt Mode)


#define TRAP_DEMO                 EXT_MTIME_SWI_NESTED_DEMO


/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
