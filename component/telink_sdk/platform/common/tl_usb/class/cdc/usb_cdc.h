/********************************************************************************************************
 * @file    usb_cdc.h
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
#ifndef __USB_CDC_H__
#define __USB_CDC_H__

#include "../../core/usbd_core.h"

typedef struct PACKED
{
    unsigned int  cdc_bit_rate;
    unsigned char cdc_stop_bits;
    unsigned char cdc_parity;
    unsigned char cdc_data_bits;
} usb_cdc_line_coding_t;

#define CDC_REQ_SET_LINE_CODING        (0x20U)
#define CDC_REQ_GET_LINE_CODING        (0x21U)
#define CDC_REQ_SET_CONTROL_LINE_STATE (0x22U)
#define CDC_REQ_SEND_BREAK             (0x23U)

#endif
