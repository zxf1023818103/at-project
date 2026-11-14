/********************************************************************************************************
 * @file    debug_vcd.h
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

#ifndef DEBUG_VCD_H_
#define DEBUG_VCD_H_

#include "log_def_stack.h"
#include "myudb.h"

#ifndef clock_time
#define clock_time stimer_get_tick
#endif

#define log_sync(en)      \
    if (en) {             \
        my_irq_disable(); \
        log_uart(0);      \
        log_uart(0);      \
        log_uart(0);      \
        log_uart(0);      \
        my_irq_restore(); \
    }

#ifndef VCD_EN
    #define VCD_EN 1
#endif

#if defined(MCU_CORE_TL7518) || defined(MCU_CORE_TL751X)

    #define USB_CNT 1
    #if (USB_CNT == 0)
        #define log_uart(d) reg_usb_ep8_dat = d
        /*
 * log_xxx_general: can used in MainLoop or IRQ
 * log_xxx_irq:     can be only used in IRQ, not execute IRQ protect(disable & restore) to save running timing
 */


        /* log_tick 4-byte (001_id-5bits) id0: timestamp align with hardware gpio output; id1-31: user define */
        #define log_tick_general(en, id)    \
            if (VCD_EN && en) {             \
                my_irq_disable();           \
                log_uart(0x20 | (id & 31)); \
                int t = clock_time();       \
                log_uart(t);                \
                log_uart(t >> 8);           \
                log_uart(t >> 16);          \
                my_irq_restore();           \
            }

        #define log_tick_irq(en, id)   \
            if (VCD_EN && en) {        \
                log_uart(0x20 | (id)); \
                int t = clock_time();  \
                log_uart(t);           \
                log_uart(t >> 8);      \
                log_uart(t >> 16);     \
            }


        /* log_event: 1-byte (000_id-5bits) */
        #define log_event_general(en, id)   \
            if (VCD_EN && en) {             \
                my_irq_disable();           \
                log_uart(0x00 | (id & 31)); \
                my_irq_restore();           \
            }

        #define log_event_irq(en, id)  \
            if (VCD_EN && en) {        \
                log_uart(0x00 | (id)); \
            }


        /* log_task: 1-byte (01x_id-5bits) 1-bit data: id0 & id1 reserved for hardware */
        #define log_task_general(en, id, b)                \
            if (VCD_EN && en) {                            \
                my_irq_disable();                          \
                log_uart(((b) ? 0x60 : 0x40) | (id & 31)); \
                int t = clock_time();                      \
                log_uart(t);                               \
                log_uart(t >> 8);                          \
                log_uart(t >> 16);                         \
                my_irq_restore();                          \
            }

        //#define log_task_irq(en,id,b)      if(VCD_EN && en) {log_uart(((b)?0x60:0x40)|(id));int t=clock_time();log_uart(t);log_uart(t>>8);log_uart(t>>16);}
        #define log_task_begin_irq(en, id) \
            if (VCD_EN && en) {            \
                log_uart(0x60 | (id));     \
                int t = clock_time();      \
                log_uart(t);               \
                log_uart(t >> 8);          \
                log_uart(t >> 16);         \
            }
        #define log_task_end_irq(en, id) \
            if (VCD_EN && en) {          \
                log_uart(0x40 | (id));   \
                int t = clock_time();    \
                log_uart(t);             \
                log_uart(t >> 8);        \
                log_uart(t >> 16);       \
            }


        /* log_b8:  2-byte (10-id-6bits) 8-bit data */
        #define log_b8_general(en, id, d)   \
            if (VCD_EN && en) {             \
                my_irq_disable();           \
                log_uart(0x80 | (id & 63)); \
                log_uart(d);                \
                my_irq_restore();           \
            }

        #define log_b8_irq(en, id, d)  \
            if (VCD_EN && en) {        \
                log_uart(0x80 | (id)); \
                log_uart(d);           \
            }


        /* log_b16:  3-byte (11-id-6bits) 16-bit data */
        #define log_b16_general(en, id, d)  \
            if (VCD_EN && en) {             \
                my_irq_disable();           \
                log_uart(0xc0 | (id & 63)); \
                log_uart(d);                \
                log_uart((d) >> 8);         \
                my_irq_restore();           \
            }
        #define log_b16_byte_general(en, id, high_b, low_b) \
            if (VCD_EN && en) {                             \
                my_irq_disable();                           \
                log_uart(0xc0 | (id));                      \
                log_uart(low_b);                            \
                log_uart(high_b);                           \
                my_irq_restore();                           \
            }


        #define log_b16_irq(en, id, d) \
            if (VCD_EN && en) {        \
                log_uart(0xc0 | (id)); \
                log_uart(d);           \
                log_uart((d) >> 8);    \
            }
        #define log_b16_byte_irq(en, id, high_b, low_b) \
            if (VCD_EN && en) {                         \
                log_uart(0xc0 | (id));                  \
                log_uart(low_b);                        \
                log_uart(high_b);                       \
            }


    #elif (USB_CNT == 1)
        /* only onca support USB1 PHY, and its VCD tool will have different parsing.*/
        #define log_uart(d) reg_usb1_ep8_dat = d

        /*
 * log_xxx_general: can used in MainLoop or IRQ
 * log_xxx_irq:     can be only used in IRQ, not execute IRQ protect(disable & restore) to save running timing
 */
        #define log_tick_general(en, id)                   \
            if (VCD_EN && en) {                            \
                my_irq_disable();                          \
                int t = clock_time();                      \
                log_uart(3 | (id << 24) | (t & 0xfffff8)); \
                my_irq_restore();                          \
            }

        #define log_tick_irq(en, id)                       \
            if (VCD_EN && en) {                            \
                int t = clock_time();                      \
                log_uart(3 | (id << 24) | (t & 0xfffff8)); \
            }


        /* unsuport log event in onca */
        #define log_event_general(en, id)
        #define log_event_irq(en, id)

        /* unsuport log b8 in onca */
        #define log_b8_general(en, id, d)
        #define log_b8_irq(en, id, d)

        /* log_task: 1-byte (01x_id-5bits) 1-bit data: id0 & id1 reserved for hardware */
        #define log_task_general(en, id, level)                          \
            if (VCD_EN && en) {                                          \
                my_irq_disable();                                        \
                int t = clock_time();                                    \
                log_uart((level ? 7 : 5) | (id << 24) | (t & 0xfffff8)); \
                my_irq_restore();                                        \
            }

        #define log_task_begin_irq(en, id)                 \
            if (VCD_EN && en) {                            \
                int t = clock_time();                      \
                log_uart(7 | (id << 24) | (t & 0xfffff8)); \
            }
        #define log_task_end_irq(en, id)                   \
            if (VCD_EN && en) {                            \
                int t = clock_time();                      \
                log_uart(5 | (id << 24) | (t & 0xfffff8)); \
            }

        /* log_b16:  2-byte (11-id-6bits) 16-bit data */
        #define log_b16_general(en, id, d)                      \
            if (VCD_EN && en) {                                 \
                my_irq_disable();                               \
                log_uart(1 | (id << 24) | ((d & 0xffff) << 8)); \
                my_irq_restore();                               \
            }

        #define log_b16_irq(en, id, d)                                                              \
            if (VCD_EN && en) {                                                                     \
                log_uart((1) | (((id) << 24) & 0xFFFFFFFF) | ((((d) & 0xffff) << 8) & 0xFFFFFFFF)); \
            }
        #define log_b16_byte_general(en, id, high_b, low_b)
        #define log_b16_byte_irq(en, id, high_b, low_b)

        /* log_b16: 24-bit data */
        #define log_b24_general(en, id, d)              \
            if (VCD_EN && en) {                         \
                my_irq_disable();                       \
                log_uart(((id & 127) << 1) | (d << 8)); \
                my_irq_restore();                       \
            }
        #define log_b24_irq(en, id, d)                  \
            if (VCD_EN && en) {                         \
                log_uart(((id & 127) << 1) | (d << 8)); \
            }
    #endif /* USB_CNT */
#else

    #if (USB_CNT == 0)
        #define log_uart(d) reg_usb_ep8_dat = d
    #elif (USB_CNT == 1)
        #define log_uart(d) reg_usb1_ep8_dat = d
    #endif

    #define LOG_EVENT_TIMESTAMP 0
    #define LOG_DATA_B1_0       0
    #define LOG_DATA_B1_1       1

    //#define           get_systemtick()        (stimer_get_tick()*3/2)
    #define get_systemtick()    stimer_get_tick()

    //#define           log_uart(d)             uart_send_byte_dma(0,d)

    #define DEBUG_PORT          GPIO_PB2
    #define log_ref_gpio_h()    gpio_set_high_level(DEBUG_PORT)
    #define log_ref_gpio_l()    gpio_set_low_level(DEBUG_PORT)


    #define log_hw_ref()              \
        {                             \
            my_irq_disable();         \
            log_ref_gpio_h();         \
            log_uart(0x20);           \
            int t = get_systemtick(); \
            log_uart(t);              \
            log_uart(t >> 8);         \
            log_uart(t >> 16);        \
            log_ref_gpio_l();         \
            my_irq_restore();         \
        }

    // 4-byte sync word: 00 00 00 00
    #define log_sync(en)      \
        if (en) {             \
            my_irq_disable(); \
            log_uart(0);      \
            log_uart(0);      \
            log_uart(0);      \
            log_uart(0);      \
            my_irq_restore(); \
        }
    //4-byte (001_id-5bits) id0: timestamp align with hardware gpio output; id1-31: user define
    #define log_tick(en, id)            \
        if (en) {                       \
            my_irq_disable();           \
            log_uart(0x20 | (id & 31)); \
            int t = get_systemtick();   \
            log_uart(t);                \
            log_uart(t >> 8);           \
            log_uart(t >> 16);          \
            my_irq_restore();           \
        }

    //1-byte (000_id-5bits)
    #define log_event(en, id)           \
        if (en) {                       \
            my_irq_disable();           \
            log_uart(0x00 | (id & 31)); \
            my_irq_restore();           \
        }

    //1-byte (01x_id-5bits) 1-bit data: id0 & id1 reserved for hardware
    #define log_task(en, id, b)                        \
        if (en) {                                      \
            my_irq_disable();                          \
            log_uart(((b) ? 0x60 : 0x40) | (id & 31)); \
            int t = get_systemtick();                  \
            log_uart(t);                               \
            log_uart(t >> 8);                          \
            log_uart(t >> 16);                         \
            my_irq_restore();                          \
        }

    //2-byte (10-id-6bits) 8-bit data
    #define log_b8(en, id, d)           \
        if (en) {                       \
            my_irq_disable();           \
            log_uart(0x80 | (id & 63)); \
            log_uart(d);                \
            my_irq_restore();           \
        }

    //3-byte (11-id-6bits) 16-bit data
    #define log_b16(en, id, d)          \
        if (en) {                       \
            my_irq_disable();           \
            log_uart(0xc0 | (id & 63)); \
            log_uart(d);                \
            log_uart((d) >> 8);         \
            my_irq_restore();           \
        }

#endif //defined(MCU_CORE_TL7518)||defined(MCU_CORE_TL751X)


#endif /* TLKSTK_DEBUG_DEBUG_VCD_H_ */
