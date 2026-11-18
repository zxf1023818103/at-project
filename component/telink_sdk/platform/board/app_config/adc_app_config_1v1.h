/********************************************************************************************************
 * @file    adc_app_config_1v1.h
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
/*
 * To prevent leakage,in gpio_init(), all GPIOs are set to High-impedance and also enable the pull-down resistor except the MSPI pins and SWS:
 * before using the corresponding io, need to cancel the pull-down or redefine the function as required based on the application scenario.
 */
#if defined(MCU_CORE_TL751X)
#define PULL_WAKEUP_SRC_PG0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PG2 GPIO_PIN_UP_DOWN_FLOAT
#endif

#if defined(MCU_CORE_TL751X)
    #define GPIO_M_CHN_SAMPLE_PIN ADC_GPIO_PG0
    #define GPIO_L_CHN_SAMPLE_PIN ADC_GPIO_PG1
    #define GPIO_R_CHN_SAMPLE_PIN ADC_GPIO_PG2
#elif defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL321X) || defined(MCU_CORE_TL322X)
    #define GPIO_M_CHN_SAMPLE_PIN ADC_GPIO_PB0
    #define GPIO_L_CHN_SAMPLE_PIN ADC_GPIO_PB1
    #define GPIO_R_CHN_SAMPLE_PIN ADC_GPIO_PB2
#endif




#include "driver.h"

    /**********************************************************************************************************************
 *                                         Users do not need to modify                                                *
 *********************************************************************************************************************/
#define NORMAL_MODE             1
#define TEST_MODE               2 // For internal testing, users don't need to care
#define DEMO_MODE               NORMAL_MODE


#define ADC_DMA_MODE         1
#define ADC_NDMA_MODE        2

#define ADC_GPIO_SAMPLE      1
/* 
 * 1. When performing vbat adc sampling,discard the first data as much as possible 
 *    because the vbat channel is weak and the first data may deviate from the actual value by a large amount.
 * 2. When the vbat adc acquisition voltage range is 3.6V~4.2V, 
 *    you need to set Power down of bypass switch (analog register 0x06 bit3 bit is set to 1) to power on ,
 *    set pd_pl_vbat_ldo_3v (analog register 0x05 bit6 bit is set to 0) to Power up.
 *    When the vbat adc acquisition voltage range is 0V~3.6V, verf reference voltage 
 *    you need to set Power down of bypass switch (analog register 0x06 bit3 bit is set to 0) to Power down ,
 *    set pd_pl_vbat_ldo_3v (analog register 0x05 bit6 bit is set to 1) to Power down.
 *    Any of the above configurations can be used within the 3.6V plus or minus 500mv range.
 * (updated by bolong.zhang, confirmed by haitao.gu at 20250219)
*/
#define ADC_VBAT_SAMPLE      2
#if INTERNAL_TEST_FUNC_EN
    #define ADC_GPIO_SAMPLE_VBAT 3 // For internal testing only
    #define ADC_TEMP_SENSOR_SAMPLE 4
#endif
//In NDMA mode, only M channel can be used.
#define NDMA_M_1_CHN_EN 1
//Multiple channels can be used in DMA mode.
#define DMA_M_1_CHN_EN 1 //When using one channels in DMA mode, only M channel can be selected.
/*
 * Due to issues with adc multichannel sampling on TL721X A2, it is not available to the public.
 * A2 chip has the problem of interference due to multi-channel.
 * In DMA mode, only m-channel can be used.(add by bolong.zhang,confirmed haitao 20241219)
 */
#define DMA_M_L_2_CHN_EN   2 //When using two channels in DMA mode, only M and L channels can be selected.//tl721X A2, tl751, tl321x not available.
#define DMA_M_L_R_3_CHN_EN 3 //tl721X A2, tl751, tl321x not available.

/**********************************************************************************************************************
 *                                         Users can modify macros                                                    *
 *********************************************************************************************************************/
/**
 *@attention  -# In NDMA/DMA mode, ADC_SAMPLE_GROUP_CNT must be multiple of 8.
 */
#define ADC_SAMPLE_GROUP_CNT 8 //Number of adc sample codes per channel.

#define ADC_MODE             ADC_NDMA_MODE

#if (ADC_MODE == ADC_DMA_MODE)
    #define ADC_DMA_CHN           DMA7
    #define ADC_SAMPLE_CHN_CNT    DMA_M_1_CHN_EN //Number of channels enabled
    #define ADC_M_CHN_SAMPLE_MODE ADC_GPIO_SAMPLE
    #define ADC_L_CHN_SAMPLE_MODE ADC_VBAT_SAMPLE
    #define ADC_R_CHN_SAMPLE_MODE ADC_GPIO_SAMPLE
#else
    #define ADC_SAMPLE_CHN_CNT NDMA_M_1_CHN_EN
    #define ADC_SAMPLE_MODE    ADC_GPIO_SAMPLE
#endif

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
