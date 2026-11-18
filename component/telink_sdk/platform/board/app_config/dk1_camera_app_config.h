/********************************************************************************************************
 * @file    dk1_camera_app_config.h
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
#include "mlxxxxx_pin_map_config.h"

#if defined(MCU_CORE_TL721X)

#define LCD_SPI_MODE                 SPI_MODE0 //CPOL=0, CPHA=0
#define LCD_PIN_FUNC_SPI_CLK         GSPI_CK_IO
#define LCD_PIN_FUNC_SPI_CSN         GSPI_CN0_IO
#define LCD_PIN_FUNC_SPI_MOSI        GSPI_MOSI_IO

#define LCD_CFG_SPI_DMA_CHN          DMA3
#define LCD_CFG_SPI_CLK              24000000

#define CAMERA_SPI0_MODULE            LSPI_MODULE
#define CAMERA_PIN_FUNC_SPI0_CLK      0
#define CAMERA_PIN_FUNC_SPI0_CSN      LSPI_CN_IO
#define CAMERA_PIN_FUNC_SPI0_MOSI     0
#define CAMERA_PIN_FUNC_SPI0_MISO     0
#define CAMERA_PIN_FUNC_SPI0_IO2      0
#define CAMERA_PIN_FUNC_SPI0_IO3      0

#define CAMERA_SPI1_MODULE            GSPI_MODULE
#define CAMERA_PIN_FUNC_SPI1_CLK      GSPI_CK_IO
#define CAMERA_PIN_FUNC_SPI1_CSN      GSPI_CN0_IO
#define CAMERA_PIN_FUNC_SPI1_MOSI     GSPI_MOSI_IO
#define CAMERA_PIN_FUNC_SPI1_MISO     GSPI_MISO_IO
#define CAMERA_PIN_FUNC_SPI1_IO2      GSPI_IO2_IO
#define CAMERA_PIN_FUNC_SPI1_IO3      GSPI_IO3_IO

#define CAMERA_PIN_FUNC_PWM           PWM0
#define CAMERA_XTAL_PWM_ID            PWM0_ID
#define CAMERA_XTAL_PWM_FLD           FLD_PWM0_EN
#define CAMERA_XTAL_PWM_MAX           10
#define CAMERA_XTAL_PWM_CMP           5

#elif defined(MCU_CORE_TL322X)

#define LCD_SPI_MODE                 SPI_MODE0 //CPOL=0, CPHA=0
#define LCD_PIN_FUNC_SPI_CLK         GSPI_CK_IO
#define LCD_PIN_FUNC_SPI_CSN         GSPI_CN_IO
#define LCD_PIN_FUNC_SPI_MOSI        GSPI_MOSI_IO

#define LCD_CFG_SPI_DMA_CHN          DMA3
#define LCD_CFG_SPI_CLK              24000000

#define CAMERA_SPI0_MODULE            LSPI_MODULE
#define CAMERA_PIN_FUNC_SPI0_CLK      LSPI_CK_IO
#define CAMERA_PIN_FUNC_SPI0_CSN      LSPI_CN_IO
#define CAMERA_PIN_FUNC_SPI0_MOSI     LSPI_MOSI_IO
#define CAMERA_PIN_FUNC_SPI0_MISO     LSPI_MISO_IO
#define CAMERA_PIN_FUNC_SPI0_IO2      LSPI_IO2_IO
#define CAMERA_PIN_FUNC_SPI0_IO3      LSPI_IO3_IO

#define CAMERA_SPI1_MODULE            GSPI_MODULE
#define CAMERA_PIN_FUNC_SPI1_CLK      GSPI_CK_IO
#define CAMERA_PIN_FUNC_SPI1_CSN      GSPI_CN_IO
#define CAMERA_PIN_FUNC_SPI1_MOSI     GSPI_MOSI_IO
#define CAMERA_PIN_FUNC_SPI1_MISO     GSPI_MISO_IO
#define CAMERA_PIN_FUNC_SPI1_IO2      GSPI_IO2_IO
#define CAMERA_PIN_FUNC_SPI1_IO3      GSPI_IO3_IO

#define CAMERA_PIN_FUNC_PWM           PWM1
#define CAMERA_XTAL_PWM_ID            PWM1_ID
#define CAMERA_XTAL_PWM_FLD           FLD_PWM1_EN
#define CAMERA_XTAL_PWM_MAX           6
#define CAMERA_XTAL_PWM_CMP           3

#endif
                                               
#define LCD_PIN_SPI_CSN              PIN19     //PH5
#define LCD_PIN_SPI_CLK              PIN20     //PH6
#define LCD_PIN_SPI_MOSI             PIN40     //PD4
#define LCD_PIN_BL                   PIN41     //PA1
#define LCD_PIN_RST                  PIN39     //PD5
#define LCD_PIN_DC                   PIN57     //PC2
#define CAMERA_PIN_CIS_XCLK          PIN8
#define CAMERA_PIN_CIS_SDA           PIN35
#define CAMERA_PIN_CIS_SCL           PIN38
#define CAMERA_PIN_CIS_VS            PIN6
#define CAMERA_PIN_CIS_RSTB          PIN30
#define CAMERA_PIN_CIS_PWDN          PIN29
#define CAMERA_PIN_CIS_HS            PIN7
#define CAMERA_PIN_CIS_PCLK          PIN17
#define CAMERA_PIN_CIS_D0            PIN16
#define CAMERA_PIN_CIS_D1            PIN15
#define CAMERA_PIN_CIS_D2            PIN14
#define CAMERA_PIN_CIS_D3            PIN13
#define CAMERA_PIN_CIS_HS2           PIN31
#define CAMERA_PIN_CIS_PCLK2         PIN53
#define CAMERA_PIN_CIS_D4            PIN28
#define CAMERA_PIN_CIS_D5            PIN18
#define CAMERA_PIN_CIS_D6            PIN12
#define CAMERA_PIN_CIS_D7            PIN9

#define LCD_DISPLAY_ON               1 // lcd display enable
#define LCD_DISPLAY_DEBUG_IMAGE      0 // display image
#define LCD_DISPLAY_COLOR_BAR        0 // display color bar

#define SPI0_MODULE_ON               1 // spi0 module enable
#define SPI1_MODULE_ON               1 // spi1 module enable

#define SWITCH_PINS_SPI0_SPI1        0 // switch pins used for spi0 and spi1

#define SWITCH_DMA_BUFFER            0 // switch dma buffer used for spi0 and spi1

#define CIS_OV7670_INIT_ON           1 // enable ov7670 init

#if !SWITCH_PINS_SPI0_SPI1
#define CAMERA_PIN_SPI0_CSN          PIN7
#define CAMERA_PIN_SPI0_CLK          PIN17
#define CAMERA_PIN_SPI0_MOSI         PIN16
#define CAMERA_PIN_SPI0_MISO         PIN15
#define CAMERA_PIN_SPI0_IO2          PIN14
#define CAMERA_PIN_SPI0_IO3          PIN13 
#define CAMERA_PIN_SPI1_CSN          PIN31 // PE5
#define CAMERA_PIN_SPI1_CLK          PIN53 // PE6
#define CAMERA_PIN_SPI1_MOSI         PIN28 // PB3
#define CAMERA_PIN_SPI1_MISO         PIN18 // PH4
#define CAMERA_PIN_SPI1_IO2          PIN12 // PD1
#define CAMERA_PIN_SPI1_IO3          PIN9  // PD3
#else
#define CAMERA_PIN_SPI0_CSN          PIN7
#define CAMERA_PIN_SPI1_CLK          PIN17 //PH3
#define CAMERA_PIN_SPI1_MOSI         PIN16 //PG3
#define CAMERA_PIN_SPI1_MISO         PIN15 //PG5
#define CAMERA_PIN_SPI1_IO2          PIN14 //PG4
#define CAMERA_PIN_SPI1_IO3          PIN13 //PD0

#define CAMERA_PIN_SPI1_CSN          PIN31
#define CAMERA_PIN_SPI0_CLK          PIN53
#define CAMERA_PIN_SPI0_MOSI         PIN28
#define CAMERA_PIN_SPI0_MISO         PIN18
#define CAMERA_PIN_SPI0_IO2          PIN12
#define CAMERA_PIN_SPI0_IO3          PIN9
#endif


/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
