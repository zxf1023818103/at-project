/********************************************************************************************************
 * @file    myudb.h
 *
 * @brief   This is the header file for Telink RISC-V MCU
 *
 * @author  Driver Group
 * @date    2019
 *
 * @par     Copyright (c) 2019, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#ifndef __MYUDB_H__
#define __MYUDB_H__
#pragma once

#include "log_def_stack.h"

/**
 * @brief 0: USB0, 1:USB1.
 * 
 * @note
 *        - This version only TL7518 support USB1.
 */
#if defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X) || defined(MCU_CORE_TL322X) || defined(MCU_CORE_TL752X) || defined(MCU_CORE_TL753X) || defined(MCU_CORE_TL711X) || defined(MCU_CORE_TL522X)
    #define USB_CNT 1
#else
    #define USB_CNT 0
#endif

#define SL_STACK_EN   1
#define SL_BASE_EN    1
#define SL_SNIFF_EN   0
#define SL_TIMEOUT_EN 0

#define my_dump_str_data(en, s, p, n)                  \
    if (en) {                                          \
        usb_send_str_data(s, (unsigned char *)(p), n); \
    }


typedef int (*func_myudb_hci_cmd_cb_t)(unsigned char *, int);

void myudb_register_hci_cb(void *p);

void myudb_usb_init(unsigned short id, void *p);

void myudb_usb_bulkout_ready(void);

void myudb_usb_handle_irq(void);

int myudb_usb_get_packet(unsigned char *p);

void usb_send_status_pkt(unsigned char status, unsigned char buffer_num, unsigned char *pkt, unsigned short len);

void myudb_capture_enable(int en);

void usb_send_str_data(char *str, unsigned char *ph, int n);

void myudb_set_txfifo_local(void);

void myudb_set_txfifo(void *p);

#define my_irq_disable() unsigned int rie = core_interrupt_disable()
#define my_irq_restore() core_restore_interrupt(rie)

#endif
