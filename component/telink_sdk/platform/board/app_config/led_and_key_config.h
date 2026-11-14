/********************************************************************************************************
 * @file    led_and_key_config.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2023
 *
 * @par     Copyright (c) 2023, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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

/**
    ===============================================================================
                         ##### LED and KEY #####
    ===============================================================================
    The default configuration of LEDs and KEYs match the following hardware revisions:
    B91 C1T213A20_V1_3_20200727
    B92 C1T266A20_V1_3_20220722
    TL721X C1T315A20_V1_0_20231211
    TL321X C1T335A20_V1.0_20240422
*/
#if defined(MCU_CORE_B91)
    #define LED1 GPIO_PB4
    #define LED2 GPIO_PB5
    #define LED3 GPIO_PB6
    #define LED4 GPIO_PB7
    /*
 * Button matrix table:
 *          KEY3    KEY4
 *  KEY1    SW2     SW3
 *  KEY2    SW4     SW5
 */
    #define KEY1 GPIO_PC2
    #define KEY2 GPIO_PC0
    #define KEY3 GPIO_PC3
    #define KEY4 GPIO_PC1
#elif defined(MCU_CORE_B92)
    #define LED3 GPIO_PD0
    #define LED4 GPIO_PD1
    #define LED1 GPIO_PE6
    #define LED2 GPIO_PE7
    /*
 * Button matrix table:
 *          KEY3    KEY4
 *  KEY1    SW2     SW3
 *  KEY2    SW4     SW5
 */
    #define KEY1 GPIO_PD2
    #define KEY2 GPIO_PD7
    #define KEY3 GPIO_PD6
    #define KEY4 GPIO_PF6
#elif defined(MCU_CORE_TL751X)
/*===============================================================================*/
 /**
  *                   ###Caution###
    ===============================================================================
   To prevent leakage,in gpio_init(), all GPIOs are set to High-impedance and also enable the pull-down resistor except the MSPI pins and SWS:
   before using the corresponding io, need to cancel the pull-down or redefine the function as required based on the application scenario.
   reference key and led configurations are as follows:
**/
 /** Example 1: for For JTAG applications

    #define PB1_FUNC AS_TCK //  as  D25&N22 TCK
    #define PB2_FUNC AS_TMS //  as  D25&N22 TMS
    #define PB3_FUNC AS_TDO //  as  D25&N22 TDO
    #define PB4_FUNC AS_TDI //  as  D25&N22 TDI
**/

 /** Example 2: For Driver SDK GPIO Demo**/

    #define PB3_FUNC  AS_GPIO //LED
    #define PB4_FUNC  AS_GPIO //LED
    #define PI2_FUNC  AS_GPIO //LED
    #define PI1_FUNC  AS_GPIO //LED

    #define PB0_FUNC  AS_GPIO //for IRQ_PIN  (input)
    #define PD2_FUNC  AS_GPIO
    #define PD3_FUNC  AS_GPIO// for signal generation (output)
    #define PD4_FUNC  AS_GPIO

    #define PULL_WAKEUP_SRC_PB3 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PB4 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PI2 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PI1 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PB0 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PD2 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PD3 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PD4 GPIO_PIN_UP_DOWN_FLOAT
 /*===============================================================================*/

//    #define LED1 GPIO_PA2
//    #define LED2 GPIO_PA1
//    #define LED3 GPIO_PA0
//    #define LED4 GPIO_PA4
    #define LED1 GPIO_PB3
    #define LED2 GPIO_PB4
    #define LED3 GPIO_PI2
    #define LED4 GPIO_PI1
    #define KEY1 GPIO_PB0
    #define KEY2 GPIO_PD2
    #define KEY3 GPIO_PD3
    #define KEY4 GPIO_PD4

#elif defined(MCU_CORE_TL721X)
    #define LED1 GPIO_PC3
    #define LED2 GPIO_PC2
    #define LED3 GPIO_PC1
    #define LED4 GPIO_PC0
    #define KEY1 GPIO_PB7 //In the hardware design of TL7218X_C1T315A20_V1_5 and earlier versions, the PD4 pin used for KEY1 does not support I/O functionality. As a result, it has been replaced with the fully functional PB7 pin after adjustment.
    #define KEY2 GPIO_PD6
    #define KEY3 GPIO_PD5
    #define KEY4 GPIO_PD7
#elif defined(MCU_CORE_TL321X)
    #define LED1 GPIO_PD0
    #define LED2 GPIO_PB0
    #define LED3 GPIO_PB1
    #define LED4 GPIO_PB2
    #define KEY1 GPIO_PD4
    #define KEY2 GPIO_PD5
    #define KEY3 GPIO_PD6
    #define KEY4 GPIO_PD7
#elif defined(MCU_CORE_W92)
    #define LED1 GPIO_PD0
    #define LED2 GPIO_PD1
    #define LED3 GPIO_PD2
    #define LED4 GPIO_PD3
    #define KEY1 GPIO_PD4
    #define KEY2 GPIO_PD5
    #define KEY3 GPIO_PD6
    #define KEY4 GPIO_PD7
#elif defined(MCU_CORE_TL323X)
    #define LED1 GPIO_PC0
    #define LED2 GPIO_PC1
    #define LED3 GPIO_PC2
    #define LED4 GPIO_PC3
    #define KEY1 GPIO_PC4
    #define KEY2 GPIO_PC5
    #define KEY3 GPIO_PC6
    #define KEY4 GPIO_PC7
#elif defined(MCU_CORE_TL322X)

    #define PULL_WAKEUP_SRC_PC4 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC5 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC6 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC7 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG3 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG2 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG1 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG0 GPIO_PIN_UP_DOWN_FLOAT

    #define LED1 GPIO_PC4
    #define LED2 GPIO_PC5
    #define LED3 GPIO_PC6
    #define LED4 GPIO_PC7

    #define KEY1 GPIO_PG3
    #define KEY2 GPIO_PG2
    #define KEY3 GPIO_PG1
    #define KEY4 GPIO_PG0
#elif defined(MCU_CORE_TL522X)

    #define PULL_WAKEUP_SRC_PC4 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC5 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC6 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC7 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG3 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG2 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG1 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PG0 GPIO_PIN_UP_DOWN_FLOAT

    #define LED1 GPIO_PD0
    #define LED2 GPIO_PD1
    #define LED3 GPIO_PD2
    #define LED4 GPIO_PD3

    #define KEY1 GPIO_PG3
    #define KEY2 GPIO_PG2
    #define KEY3 GPIO_PG1
    #define KEY4 GPIO_PG0

#elif defined(MCU_CORE_TL711X)
    #define LED1 GPIO_PD0
    #define LED2 GPIO_PD1
    #define LED3 GPIO_PD2
    #define LED4 GPIO_PD3

    #define KEY1 GPIO_PA3
    #define KEY2 GPIO_PE3
    #define KEY3 GPIO_PA0
    #define KEY4 GPIO_PF0
#elif defined(MCU_CORE_TL523X)
    #define LED1 GPIO_PB2
    #define LED2 GPIO_PB3
    #define LED3 GPIO_PB4
    #define LED4 GPIO_PB5

    #define KEY1 GPIO_PC0
    #define KEY2 GPIO_PC1
    #define KEY3 GPIO_PC2
    #define KEY4 GPIO_PC3
#elif defined(MCU_CORE_TL521X)
    #define LED1 GPIO_PA0
    #define LED2 GPIO_PA2
    #define LED3 GPIO_PA3
    #define LED4 GPIO_PA4
/*the key defined according to the hardware design, row0, row1,col0 col1*/
    #define KEY1 GPIO_PC2
    #define KEY2 GPIO_PC3
    #define KEY3 GPIO_PA1
    #define KEY4 GPIO_PB2
#elif defined(MCU_CORE_TL752X)
    #define LED1 GPIO_PA7
    #define LED2 GPIO_PA9
    #define LED3 GPIO_PB2
    #define LED4 GPIO_PB3

    #define KEY1 GPIO_PA6
    #define KEY2 GPIO_PA8
    #define KEY3 GPIO_PB1
    #define KEY4 GPIO_PB0

#elif defined(MCU_CORE_TL753X)
    #define LED1 GPIO_PC4
    #define LED2 GPIO_PC3
    #define LED3 GPIO_PC2
    #define LED4 GPIO_PC1

    #define KEY1 GPIO_PA0
    #define KEY2 GPIO_PF6
    #define KEY3 GPIO_PF7
    
    #define PULL_WAKEUP_SRC_PC4 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC3 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC2 GPIO_PIN_UP_DOWN_FLOAT
    #define PULL_WAKEUP_SRC_PC1 GPIO_PIN_UP_DOWN_FLOAT

#else
    #define LED1 GPIO_PD0
    #define LED2 GPIO_PD1
    #define LED3 GPIO_PD2
    #define LED4 GPIO_PD3
    #define LED5 GPIO_PD4
    #define LED6 GPIO_PD5

    #define KEY1 GPIO_PA3
    #define KEY2 GPIO_PE3
    #define KEY3 GPIO_PA0
    #define KEY4 GPIO_PF0
#endif
