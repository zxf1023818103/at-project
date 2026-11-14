/********************************************************************************************************
 * @file    dk1_codec_app_config.h
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
#include "driver.h"
#include "common.h"
#include "audio.h"
#include "mlxxxxx_pin_map_config.h"

#if defined(MCU_CORE_TL321X)

#define LEDTEST1                 GPIO_PE5
#define LEDTEST2                 GPIO_PE4

#define I2S_TO_EXT_CODEC_USB     2 //I2S input->buff->I2S out for  external codec
#define I2S_TO_EXT_CODEC_MIC_SPK 3 //I2S input->buff->I2S out for  external codec

#define AUDIO_MODE               I2S_TO_EXT_CODEC_USB

#else
// #define LEDTEST1                     GPIO_PE1
// #define LEDTEST2                     GPIO_PE2
#define LEDTEST1                     PIN17
#define LEDTEST2                     PIN16

#define DMIC_INPUT_TO_BUF_TO_LINEOUT 1 //dmic_in->buff->SDM out
#define I2S_TO_EXT_CODEC_USB         2 //I2S input->buff->I2S out for  external codec. See AUDIO_I2S_TO_EXT_MODE to choose codec type
#define I2S_TO_EXT_CODEC_MIC_SPK     3 //I2S input->buff->I2S out for  external codec. See AUDIO_I2S_TO_EXT_MODE to choose codec type

#define AUDIO_MODE                   I2S_TO_EXT_CODEC_USB
#endif

/* Choose different external codec type */
#define I2S_TO_EXT_nau8821        1 //external codec
#define I2S_TO_EXT_es8389         2 //external codec
#define AUDIO_I2S_TO_EXT_MODE     I2S_TO_EXT_nau8821

typedef unsigned int sa_adc_t;
typedef unsigned int sa_dac_t;

void app_usb_handle_set_intf(int intf, int alt);

#if defined(MCU_CORE_B92) || defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X)
/**
 * @brief   The POWER_SUPPLY_MODE defaults to VBUS_POWER_SUPPLY.
 */
#define VBAT_POWER_SUPPLY 0
#define VBUS_POWER_SUPPLY 1
#define POWER_SUPPLY_MODE VBUS_POWER_SUPPLY
#endif

#if !defined(MCU_CORE_B91)
#define AISO     0
#define INT      1
#define USB_MODE INT
#endif

#define USB_MOUSE       1
#define USB_KEYBOARD    2
#define USB_MICROPHONE  3 /* TL7518 this version is not support microphone data->SRAM, it will be added later. */
#define USB_SPEAKER     4 /* TL7518 this version is not support SRAM data->speaker, it will be added later. */
#define USB_CDC         5
#define USB_MIC_SPEAKER 6 /* TL7518 this version is not support microphone data->SRAM and SRAM data->speaker, it will be added later. */
#if defined(MCU_CORE_B91)
#define USB_MASS_STORAGE 7
#endif
#define USB_PRINT \
    8 /* TL7518, TL721X, TL321X don't support hardware enumeration into print device.
                                 Please use software enumeration instead of hardware enumeration. */

#define USB_DEMO_TYPE USB_MIC_SPEAKER

#if (USB_DEMO_TYPE == USB_MOUSE)
#define USB_MOUSE_ENABLE 1
#elif (USB_DEMO_TYPE == USB_KEYBOARD)
#define USB_KEYBOARD_ENABLE 1
#elif (USB_DEMO_TYPE == USB_MICROPHONE)
#define USB_MIC_ENABLE 1
#elif (USB_DEMO_TYPE == USB_SPEAKER)
#define USB_SPEAKER_ENABLE 1
#elif (USB_DEMO_TYPE == USB_CDC)
#define USB_CDC_ENABLE 1
#elif (USB_DEMO_TYPE == USB_MIC_SPEAKER)
#ifndef USB_MIC_ENABLE
#define USB_MIC_ENABLE 1
#endif
#ifndef USB_SPEAKER_ENABLE
#define USB_SPEAKER_ENABLE 1
#endif
#elif (USB_DEMO_TYPE == USB_PRINT)
#define USB_PRINTER_ENABLE 1
#endif
#if defined(MCU_CORE_B91)
#if ((USB_DEMO_TYPE == USB_MASS_STORAGE))
#define USB_MASS_STORAGE_ENABLE 1

#define SYS_NOR_FLASH_SUPPORT   1 //sys NOR_FLASH  1.Please refer to "sys_norflash.h" for the allocated address and capacity,2.Must be formatted for the first use
#define SD_NAND_FLASH_SUPPORT   0 // External SD_NAND flash must be connected
#if (SD_NAND_FLASH_SUPPORT) && (SYS_NOR_FLASH_SUPPORT)
#define USB_DISK_NUM 1 //1 means 2 disk,0 means 1 disk, and so on
#elif (SD_NAND_FLASH_SUPPORT) || (SYS_NOR_FLASH_SUPPORT)
#define USB_DISK_NUM 0 //1 means 2 disk,0 means 1 disk, and so on
#endif
#endif
#endif

#define CHIP_VER_A0 0
#define CHIP_VER_A1 1
#define CHIP_VER    CHIP_VER_A1


#if defined(MCU_CORE_TL322X)
#define I2C_CHN                                            I2C0
#define _i2c_set_pin(sda, scl)                             i2c_set_pin(I2C_CHN, sda, scl)
#define _i2c_master_init()                                 i2c_master_init(I2C_CHN)
#define _i2c_master_write(id, wr_data, wr_len)             i2c_master_write(I2C_CHN, id, wr_data, wr_len)
#define _i2c_master_read(id, rd_data, rd_len)              i2c_master_read(I2C_CHN, id, rd_data, rd_len)
#define _i2c_set_master_clk(div)                           i2c_set_master_clk(I2C_CHN, div)
#define _i2c_set_tx_dma_config(chn)                        i2c_set_tx_dma_config(I2C_CHN, chn)
#define _i2c_set_rx_dma_config(chn)                        i2c_set_rx_dma_config(I2C_CHN, chn)
#define _i2c_master_busy()                                 i2c_master_busy(I2C_CHN)
#define _i2c_master_write_dma(id, wr_data, wr_len)         i2c_master_write_dma(I2C_CHN, id, wr_data, wr_len)
#define _i2c_master_read_dma(id, rd_data, rd_len)          i2c_master_read_dma(I2C_CHN, id, rd_data, rd_len)
#else
#define _i2c_set_pin(sda, scl)                             i2c_set_pin(sda, scl)
#define _i2c_master_init()                                 i2c_master_init()
#define _i2c_master_write(id, wr_data, wr_len)             i2c_master_write(id, wr_data, wr_len)
#define _i2c_master_read(id, rd_data, rd_len)              i2c_master_read(id, rd_data, rd_len)
#define _i2c_set_master_clk(div)                           i2c_set_master_clk(div)
#define _i2c_set_tx_dma_config(chn)                        i2c_set_tx_dma_config(chn)
#define _i2c_set_rx_dma_config(chn)                        i2c_set_rx_dma_config(chn)
#define _i2c_master_busy()                                 i2c_master_busy()
#define _i2c_master_write_dma(id, wr_data, wr_len)         i2c_master_write_dma(id, wr_data, wr_len)
#define _i2c_master_read_dma(id, rd_data, rd_len)          i2c_master_read_dma(id, rd_data, rd_len)
#endif


/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
