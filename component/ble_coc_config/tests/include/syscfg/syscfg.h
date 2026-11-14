/* Native test configuration: no chip SDK, CLI or FreeRTOS dependency. */
#pragma once
#define MYNEWT_VAL_BSP_SIMULATED 0
#define MYNEWT_VAL_BLE_MESH 0
#define MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM 1
#define MYNEWT_VAL_BLE_L2CAP_COC_MPS 247
#define MYNEWT_VAL_BLE_L2CAP_COC_SDU_BUFF_COUNT 1
#ifndef MYNEWT_VAL_MSYS_1_BLOCK_COUNT
#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT 16
#endif
#include_next <syscfg/syscfg.h>
