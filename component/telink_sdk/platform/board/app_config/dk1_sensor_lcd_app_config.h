/********************************************************************************************************
 * @file    dk1_sensor_lcd_app_config.h
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
#include <sys/errno.h>
#include "mlxxxxx_pin_map_config.h"

#if defined(MCU_CORE_TL321X)
#define LCD_ST7789_DC GPIO_PE6
#else
#define LCD_ST7789_DC PIN13 //GPIO_PE5
#endif

#if defined(MCU_CORE_TL321X)
#define LCD_ST7789_CN GPIO_PB5
#else 
#define LCD_ST7789_CN PIN55 //GPIO_PD3
#endif

#if defined(MCU_CORE_TL321X)
#define LCD_ST7789_RTS GPIO_PE7
#else
#define LCD_ST7789_RTS PIN14 //GPIO_PE4
#endif

#if defined(MCU_CORE_TL321X)
#define LCD_ST7789_BL GPIO_PE2
#else 
#define LCD_ST7789_BL PIN15 //GPIO_PE3
#endif

#if defined(MCU_CORE_TL321X)
#define I2C_CLK_SPEED (400 * 1000) // must < 400KHz
#define I2C_SCL_PIN   GPIO_FC_PA4
#define I2C_SDA_PIN   GPIO_FC_PB7

#define SPI_CLK_SPEED (48 * 1000000)
#define SPI_MISO_PIN  GPIO_FC_PE2
#define SPI_MOSI_PIN  GPIO_FC_PE4
#define SPI_CLK_PIN   GPIO_FC_PE5
#define SPI_CSN_PIN   GPIO_FC_PB5
#else

#define SPI_CLK_SPEED (24 * 1000000)
// #define SPI_MISO_PIN  GPIO_PE3
// #define SPI_MOSI_PIN  GPIO_PE2
// #define SPI_CLK_PIN   GPIO_PE1
// #define SPI_CSN_PIN   GPIO_PD3
#define SPI_MISO_PIN  PIN15
#define SPI_MOSI_PIN  PIN16
#define SPI_CLK_PIN   PIN17
#define SPI_CSN_PIN   PIN55

#define I2C_CLK_SPEED (400 * 1000) // must < 400KHz
// #define I2C_SCL_PIN   GPIO_PC0
// #define I2C_SDA_PIN   GPIO_PB7
// #define DMICCLK_PIN   GPIO_PA2
// #define DMICDAT_PIN   GPIO_PB4
// #define SDMP_PIN      GPIO_PA0
// #define SDMN_PIN      GPIO_PA1
// #define D1            GPIO_PE1
// #define D2            GPIO_PE2
// #define SW4           GPIO_PE3
// #define SW5           GPIO_PF4
// #define SW6           GPIO_PE5
// #define SW12          GPIO_PA7
#define I2C_SCL_PIN   PIN38
#define I2C_SDA_PIN   PIN35
#define DMICCLK_PIN   PIN21
#define DMICDAT_PIN   PIN32
#define SDMP_PIN      PIN19
#define SDMN_PIN      PIN20
#define D1            PIN17
#define D2            PIN16
#define SW4           PIN15
#define SW5           PIN5
#define SW6           PIN13
#define SW12          PIN27

#endif

#define EXAMPLE_ALL     0
#define EXAMPLE_KEY     1
#define EXAMPLE_ADC     2
#define EXAMPLE_UART    3
#define EXAMPLE_LPS22HB 4
#define EXAMPLE_LSM9DS1 5
#define EXAMPLE_HTS221  6
#define EXAMPLE_LCD     7
#define EXAMPLE_SELECT  EXAMPLE_ALL
#define WAIT_OK(XXX)  \
    while (XXX) {     \
        delay_ms(10); \
    }


#if defined(MCU_CORE_TL322X)
#define I2C_CHN                                            I2C0
#define _i2c_set_pin(sda, scl)                             i2c_set_pin(I2C_CHN, sda, scl)
#define _i2c_master_init()                                 i2c_master_init(I2C_CHN)
#define _i2c_master_write(id, wr_data, wr_len)             i2c_master_write(I2C_CHN, id, wr_data, wr_len)
#define _i2c_master_read(id, rd_data, rd_len)              i2c_master_read(I2C_CHN, id, rd_data, rd_len)
#define _i2c_set_master_clk(div)                           i2c_set_master_clk(I2C_CHN, div)
#else
#define _i2c_set_pin(sda, scl)                             i2c_set_pin(sda, scl)
#define _i2c_master_init()                                 i2c_master_init()
#define _i2c_master_write(id, wr_data, wr_len)             i2c_master_write(id, wr_data, wr_len)
#define _i2c_master_read(id, rd_data, rd_len)              i2c_master_read(id, rd_data, rd_len)
#define _i2c_set_master_clk(div)                           i2c_set_master_clk(div)
#endif

static inline void board_iic_init(void)
{
    _i2c_set_pin((gpio_func_pin_e)I2C_SDA_PIN, (gpio_func_pin_e)I2C_SCL_PIN);
    _i2c_master_init();
    _i2c_set_master_clk((unsigned char)(sys_clk.pclk * 1000 * 1000 / (4 * I2C_CLK_SPEED)));
}

static inline void board_gpio_init(void)
{
#if defined(MCU_CORE_TL721X) || defined(MCU_CORE_TL322X)
    gpio_function_en(D1);
    gpio_output_en(D1);
    gpio_input_dis(D1);
    gpio_function_en(D2);
    gpio_output_en(D2);
    gpio_input_dis(D2);
    gpio_function_en(SW4);
    gpio_output_dis(SW4);
    gpio_input_en(SW4);
    gpio_set_up_down_res(SW4, GPIO_PIN_PULLUP_10K);
    gpio_function_en(SW5);
    gpio_output_dis(SW5);
    gpio_input_en(SW5);
    gpio_set_up_down_res(SW5, GPIO_PIN_PULLUP_10K);
    gpio_function_en(SW6);
    gpio_output_dis(SW6);
    gpio_input_en(SW6);
    gpio_set_up_down_res(SW6, GPIO_PIN_PULLUP_10K);
#endif
}

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
