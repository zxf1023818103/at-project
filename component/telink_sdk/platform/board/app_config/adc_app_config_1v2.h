/********************************************************************************************************
 * @file    adc_app_config_1v2.h
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
#if defined(MCU_CORE_TL322X)
#define  GPIO_M_CHNP_SAMPLE_SIGNAL     ADC0_GPIO_PC0P
#define  GPIO_M_CHNN_SAMPLE_SIGNAL     ADC0_GPIO_PC1N
#define  GPIO_L_CHNP_SAMPLE_SIGNAL     ADC0_GPIO_PC2P
#define  GPIO_L_CHNN_SAMPLE_SIGNAL     ADC0_GPIO_PC3N
#define  GPIO_R_CHNP_SAMPLE_SIGNAL     ADC0_GPIO_PC4P
#define  GPIO_R_CHNN_SAMPLE_SIGNAL     ADC0_GPIO_PC5N
#endif
#include "driver.h"
#define NORMAL_MODE             1
#define TEST_MODE               2 // For internal testing, users don't need to care
#define DEMO_MODE               NORMAL_MODE

#define ADC_SIGNAL_ADC_RUNNING_MODE   0
#define ADC_DOUBLE_ADC_RUNNING_MODE   1

#define ADC_MODE ADC_DOUBLE_ADC_RUNNING_MODE

#define ADC_NDMA_MODE           1
#define ADC_DMA_INTERRUPT_MODE  2
#define ADC_DMA_CHAIN_MODE      3

#define  ADC_SLE_DMA_MODE        ADC_NDMA_MODE

#if ADC_SLE_DMA_MODE != ADC_NDMA_MODE
#define  ADC_TRIGGER_FEATURE    0  // 0 disable  1 enable

    #if ADC_TRIGGER_FEATURE
#define TRIGGER_CNT            1   //The quantity of data collected after triggering
    #endif
#endif


#define ADC0_MODULE   0 //ADC0
#define ADC1_MODULE   1 //ADC1

#define ADC_MODULE_SEL ADC0_MODULE
#define ADC_GPIO_SAMPLE         0
#define ADC_VBAT_SAMPLE         1
#if INTERNAL_TEST_FUNC_EN
#define ADC_TEMP_SENSOR_SAMPLE         2
#endif

//In NDMA mode and DMA , only M channel can be used.
#define NDMA_M_1_CHN_EN     1
//Multiple channels can be used in DMA mode.
#define DMA_M_1_CHN_EN      1//When using one channels in DMA mode, only M channel can be selected.
#define DMA_M_L_2_CHN_EN    2//When using two channels in DMA mode, only M and L channels can be selected.
#define DMA_M_L_R_3_CHN_EN  3

// The maximum value is set at 10,000.
#define  ADC_SAMPLE_GROUP_CNT       8//Number of adc sample codes per channel.

#define  ADC_FEATURE_MODE           ADC_NO_FEATURE

#if (ADC_MODE == ADC_SIGNAL_ADC_RUNNING_MODE)
    #if (ADC_SLE_DMA_MODE != ADC_NDMA_MODE )
#define  ADC_DMA_CHN                DMA6
#define  ADC_SAMPLE_CHN_CNT         NDMA_M_1_CHN_EN  //Number of channels enabled
#define  ADC_M_CHN_SAMPLE_MODE      ADC_GPIO_SAMPLE
#define  ADC_L_CHN_SAMPLE_MODE      ADC_VBAT_SAMPLE
#define  ADC_R_CHN_SAMPLE_MODE      ADC_GPIO_SAMPLE
    #else
#define  ADC_SAMPLE_CHN_CNT         NDMA_M_1_CHN_EN
#define  ADC_SAMPLE_MODE            ADC_GPIO_SAMPLE
    #endif
#elif (ADC_MODE == ADC_DOUBLE_ADC_RUNNING_MODE)
    #if (ADC_SLE_DMA_MODE != ADC_NDMA_MODE )
#define SAR0_DMA_CHN    DMA7
#define SAR1_DMA_CHN    DMA6
#define  ADC_SAR0_SAMPLE_CHN_CNT         DMA_M_L_R_3_CHN_EN  //Number of channels enabled
#define  ADC_SAR1_SAMPLE_CHN_CNT         DMA_M_L_R_3_CHN_EN  
#define  ADC_SAR0_M_CHN_SAMPLE_MODE      ADC_GPIO_SAMPLE
#define  ADC_SAR1_M_CHN_SAMPLE_MODE      ADC_GPIO_SAMPLE   
#define  ADC_SAR0_L_CHN_SAMPLE_MODE      ADC_VBAT_SAMPLE
#define  ADC_SAR1_L_CHN_SAMPLE_MODE      ADC_GPIO_SAMPLE
#define  ADC_SAR0_R_CHN_SAMPLE_MODE      ADC_GPIO_SAMPLE
#define  ADC_SAR1_R_CHN_SAMPLE_MODE      ADC_VBAT_SAMPLE
    #else
#define  ADC_SAMPLE_CHN_CNT         NDMA_M_1_CHN_EN
#define  ADC_SAMPLE_MODE            ADC_GPIO_SAMPLE

    #endif
#endif 

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
