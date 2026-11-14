/********************************************************************************************************
 * @file    usb0_app_config.h
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
extern "C" {
#endif

#include "driver.h"

#define USB_MOUSE          1
#define USB_KEYBOARD       2
#define USB_MICROPHONE     3
#define USB_SPEAKER        4
#define USB_CDC            5
#define USB_MOUSE_KEYBOARD 6

#if defined(MCU_CORE_TL752X)
#define USB_MIC_SPK        7
#endif

#define USB_DEMO_TYPE      USB_MOUSE

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
