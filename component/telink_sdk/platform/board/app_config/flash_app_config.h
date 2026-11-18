/********************************************************************************************************
 * @file    flash_app_config.h
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


#include "driver.h"

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C"
{
#endif

#if defined(MCU_CORE_TL752X)||defined(MCU_CORE_TL651X)
#define NORMAL_MODE               1
#define FLASH_QUALITY_TEST        2
#define FLASH_WORK_VOLTAGE_TEST   3
#define FLASH_10W_TIME_TEST       4
#define DEMO_MODE                 NORMAL_MODE

#else

#define FLASH_TEST_MODE         1
#define FLASH_LPC_PROTECT_MODE  2
#define FLASH_DEMO_MODE         FLASH_TEST_MODE



/*
 * @note    Because the flash enabled QE function is not compatible with each flash chip, when adding a new flash chip,
 *          the ram bin that enables QE needs to be stored in svn and used for factory enabling flash QE function of ATE
 *          and it also can be used for if the ATE does not receive the notification of enabling flash QE function,
 *          however it needs to be enabled by Lidong and the fixture department in the future.
 */
#define FLASH_QE_ENABLE 1

/*
 * @note    It is used to verify the read, erase and write functions, lock and unlock functions��read decrypt of and umid functions of Flash,
 *          and the read, erase and write functions of the security registers.
 *          You can judge which functions pass the test through structures err_status and check_status.
 */
#define FLASH_TEST          2

#define FLASH_FUNCTION_MODE FLASH_TEST

#endif

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
