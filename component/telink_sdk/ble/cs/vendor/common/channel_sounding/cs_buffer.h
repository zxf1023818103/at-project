/********************************************************************************************************
 * @file    cs_buffer.h
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
#ifndef __CS_BUFFER_H__
#define __CS_BUFFER_H__

#include "tl_common.h"
#include "cs_config.h"
#include "algorithm/hadm/tlk_algo4/include/libcs_tlk4.h"

/***************** CS rx Buffer allocation, Begin **************************************/
/**
 * @brief   CS RX Data buffer length
 */
#define CS_PHY_RX_FIFO_SIZE       CHANNEL_SOUNDING_RX_FIFO_SIZE_ALIGN16(MAX_ANT_PATHS_SUPPORT, 40, 10)

#if (CHIP_TYPE == CHIP_TYPE_TL323X)
//if use mode1,need add buf size ,use CHANNEL_SOUNDING_TRANSPORT_RX_FIFO_SIZE_ALIGN16(MAX_ANT_PATHS_SUPPORT, 40, 10)
    #define CS_TRANSPORT_RX_FIFO_SIZE CHANNEL_SOUNDING_TRANSPORT_RX_FIFO_SIZE_WITHOUT_MODE1_ALIGN16(MAX_ANT_PATHS_SUPPORT, 40, 10)
#else
    #define CS_TRANSPORT_RX_FIFO_SIZE CHANNEL_SOUNDING_TRANSPORT_RX_FIFO_SIZE_ALIGN16(MAX_ANT_PATHS_SUPPORT, 40, 10)
#endif
#define CS_PHY_RX_FIFO_NUM        1
#define CS_TRANSPORT_RX_FIFO_NUM  4

#define CS_APP_HOST_RAS_CLIENT_MALLOC_BUFF_SIZE ((RAS_PROCEDURE_COUNT * 2 + 1) * PROCEDURE_DATA_LEN + 4 * 1024)
#define CS_APP_HOST_RAS_SERVER_MALLOC_BUFF_SIZE ((RAS_PROCEDURE_COUNT + 1)     * PROCEDURE_DATA_LEN + 4 * 1024)
#define APP_HOST_MALLOC_BUFFER_SIZE    (4 * 1024)

extern u8 g_cs_phy_rx_buf[];
extern u8 g_cs_transport_rx_buf[];
extern u8 g_cs_app_config_param[];
/***************** CS Buffer allocation, End ****************************************/
#define CS_HCI_RX_FIFO_NUM 2
#define CS_PCT_DATA_SIZE  ((MAX_ANT_PATHS_SUPPORT + 1) * STEP_NUM_PER_SUBEVENT * (PHASE_ITEM_LEN + AMP_ITEM_LEN))

extern u8 g_cs_hci_subevent_rx_fifo[];
extern u8 g_cs_pct_raw_data[];

extern u8 g_cs_app_host_malloc_buffer[];
extern u8 app_cs_stepDRBGInfoBuffer[];

extern distance_tag_slot_t cs_tag_buffer[];

#endif
