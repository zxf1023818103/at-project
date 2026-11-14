/********************************************************************************************************
 * @file    tahdt_app_config.h
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
extern "C"
{
#endif

#include "driver.h"
#define TAHDT_DEMO_STX        0
#define TAHDT_DEMO_SRX        1

#define TAHDT_DEMO_STX2RX     2
#define TAHDT_DEMO_SRX2TX     3

#define TAHDT_DEMO_AUTO_TEST  5
#define TAHDT_PER_TEST        6

#define TAHDT_DEMO            TAHDT_DEMO_STX

#define TX_IO_HIGH()
#define TX_IO_LOW()

#define RX_IO_HIGH()
#define RX_IO_LOW()

#define TIMEOUT_IO_HIGH()
#define TIMEOUT_IO_LOW()

#define ERROR_IO_HIGH()
#define ERROR_IO_LOW()

#define HEC_IO_HIGH()
#define HEC_IO_LOW()

#define CRC_IO_HIGH()
#define CRC_IO_LOW()


void tahdt_demo_init(void);
void tahdt_demo(void);

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
