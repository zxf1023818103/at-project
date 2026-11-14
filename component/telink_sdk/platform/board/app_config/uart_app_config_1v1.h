/********************************************************************************************************
 * @file    uart_app_config_1v1.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2018
 *
 * @par     Copyright (c) 2018, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#include "common.h"
/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @brief Define system clock
 */
#define CLOCK_SYS_CLOCK_HZ 24000000

/**
 * @note The sys_clock.h file depends on the definition of CLOCK_SYS_CLOCK_HZ so CLOCK_SYS_CLOCK_HZ must be placed before #include "sys_clock.h" without reversing the order.
 */

#if MCU_CORE_TL523X
#define UART_TX_PIN             GPIO_FC_PA0
#define UART_RX_PIN             GPIO_FC_PA1
#define UART_CTS_PIN            GPIO_FC_PA2
#define UART_RTS_PIN            GPIO_FC_PA4
#else
#define UART_TX_PIN             GPIO_FC_PD0
#define UART_RX_PIN             GPIO_FC_PD1
#define UART_CTS_PIN            GPIO_FC_PD2
#define UART_RTS_PIN            GPIO_FC_PD3
#endif

#if MCU_CORE_TL523X
#define UART_RTX_PIN            GPIO_FC_PA0
#else
#define UART_RTX_PIN            GPIO_FC_PC7
#endif

#if MCU_CORE_TL523X
#define UART0_MODULE            0 /* UART0 */
#define UART1_MODULE            1 /* UART1 */
#else
#define UART0_MODULE            0 /* UART0 */
#endif

/* uart select */
#define UART_MODULE_SEL         UART0_MODULE

/******************set mode**********************/
#define UART_DMA_CHANNEL_RX  DMA0
#define UART_DMA_CHANNEL_TX  DMA1

#define UART_DMA_NONE         0  // UART not support mode
#define UART_DMA_MODE         1  // UART support mode

#if MCU_CORE_TL523X
#if (UART_MODULE_SEL == UART0_MODULE || UART_MODULE_SEL == UART1_MODULE)
#define UART_DMA         UART_DMA_MODE     //uart use dma
#else
#define UART_DMA         UART_DMA_NONE      //uart not support dma
#endif
#endif
#define UART_NDMA           2     //uart not use dma

#define UART_MODE         UART_NDMA

/***********set uart_dma interrupt type*************/
#define UART_RXDMA_IRQ   1

#define UART_DMA_INT_TYPE UART_RXDMA_IRQ




#define NONE            0
#define USE_CTS         1
#define USE_RTS         2

#define FLOW_CTR        NONE

#define UART_1WIRE_MODE       0  //tx and rx is a same line
#define UART_2WIRE_MODE       1  //tx and rx are two lines
#define UART_WIRE_MODE        UART_2WIRE_MODE
#if( FLOW_CTR==USE_CTS)
    #define STOP_VOLT         1            //0 :Low level stops TX.  1 :High level stops TX.
#elif (FLOW_CTR==USE_RTS)
    #define RTS_MODE        UART_RTS_MODE_AUTO             //It can be UART_RTS_MODE_AUTO/UART_RTS_MODE_MANUAL.
    #define RTS_THRESH        5            //UART_RTS_MODE_AUTO need.It indicates RTS trigger threshold.
    #define RTS_INVERT        0            //UART_RTS_MODE_AUTO need.1 indicates RTS_pin will change from low to high.
    #define RTS_POLARITY      0            //UART_RTS_MODE_MANUAL need. It indicates RTS_POLARITY .
#endif

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
