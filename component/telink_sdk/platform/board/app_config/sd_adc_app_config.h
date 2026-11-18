/********************************************************************************************************
 * @file    sd_adc_app_config.h
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


/**********************************************************************************************************************
 *                                         Users do not need to modify                                                *
 *********************************************************************************************************************/
#define SD_ADC_GPIO_MODE        1
#define SD_ADC_VBAT_MODE        2
#if !defined(MCU_CORE_TL521X) || (defined(MCU_CORE_TL521X) && SD_ADC_INTERNAL_TEST_FUNC_EN)
#define SD_ADC_TEMP_MODE        3
#endif
#define NDMA_POLLING_MODE        1
#define DMA_INTERRUPT_MODE       2

/**********************************************************************************************************************
 *                                         Users can modify macros                                                    *
 *********************************************************************************************************************/

/**
 * @attention -# In NDMA_INTERRUPT_MODE mode, SD_ADC_SAMPLE_CNT is configurable 8-16 and needs to be a multiple of 2 because the rx fifo is 16 words deep.
 *            -# In NDMA_POLLING_MODE and DMA_INTERRUPT_MODE, SD_ADC_SAMPLE_CNT needs to be >= 8 and a multiple of 2.
 *            -# For tl521x, The SD_ADC_TEMP_MODE is only used for internal testing. Users are not advised to use it.
 */
#define SD_ADC_SAMPLE_CNT  16

#define SD_ADC_DMA_CHN     DMA2

#define SD_ADC_MODE        SD_ADC_GPIO_MODE

#define SAMPLE_MODE        NDMA_POLLING_MODE

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
