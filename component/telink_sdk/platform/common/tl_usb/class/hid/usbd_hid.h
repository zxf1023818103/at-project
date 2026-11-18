/********************************************************************************************************
 * @file    usbd_hid.h
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
#ifndef __USBD_HID_H__
#define __USBD_HID_H__

#include "usb_hid.h"

unsigned char  usbd_hid_interface_request_handler(unsigned char bus, usb_control_request_t const *setup, unsigned char setup_stage);
unsigned char *usbd_hid_get_report_descriptor(unsigned char bus, unsigned char intf, unsigned short *len);
WEAK void      usbd_hid_set_report(unsigned char bus, unsigned char hid_itf, unsigned char report_id, unsigned char report_type, unsigned char *report, unsigned short report_len);

#endif
