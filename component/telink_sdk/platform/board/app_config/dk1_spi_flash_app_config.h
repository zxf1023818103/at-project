/********************************************************************************************************
 * @file    dk1_spi_flash_app_config.h
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

#if defined(MCU_CORE_TL322X)

#if 0
#define CAMERA_PIN_CIS_HS            PIN7
#define CAMERA_PIN_CIS_PCLK          PIN17
#define CAMERA_PIN_CIS_D0            PIN16
#define CAMERA_PIN_CIS_D1            PIN15
#define CAMERA_PIN_CIS_D2            PIN14
#define CAMERA_PIN_CIS_D3            PIN13

#define FLASH_SPI_PIN_CSN        CAMERA_PIN_CIS_HS  
#define FLASH_SPI_PIN_CLK        CAMERA_PIN_CIS_PCLK
#define FLASH_SPI_PIN_MOSI       CAMERA_PIN_CIS_D1   
#define FLASH_SPI_PIN_MISO       CAMERA_PIN_CIS_D0
#define FLASH_SPI_PIN_IO2        CAMERA_PIN_CIS_D2  
#define FLASH_SPI_PIN_IO3        CAMERA_PIN_CIS_D3  
#else
#define FLASH_SPI_PIN_CSN         GPIO_PG2
#define FLASH_SPI_PIN_CLK         GPIO_PG6
#define FLASH_SPI_PIN_MOSI        GPIO_PH2
#define FLASH_SPI_PIN_MISO        GPIO_PH1
#define FLASH_SPI_PIN_IO2         GPIO_PH0
#define FLASH_SPI_PIN_IO3         GPIO_PG7

#define FLASH_PIN_WP              FLASH_SPI_PIN_IO2
#define FLASH_PIN_HOLD            FLASH_SPI_PIN_IO3

#endif

#define FLASH_SPI_MODULE_SEL      GSPI3_MODULE
#define FLASH_SPI_SPEED           48000000


#else

#define FLASH_SPI_PIN_CSN         GPIO_NONE
#define FLASH_SPI_PIN_CLK         GPIO_NONE
#define FLASH_SPI_PIN_MOSI        GPIO_NONE
#define FLASH_SPI_PIN_MISO        GPIO_NONE
#define FLASH_SPI_PIN_IO2         GPIO_NONE
#define FLASH_SPI_PIN_IO3         GPIO_NONE

#endif

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
