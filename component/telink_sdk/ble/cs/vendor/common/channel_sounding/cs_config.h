/********************************************************************************************************
 * @file    cs_config.h
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
#ifndef __CS_CONFIG_H__
#define __CS_CONFIG_H__

#include "vendor/common/user_config.h"

#ifndef CONFIG_CS_SUPPORT_INITIATOR
    #define CONFIG_CS_SUPPORT_INITIATOR   0 // Default not support initiator role
#endif

#ifndef CONFIG_CS_SUPPORT_REFLECTOR
    #define CONFIG_CS_SUPPORT_REFLECTOR   0 // Default not support reflector role
#endif

#ifndef CS_MAX_NUM
    #define CS_MAX_NUM    1
#endif

#ifndef APP_CS_CONFIG_NUM
    #define APP_CS_CONFIG_NUM       1
#endif

#ifndef UI_CONTROL_ENABLE
    #define UI_CONTROL_ENABLE   1 // Default not support UI control
#endif

#ifndef APP_CS_SUBEVENT_LOG_EN
    #define APP_CS_SUBEVENT_LOG_EN   0 // Default not support subevent log
#endif

#ifndef CS_PROCEDURE_CMD_TRIG
    #define CS_PROCEDURE_CMD_TRIG   0 // Default not support procedure command trigger
#endif

#ifndef MODE0_RSSI_DISTANCE
    #define MODE0_RSSI_DISTANCE   0 // Default not support mode0 rssi distance
#endif

#ifndef CS_ALGO_MAX_DISTANCE
    #define CS_ALGO_MAX_DISTANCE   120 // Default max distance
#endif

#ifndef CS_SUPPORT_PRIVATE_CONTROL_PDU
    #define CS_SUPPORT_PRIVATE_CONTROL_PDU   0 // Default not support private control pdu
#endif

#if (CS_SUPPORT_PRIVATE_CONTROL_PDU)
    #define LL_FEATURE_ENABLE_PRIVATE_CONTROL_PDU 1
#endif


#endif