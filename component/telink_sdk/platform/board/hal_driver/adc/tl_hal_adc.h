/********************************************************************************************************
 * @file    tl_hal_adc.h
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
#if defined(MCU_CORE_TL321X)
#include "compiler.h"
#include "gpio.h"
#include "reg_include/register.h"

typedef enum
{
    HAL_NDMA = 1 | (0 << 4),
    HAL_DMA  = 1 | (1 << 4),
} tl_hal_transfer_mode_e;

typedef enum
{
    HAL_GPIO_MODE,
} tl_hal_sample_mode_e;

typedef enum
{
#if defined(MCU_CORE_TL321X)
    HAL_ADC_GPIO_P0_0      = ADC_GPIO_PB0,
#elif defined(MCU_CORE_TL323X)
    HAL_ADC_GPIO_P0_0      = SD_ADC_GPIO_PB0P,
#endif

} tl_hal_adc_gpio_input_e;


typedef enum
{
#if defined(MCU_CORE_TL321X)
    HAL_ADC_PRESCALE_1F4 = ADC_PRESCALE_1F4,
#elif defined(MCU_CORE_TL323X)
    HAL_ADC_PRESCALE_1F4 = SD_ADC_GPIO_P_CHN_DIV_1F4,
#endif
} tl_hal_adc_pre_scale_e;



/**
 * @brief      This function is used to initialize the ADC.
 * @param[in]  mode - transfer_mode
 * @param[in]  chn - the DMA channel
 * @return     none
 */
void tl_hal_adc_init(tl_hal_transfer_mode_e mode,dma_chn_e chn);

/**
 * @brief This function is used to initialize the ADC for gpio sampling.
 * @param[in]  pin - enum variable of ADC analog positive input IO.
 * @param[in]  prescale - enum variable of ADC pre_scaling factor.
 * @return none
 */
void tl_hal_adc_config(tl_hal_sample_mode_e mode, tl_hal_adc_gpio_input_e pin, tl_hal_adc_pre_scale_e prescale);


/**
 * @brief    This function is used to power on/off sar_adc.
 * @param[in] on - 1:on; 0:off.
 * @return   none.
 */
void tl_hal_adc_set_power(bool on);

/**
 * @brief      This function serves to get adc voltage value.
 * @param[in]  sample_cnt - sample number.
 * @return     adc_result   - adc voltage value.
 */
unsigned int tl_hal_adc_get_vol(unsigned int sample_cnt);
#endif
