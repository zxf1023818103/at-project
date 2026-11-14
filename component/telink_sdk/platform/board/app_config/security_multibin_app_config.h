/********************************************************************************************************
 * @file    security_multibin_app_config.h
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
/**
    Note:
    All code in this demo is for internal use only and is not allowed to be open sourced to users.
    Security_MultiBin_SDK_Demo.bin is downloaded to address 0 of flash.
    Security_MultiBin_USER_Demo.bin is downloaded to address 0x60000 of flash.
    Pressing sw2 generates a gpio interrupt.
    you can see that the variable g_get_mainloop_cnt in Security_MultiBin_USER_Demo.bin accumulates
*/
#define IRQ_PIN KEY1

#include "driver.h"

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
