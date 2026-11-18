/********************************************************************************************************
 * @file    cs_common.h
 *
 * @brief   This is the source file for BLE SDK
 *
 * @author  BLE GROUP
 * @date    02,2026
 *
 * @par     Copyright (c) 2026, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
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
#ifndef __CS_COMMON_H__
#define __CS_COMMON_H__

#include "cs_config.h"

#if (CONFIG_CS_SUPPORT_REFLECTOR == 1 || CONFIG_CS_SUPPORT_INITIATOR == 1)
#include "types.h"
#include "cs_buffer.h"

/*
 * Ranging method capability, compile-time and independent of each other:
 *
 * APP_IPT_SUPPORT_EN: enable Inline-PCT ranging, which only uses the initiator's
 *                     local PCT/IQ to calculate distance, that means:
 *                     1) we don't need RAS. 2) we don't need transfer ranging data anymore.
 *                     3) when initator's sub-event reported, calculate distance.
 * APP_RAS_SUPPORT_EN: enable traditional RAS based ranging.
 *
 * They can be enabled alone or together. When both are enabled, the method used
 * at runtime is selected per connection: Inline-PCT is preferred if the peer
 * supports it, otherwise RAS is used.
 *
 * [NOTE!!!]Inline-PCT only support in TL322X / TL323X / TL521X.
 */
#ifndef APP_IPT_SUPPORT_EN
#define APP_IPT_SUPPORT_EN     0
#endif

#ifndef APP_RAS_SUPPORT_EN
#define APP_RAS_SUPPORT_EN     1
#endif

#if (!APP_IPT_SUPPORT_EN && !APP_RAS_SUPPORT_EN)
    #error "At least one of APP_IPT_SUPPORT_EN and APP_RAS_SUPPORT_EN must be enabled"
#endif

#if (APP_IPT_SUPPORT_EN && (MCU_CORE_TYPE == MCU_CORE_TL721X || MCU_CORE_TYPE == MCU_CORE_B92))
    #error "Inline-PCT is not supported in TL721X and B92"
#endif

typedef struct
{
    u8 subevt_code;
    int (*event_handler)(u8 *p, int n);
    char *log_message;
} cs_event_handler_t;

typedef enum
{
    INITIATOR_ROLE = 0,
    REFLECTOR_ROLE = 1,
} cs_app_ranging_role_e;

typedef struct __attribute__((packed))
{
    u16 conn_handle;
    u8  config_id;
    u8  acl_role;
    u8  config_created;
    u8  exch_start_state;
    u8  exch_cmplt_state;
    u8  cs_enable_ipt;
    u32 exchange_tick;
} cs_control_t;

typedef struct __attribute__((packed))
{
    cs_control_t cs_ctrl[CS_MAX_NUM];
    u8  last_create_cfg_idx;
} cs_app_control_t;

typedef struct __attribute__((packed))
{
    u16 conn_handle;
    u8  config_id;
    u8  main_mode;
    u8  sub_mode;
    u8  role;
    u8  rtt_type;
    u8  valid;
} cs_app_config_t;

void blc_cs_common_init(void);

void blc_cs_app_ranging_calc_init(void);

void cs_deepretn_init(void);

void cs_app_multi_ant_switch_init(void);

void cs_app_reflactor_init(void);
void cs_app_reflactor_handle_cs_event(u8 event, u8 *p, int n);

void cs_app_initiator_init(void);
void cs_app_initiator_event_handle(u8 event, u8 *p, int n);

void cs_app_process_ctrl_loop(void);
cs_app_config_t *cs_app_get_cs_config(u16 conn_handle, u8 config_id);

int cs_app_ras_client_handle(u8 event, u16 conn_handle, u8 *data, u16 len);

int start_cs_timer_callback(void);


int blc_cs_ipt_start_cs(u16 conn_handle);

#endif // (CONFIG_CS_SUPPORT_REFLECTOR == 1 || CONFIG_CS_SUPPORT_INITIATOR == 1)
#endif // __CS_COMMON_H__
