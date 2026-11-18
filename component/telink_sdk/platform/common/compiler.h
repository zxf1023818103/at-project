/********************************************************************************************************
 * @file    compiler.h
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
#ifndef COMPILER_H_
#define COMPILER_H_

#if defined(MCU_CORE_TL752X)|| defined(MCU_CORE_TL651X)
#define _attribute_ram_code_sec_            __attribute__((section(".ilm_code")))
#define _attribute_ram_code_sec_noinline_   __attribute__((section(".ilm_code"))) __attribute__((noinline))

#define _attribute_text_sec_                __attribute__((section(".flash"))) __attribute__((noinline)) //Inlining happens when __attribute__((noinline)) is not added.
#define _attribute_text_sec_optimize_o2_    __attribute__((section(".flash"))) __attribute__((optimize("O2"))) __attribute__((noinline)) __attribute__((no_execit))


#define _attribute_retention_ram_code_sec_            __attribute__((section(".retention_ram_code")))
#define _attribute_retention_ram_code_sec_noinline_   __attribute__((section(".retention_ram_code"))) __attribute__((noinline))

#define _attribute_retention_ram_data_sec_            __attribute__((section(".retention_data")))
#define _attribute_data_retention_sec_                _attribute_retention_ram_data_sec_


#ifndef STD_GCC //standard open source risc-V GCC
#define _attribute_flash_code_sec_noinline_     __attribute__((section(".flash"))) __attribute__((optimize("O2"))) __attribute__((noinline)) __attribute__((no_execit))
#else
#define _attribute_flash_code_sec_noinline_     __attribute__((section(".flash"))) __attribute__((noinline))
#endif /* end of STD_GCC. */

#define _attribute_aligned_(s)              __attribute__((aligned(s)))

/**
 *  _always_inline needs to be added in the following two cases:
 * 1. The subfunctions in the pm_sleep_wakeup function need to use _always_inline and _attribute_ram_code_sec_noinline_, as detailed in the internal comments of pm_sleep_wakeup.
 * 2. The BLE SDK uses interrupt preemption, flash interface operations can be interrupted by high-priority interrupts, which requires that the high-priority interrupt handler function,can not have a text segment of code.
 *    So the BLE SDK provides the following requirements: Add the following function to _always_inline: rf_set_tx_rx_off, rf_set_ble_crc_adv, rf_set_ble_crc_value, rf_set_rx_maxlen, stimer_get_tick, clock_time_exceed, rf_receiving_flag, dma_config, gpio_toggle, gpio_set_low_level, gpio_set_level.
 */
#define _always_inline inline __attribute__((always_inline))


/**
 * No_execit must be added here for the following reasons: When compiling at the optimization level of -Os, link may use exec.it for functions compiled at -O2. To disable this behavior,
 * add -mno-exit to the linking phase (see Andes Programming Guide), or add _attribute_((no_execit)) to functions that don't want to use exec.it.
 */
#ifndef STD_GCC //standard open source risc-V GCC
    #define _attribute_ram_code_sec_optimize_o2_          __attribute__((section(".ilm_code"))) __attribute__((optimize("O2"))) __attribute__((no_execit))
    #define _attribute_ram_code_sec_optimize_o2_noinline_ __attribute__((noinline)) __attribute__((section(".ilm_code"))) __attribute__((optimize("O2"))) __attribute__((no_execit))
#else
    #define _attribute_ram_code_sec_optimize_o2_          __attribute__((section(".ilm_code"))) __attribute__((optimize("O2")))
    #define _attribute_ram_code_sec_optimize_o2_noinline_ __attribute__((noinline)) __attribute__((section(".ilm_code"))) __attribute__((optimize("O2")))
#endif /* end of STD_GCC. */

#else

#define _attribute_ram_code_sec_            __attribute__((section(".ram_code")))
#define _attribute_ram_code_sec_noinline_   __attribute__((section(".ram_code"))) __attribute__((noinline))

#define _attribute_text_sec_                __attribute__((section(".text"))) __attribute__((noinline)) //Inlining happens when __attribute__((noinline)) is not added.
#define _attribute_text_sec_optimize_o2_    __attribute__((section(".text"))) __attribute__((optimize("O2"))) __attribute__((noinline)) __attribute__((no_execit))

#ifndef STD_GCC //standard open source risc-V GCC
#define _attribute_flash_code_sec_noinline_     __attribute__((section(".flash_code"))) __attribute__((optimize("O2"))) __attribute__((noinline)) __attribute__((no_execit))
#else
#define _attribute_flash_code_sec_noinline_     __attribute__((section(".flash_code"))) __attribute__((noinline))
#endif /* end of STD_GCC. */

#define _attribute_aes_data_sec_            __attribute__((section(".aes_data")))

#define _attribute_data_retention_sec_      __attribute__((section(".retention_data")))

#define _attribute_aligned_(s)              __attribute__((aligned(s)))

#if defined(MCU_CORE_TL322X)
#define _attribute_nvm_code_sec_           __attribute__((section(".nvm_code")))
#define _attribute_nvm_code_sec_noinline_  __attribute__((section(".nvm_code"))) __attribute__((noinline))
#define _attribute_nvm_rodata_sec_         __attribute__((section(".nvm_rodata")))
#define _attribute_nvm_rwdata_nocopy_sec_  __attribute__((section(".nvm_rwdata_nocopy"))) /* Variables stored in NVM, read-write attribute, NO data copy from NVM to RAM at startup. */
#define _attribute_nvm_rwdata_copy_sec_    __attribute__((section(".nvm_rwdata_copy")))   /* Variables stored in NVM, read-write attribute, copy data from NVM to RAM at startup. */
#endif

/**
 *  _always_inline needs to be added in the following two cases:
 * 1. The subfunctions in the pm_sleep_wakeup function need to use _always_inline and _attribute_ram_code_sec_noinline_, as detailed in the internal comments of pm_sleep_wakeup.
 * 2. The BLE SDK uses interrupt preemption, flash interface operations can be interrupted by high-priority interrupts, which requires that the high-priority interrupt handler function,can not have a text segment of code.
 *    So the BLE SDK provides the following requirements: Add the following function to _always_inline: rf_set_tx_rx_off, rf_set_ble_crc_adv, rf_set_ble_crc_value, rf_set_rx_maxlen, stimer_get_tick, clock_time_exceed, rf_receiving_flag, dma_config, gpio_toggle, gpio_set_low_level, gpio_set_level.
 */
#define _always_inline inline __attribute__((always_inline))

#define _attribute_no_inline_               __attribute__((noinline))
/**
 * No_execit must be added here for the following reasons: When compiling at the optimization level of -Os, link may use exec.it for functions compiled at -O2. To disable this behavior,
 * add -mno-exit to the linking phase (see Andes Programming Guide), or add _attribute_((no_execit)) to functions that don't want to use exec.it.
 */
#ifndef STD_GCC //standard open source risc-V GCC
    #define _attribute_ram_code_sec_optimize_o2_          __attribute__((section(".ram_code"))) __attribute__((optimize("O2"))) __attribute__((no_execit))
    #define _attribute_ram_code_sec_optimize_o2_noinline_ __attribute__((noinline)) __attribute__((section(".ram_code"))) __attribute__((optimize("O2"))) __attribute__((no_execit))
#else
    #define _attribute_ram_code_sec_optimize_o2_          __attribute__((section(".ram_code"))) __attribute__((optimize("O2")))
    #define _attribute_ram_code_sec_optimize_o2_noinline_ __attribute__((noinline)) __attribute__((section(".ram_code"))) __attribute__((optimize("O2")))
#endif /* end of STD_GCC. */

#endif /* end of MCU_CORE_TL752X */

/// Pack a structure field
#define __PACKED __attribute__((__packed__))



/**********************************************************************************************************************
 *   If you need to optimize the code size, you can modify the following configuration according to the situation.
 *   Note:
 *   -Before modifying, please confirm whether the function is needed, in case the needed function is deleted by mistake.
 *   -If you modify the following configuration options, please regenerate the library file
 *********************************************************************************************************************/
#ifndef BLC_PM_EN
#define BLC_PM_EN                           0
#endif
#ifndef BLC_PM_DEEP_RETENTION_MODE_EN
#define BLC_PM_DEEP_RETENTION_MODE_EN       1/**< 0:only Support sleep modes:suspend/deep */
                                             /**< 1:support all sleep modes:suspend/deep/deep retention */
#endif

#endif
