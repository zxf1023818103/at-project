/********************************************************************************************************
 * @file    soc_printf.h
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
#if defined(MCU_CORE_TL752X)
/*
 * @FilePath: \tx232_driver_src\components\soc_printf\soc_printf.h
 * @Version: V1.0.1
 * @Date: 2025-07-07 09:59:38
 * @Author: driver group
 * @Description: soc_printf
 * Copyright (C) 2025-2035 Expanse Micro, All Rights Reserved.
 */

#ifndef __SOC_PRINTF_H__
#define __SOC_PRINTF_H__

#ifdef __cplusplus
extern "C" { /* C declaration in C++ */
#endif       /* _cplusplus */

#include <stdarg.h>

int soc_printf(const char *str, ...);
int soc_sprintf(char *buffer, const char *str, ...);
int soc_vsprintf(char *buffer, const char *str, va_list ap);

#ifdef __cplusplus
}
#endif

#endif
#endif
