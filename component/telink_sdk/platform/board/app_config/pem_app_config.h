/********************************************************************************************************
 * @file    pem_app_config.h
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
#define NORMAL_MODE      1
#define TEST_MODE        2 // For internal testing, users need not care
#define DEMO_MODE        NORMAL_MODE

#define PEM_ID           PEM0

#define PEM_MSPI         0
#define PEM_LSPI         1
#define PEM_GSPI         2
#define PEM_GPIO         4
#define PEM_DMA          5
#define PEM_QDEC         6
#define PEM_CPU          7
#define PEM_TIMER        8
#define PEM_STIMER       9
#define PEM_SAR_ADC      10
#define PEM_AUDIO        11
#define PEM_IR_LEARN     12
#define PEM_PWM          13
#define PEM_PWM_1        14
#define PEM_RZ           15
#define PEM_ALGM         16
#define PEM_UART0        17
#define PEM_UART1        18
#define PEM_UART2        19
#define PEM_I2C          20
#define PEM_KS           21
#define PEM_USB          22
#define PEM_ZB           23
#define PEM_MISC         24
#define PEM_NONE         50

#define PEM_EVENT_MODE   PEM_STIMER
#define PEM_TASK_MODE    PEM_GPIO

#define TL_TEST_RESP_OK  1
#define TL_TEST_RESP_ERR 0

struct DUT_CMD_FUNC
{
    int cmd_name;
    void (*func1)(unsigned char, unsigned char);
    int (*func2)(unsigned char);
};

typedef struct
{
    unsigned char cmd   : 7; //bit0~bit6
    unsigned char state : 1; //bit7
} cmd_state_t;

typedef struct
{
    unsigned char resp[3];

    struct
    {
        unsigned char cmd   : 7; //bit0~bit6
        unsigned char state : 1; //bit7
    } cmd_state_t;

    unsigned long param[2];
    //unsigned char address[3];
} __attribute__((packed)) tl_test_cmd_pkt_t_another;

/********************************************************************************************************
 *                                 system time
 *******************************************************************************************************/
#define PULL_WAKEUP_SRC_PC5 GPIO_PIN_UP_DOWN_FLOAT

#define STIMER_CAPT_PIN     GPIO_PC5

extern void pem_task_stimer_init(unsigned char pem_id, unsigned char stimer_sel);
extern int  pem_task_stimer_mainloop(unsigned char stimer_sel);
extern void pem_event_stimer_init(unsigned char pem_id, unsigned char stimer_sel);
extern int  pem_event_stimer_start(unsigned char stimer_sel);

/********************************************************************************************************
 *                                 PWM
 *******************************************************************************************************/
#define PULL_WAKEUP_SRC_PB0 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PA1 GPIO_PIN_UP_DOWN_FLOAT
#define PULL_WAKEUP_SRC_PA2 GPIO_PIN_UP_DOWN_FLOAT

#define PWM0_PIN            GPIO_FC_PB0
#define PWM1_PIN            GPIO_FC_PA1
#define PWM2_PIN            GPIO_FC_PA2

#define PWM_PCLK_SPEED      10000000
enum
{
    CLOCK_PWM_CLOCK_1S  = PWM_PCLK_SPEED,
    CLOCK_PWM_CLOCK_1MS = (CLOCK_PWM_CLOCK_1S / 1000),
    CLOCK_PWM_CLOCK_1US = (CLOCK_PWM_CLOCK_1S / 1000000),
};

#include "gpio.h"
extern void pwm0_init(gpio_func_pin_e pin);
extern void pwm1_init(gpio_func_pin_e pin);
extern void pwm2_init(gpio_func_pin_e pin);
extern void pem_task_pwm_init(unsigned char pem_id, unsigned char pwm_sel);
extern int  pem_task_pwm_mainloop(unsigned char pwm_sel);
extern void pem_event_pwm_init(unsigned char pem_id, unsigned char pwm_sel);
extern int  pem_event_pwm_start(unsigned char pwm_sel);
#if defined(MCU_CORE_TL721X)||defined(MCU_CORE_TL321X)
extern void pem_task_pwm_1_init(unsigned char pem_id, unsigned char pwm_sel);
extern int  pem_task_pwm_1_mainloop(unsigned char pwm_sel);
extern void pem_event_pwm_1_init(unsigned char pem_id, unsigned char pwm_sel);
extern int  pem_event_pwm_1_start(unsigned char pwm_sel);
#endif

/********************************************************************************************************
 *                                 ADC
 *******************************************************************************************************/
extern void pem_task_adc_init(unsigned char pem_id, unsigned char adc_sel);
extern int  pem_task_adc_mainloop(unsigned char adc_sel);
extern void pem_event_adc_init(unsigned char pem_id, unsigned char adc_sel);
extern int  pem_event_adc_start(unsigned char adc_sel);

/********************************************************************************************************
 *                                 GPIO
 *******************************************************************************************************/
#if defined(MCU_CORE_TL721X)
    #define LED1_TOGGLE    PC3_TOGGLE
    #define LED2_TOGGLE    PC2_TOGGLE
    #define LED3_TOGGLE    PC1_TOGGLE
    #define LED4_TOGGLE    PC0_TOGGLE
#elif defined(MCU_CORE_TL321X)
    #define LED1_TOGGLE    PD0_TOGGLE
    #define LED2_TOGGLE    PB0_TOGGLE
    #define LED3_TOGGLE    PB1_TOGGLE
    #define LED4_TOGGLE    PB2_TOGGLE
#elif defined(MCU_CORE_TL323X)
    #define LED1_TOGGLE    PC0_TOGGLE
    #define LED2_TOGGLE    PC1_TOGGLE
    #define LED3_TOGGLE    PC2_TOGGLE
    #define LED4_TOGGLE    PC3_TOGGLE
#elif defined(MCU_CORE_TL322X)
    #define LED1_TOGGLE    PC4_TOGGLE
    #define LED2_TOGGLE    PC5_TOGGLE
    #define LED3_TOGGLE    PC6_TOGGLE
    #define LED4_TOGGLE    PC7_TOGGLE
#elif defined(MCU_CORE_TL521X)
    #define LED1_TOGGLE     PA0_TOGGLE
    #define LED2_TOGGLE     PA2_TOGGLE
    #define LED3_TOGGLE     PA3_TOGGLE
    #define LED4_TOGGLE     PA4_TOGGLE
#endif

extern void pem_task_gpio_init(unsigned char pem_id, unsigned int gpio_sel);
extern int  pem_task_gpio_mainloop(unsigned int gpio_sel);
extern void pem_event_gpio_init(unsigned char pem_id, unsigned int gpio_sel, pem_event_pol_e gpio_pol);
extern int  pem_event_gpio_start(unsigned int gpio_sel);

/********************************************************************************************************
 *                                 MISC
 *******************************************************************************************************/
typedef enum
{
    MISC_EVENT_QDEC_INT_POS = 0,
    MISC_EVENT_QDEC_WAKEUP,
    MISC_EVENT_LPC,
    MISC_EVENT_PM_IRQ,
} misc_event_e;

extern void pem_event_misc_init(unsigned char pem_id, unsigned int misc_sel);
extern int  pem_event_misc_start(unsigned int misc_sel);



#include "driver.h"
/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif
