/********************************************************************************************************
 * @file    mlxxxxx_pin_map_config.h
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

#define GPIO_NONE          0xffff

#if defined(MCU_CORE_TL721X)

#define PIN1 GPIO_NONE
#define PIN2 GPIO_PF7
#define PIN3 GPIO_PF6
#define PIN4 GPIO_PF5
#define PIN5 GPIO_PF4
#define PIN6 GPIO_PF3
#define PIN7 GPIO_PF2
#define PIN8 GPIO_PF1
#define PIN9 GPIO_PF0
#define PIN10 GPIO_NONE
#define PIN11 GPIO_PE7
#define PIN12 GPIO_PE6
#define PIN13 GPIO_PE5
#define PIN14 GPIO_PE4
#define PIN15 GPIO_PE3
#define PIN16 GPIO_PE2  
#define PIN17 GPIO_PE1  
#define PIN18 GPIO_PE0  
#define PIN19 GPIO_PA0  
#define PIN20 GPIO_PA1  
#define PIN21 GPIO_PA2  
#define PIN22 GPIO_PA3  
#define PIN23 GPIO_NONE 
#define PIN24 GPIO_PA4
#define PIN25 GPIO_PA5
#define PIN26 GPIO_PA6
#define PIN27 GPIO_PA7
#define PIN28 GPIO_PB0
#define PIN29 GPIO_PB1
#define PIN30 GPIO_PB2
#define PIN31 GPIO_PB3
#define PIN32 GPIO_PB4
#define PIN33 GPIO_PB5
#define PIN34 GPIO_PB6
#define PIN35 GPIO_PB7
#define PIN36 GPIO_NONE
#define PIN37 GPIO_NONE
#define PIN38 GPIO_PC0
#define PIN39 GPIO_PC1
#define PIN40 GPIO_PC2
#define PIN41 GPIO_PC3
#define PIN42 GPIO_PC4
#define PIN43 GPIO_PC5
#define PIN44 GPIO_PC6
#define PIN45 GPIO_PC7
#define PIN46 GPIO_NONE
#define PIN47 GPIO_NONE
#define PIN48 GPIO_NONE
#define PIN49 GPIO_NONE
#define PIN50 GPIO_NONE
#define PIN51 GPIO_PD4
#define PIN52 GPIO_PD5
#define PIN53 GPIO_PD6
#define PIN54 GPIO_PD7
#define PIN55 GPIO_PD3
#define PIN56 GPIO_PD2
#define PIN57 GPIO_PD1
#define PIN58 GPIO_PD0
#define PIN59 GPIO_NONE

#elif defined(MCU_CORE_TL322X)

#define PIN1 GPIO_NONE
#define PIN2 GPIO_PG0
#define PIN3 GPIO_PF7
#define PIN4 GPIO_PF6
#define PIN5 GPIO_PF5
#define PIN6 GPIO_PF4
#define PIN7 GPIO_PF3
#define PIN8 GPIO_PF0
#define PIN9 GPIO_PD3
#define PIN10 GPIO_NONE
#define PIN11 GPIO_PD2
#define PIN12 GPIO_PD1
#define PIN13 GPIO_PD0
#define PIN14 GPIO_PG4
#define PIN15 GPIO_PG5
#define PIN16 GPIO_PG3
#define PIN17 GPIO_PH3
#define PIN18 GPIO_PH4
#define PIN19 GPIO_PH5
#define PIN20 GPIO_PH6
#define PIN21 GPIO_PH7
#define PIN22 GPIO_PD6
#define PIN23 GPIO_NONE
#define PIN24 GPIO_PD7
#define PIN25 GPIO_PA3
#define PIN26 GPIO_PA4
#define PIN27 GPIO_PA7
#define PIN28 GPIO_PB3
#define PIN29 GPIO_PB2
#define PIN30 GPIO_PB1
#define PIN31 GPIO_PB0
#define PIN32 GPIO_PA2
#define PIN33 GPIO_PB4
#define PIN34 GPIO_PB7
#define PIN35 GPIO_PC6
#define PIN36 GPIO_NONE
#define PIN37 GPIO_NONE
#define PIN38 GPIO_PC7
#define PIN39 GPIO_PD5
#define PIN40 GPIO_PD4
#define PIN41 GPIO_PA1
#define PIN42 GPIO_PE0
#define PIN43 GPIO_PE1
#define PIN44 GPIO_PE2
#define PIN45 GPIO_PE3
#define PIN46 GPIO_NONE
#define PIN47 GPIO_NONE
#define PIN48 GPIO_NONE
#define PIN49 GPIO_NONE
#define PIN50 GPIO_NONE
#define PIN51 GPIO_PE4
#define PIN52 GPIO_PE5
#define PIN53 GPIO_PE6
#define PIN54 GPIO_PA0
#define PIN55 GPIO_PC3
#define PIN56 GPIO_PC1
#define PIN57 GPIO_PC2
#define PIN58 GPIO_PC0
#define PIN59 GPIO_NONE

#elif defined(MCU_CORE_TL321X)

#define PIN1  GPIO_NONE
#define PIN2  GPIO_PD6
#define PIN3  GPIO_PA2
#define PIN4  GPIO_PC2
#define PIN5  GPIO_PA3
#define PIN6  GPIO_NONE
#define PIN7  GPIO_NONE
#define PIN8  GPIO_NONE
#define PIN9  GPIO_NONE
#define PIN10 GPIO_NONE
#define PIN11 GPIO_PA0
#define PIN12 GPIO_NONE
#define PIN13 GPIO_PE6
#define PIN14 GPIO_PE7
#define PIN15 GPIO_PE2
#define PIN16 GPIO_PE4
#define PIN17 GPIO_PE5
#define PIN18 GPIO_NONE
#define PIN19 GPIO_NONE
#define PIN20 GPIO_NONE
#define PIN21 GPIO_PB0
#define PIN22 GPIO_PA1
#define PIN23 GPIO_NONE
#define PIN24 GPIO_PD4
#define PIN25 GPIO_PA5
#define PIN26 GPIO_PA6
#define PIN27 GPIO_PA7
#define PIN28 GPIO_NONE
#define PIN29 GPIO_NONE
#define PIN30 GPIO_NONE
#define PIN31 GPIO_NONE
#define PIN32 GPIO_PD1
#define PIN33 GPIO_PB1
#define PIN34 GPIO_PB6
#define PIN35 GPIO_PB7
#define PIN36 GPIO_NONE
#define PIN37 GPIO_NONE
#define PIN38 GPIO_PA4
#define PIN39 GPIO_NONE
#define PIN40 GPIO_NONE
#define PIN41 GPIO_NONE
#define PIN42 GPIO_NONE
#define PIN43 GPIO_NONE
#define PIN44 GPIO_NONE
#define PIN45 GPIO_NONE
#define PIN46 GPIO_NONE
#define PIN47 GPIO_NONE
#define PIN48 GPIO_NONE
#define PIN49 GPIO_NONE
#define PIN50 GPIO_NONE
#define PIN51 GPIO_NONE
#define PIN52 GPIO_NONE
#define PIN53 GPIO_NONE
#define PIN54 GPIO_NONE
#define PIN55 GPIO_PB5
#define PIN56 GPIO_PC1
#define PIN57 GPIO_NONE
#define PIN58 GPIO_PC0
#define PIN59 GPIO_NONE

#endif



#define PIN_D1     PIN17
#define PIN_D2     PIN16
#define PIN_SW4    PIN15
#define PIN_SW5    PIN5 
#define PIN_SW6    PIN13

#define PIN_J4_RX  PIN4
#define PIN_J4_TX  PIN3

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
