/********************************************************************************************************
 * @file    pke_app_config_1v0.h
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
#ifndef __APP_CONFIG__H_
#define __APP_CONFIG__H_
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif

#define PKE_ECDH 1
#define PKE_ECDSA 2
#define PKE_RSA 3
#define PKE_X25519 3
#define PKE_TEST_MODE PKE_ECDH


#include "driver.h"



/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif

#endif /* __APP_CONFIG__H_ */
