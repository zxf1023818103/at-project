/********************************************************************************************************
 * @file    dut_cmd.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2022
 *
 * @par     Copyright (c) 2022, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#ifndef _DUT_CMD_H_
#define _DUT_CMD_H_

/////////////////////////    DEFAULT_TEST_CASE     //////////////////////////////
#define DUTCMD_DEFAULT 0x0000

/////////////////////////    GPIO_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_GPIO_INPUT                 0x0100
#define DUTCMD_GPIO_OUTPUT_LOW            0x0101
#define DUTCMD_GPIO_OUTPUT_HIGH           0x0102
#define DUTCMD_GPIO_DIGITAL_PULL_UP       0x0103
#define DUTCMD_GPIO_ANALOG_PULL_UP_1M     0x0104
#define DUTCMD_GPIO_ANALOG_PULL_UP_10K    0x0105
#define DUTCMD_GPIO_ANALOG_PULL_DOWN_100K 0x0106
#define DUTCMD_GPIO_IRQ                   0x0107
#define DUTCMD_GPIO_IRQ_RISC0             0x0108
#define DUTCMD_GPIO_IRQ_RISC1             0x0109
#define DUTCMD_GPIO_IRQ_SEL               0x010A
#define DUTCMD_GPIO_IRQ_REPORT            0x010B
#define DUTCMD_GPIO_IRQ_SEL_REPORT        0x010C
#define DUTCMD_GPIO_IRQ0                  0x010D
#define DUTCMD_GPIO_IRQ1                  0x010E
#define DUTCMD_GPIO_IRQ2                  0x010F
#define DUTCMD_GPIO_IRQ3                  0x0110
#define DUTCMD_GPIO_IRQ4                  0x0111
#define DUTCMD_GPIO_IRQ5                  0x0112
#define DUTCMD_GPIO_IRQ6                  0x0113
#define DUTCMD_GPIO_IRQ7                  0x0114
#define DUTCMD_GPIO_DIGITAL_PULL_DOWN     0x0115

#define TL_TEST_RESP_OK                   1
#define TL_TEST_RESP_ERR                  0
#define RESP_TYPE_PARAM_ERR               0xff
/////////////////////////    ADC_DEMO_TEST_CASE   //////////////////////////////
#define  DUTCMD_ADC_BASE_MULTIPLE_START       0x0200
#define  DUTCMD_ADC_BASE_MULTIPLE_INIT        0x0201
#define  DUTCMD_ADC_BASE_DMA_COMPLETE_INQUIRY 0x0202
#define  DUTCMD_ADC_BASE_SET_PARAMETER    0x0203
#define  DUTCMD_ADC_BASE_GET_DATA         0x0204
#define  DUTCMD_ADC_BASE_VBAT             0x0205
#define  DUTCMD_ADC_BASE_SAMPLE           0x0206
#define  DUTCMD_ADC_BASE_SAMPLE_STAR                    0x0207
#define  DUTCMD_ADC_BASE_SAMPLE_INIT_TEST              0x0208
#define  DUTCMD_ADC_BASE_VBAT_STAR                     0x0209
#define  DUTCMD_ADC_MULTIPLE_M_COLLECT                 0x020A
#define  DUTCMD_ADC_MULTIPLE_ML_COLLECT                0x020B
#define  DUTCMD_ADC_MULTIPLE_MLR_COLLECT               0x020C
#define  DUTCMD_ADC_BASE_VBAT_SET_PARAMETER_COLLECT    0x0210
#define  DUTCMD_ADC_BASE_MULTIPLE_START_PARTIAL_PRESSURE 0x0211


/////////////////////////    AES_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_AES_TEST 0x0300

/////////////////////////   ALG_REG_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_ANALOG_BYTE_WRITE      0x0400
#define DUTCMD_ANALOG_HALFWORD_WRITE  0x0401
#define DUTCMD_ANALOG_WORD_WRITE      0x0402
#define DUTCMD_ANALOG_BUFF_WRITE      0x0403
#define DUTCMD_ANALOG_WORD_DMA_WRITE  0x0404
#define DUTCMD_ANALOG_BUFF_DMA_WRITE  0x0405
#define DUTCMD_ANALOG_ADDR_DATA_WRITE 0x0406

/////////////////////////   Audio_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_AUDIO_TEST 0x0500

/////////////////////////   RF_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_RF_TEST 0x0600

/////////////////////////   BT_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_BT_TEST 0x0700

/////////////////////////   Coremark_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_COREMARK_TEST 0x0800

/////////////////////////   Dhrystone_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_DHRYSTONE_TEST 0x0900

/////////////////////////   Flash_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_Flash_TEST 0x0a00

/////////////////////////   IIC_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_I2C_TEST 0x0b00

/////////////////////////   LPC_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_LPC_TEST 0x0c00

/////////////////////////   MDEC_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_MDEC_TEST 0x0d00

/////////////////////////   NPE_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_NPE_TEST 0x0e00

/////////////////////////   PKE_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_PKE_TEST 0x0f00

/////////////////////////   PM_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_PM_TEST 0x1000

/////////////////////////   PWM_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_PWM_CONTINUE 0x1100
#define DUTCMD_PWM_COUNT    0x1101
#define DUTCMD_PWM_IR       0x1102
#define DUTCMD_PWM_IR_FIFO  0x1103
#define DUTCMD_PWM_IR_DMA   0x1104
/////////////////////////   S7816_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_S7816_TEST 0x1200

/////////////////////////   STimer_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_STIMER_TEST 0x1300

/////////////////////////   Timer_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_TIMER_TEST 0x1400

/////////////////////////   TRNG_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_TRNG_TEST 0x1500

/////////////////////////   UART_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_UART_TEST 0x1600

/////////////////////////   USB_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_USB_TEST 0x1700

/////////////////////////   DHRYSTONE_DEMO_TEST_CASE   //////////////////////////////
#define DUTCMD_DHRYSTONE_DETERMINE_RUN_TEST 0x1800
#define DUTCMD_DHRYSTONE_SET_PARAMETER_TEST 0x1801

#endif /* _DUT_CMD_H_ */
