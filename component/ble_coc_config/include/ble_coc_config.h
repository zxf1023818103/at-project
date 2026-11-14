/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Per-image overrides belong on the consuming host target, never in a chip
 * conditional here. The default profile fits the TL3238 CS/RAS/OTA image. */
#ifndef BLE_COC_PSM
#define BLE_COC_PSM 0x81
#endif
#ifndef BLE_COC_RX_MTU
#define BLE_COC_RX_MTU 2048
#endif
#ifndef BLE_COC_RX_CREDITS
#define BLE_COC_RX_CREDITS 24
#endif
#ifndef BLE_COC_RX_BUFFERS
#define BLE_COC_RX_BUFFERS 1
#endif
#ifndef BLE_COC_TX_SDU_MAX
#define BLE_COC_TX_SDU_MAX 245
#endif
#ifndef BLE_COC_TX_QUEUE_LIMIT
#define BLE_COC_TX_QUEUE_LIMIT 4
#endif
#ifndef BLE_COC_MSYS_RESERVE
#define BLE_COC_MSYS_RESERVE 6
#endif
#ifndef BLE_COC_TX_BURST
#define BLE_COC_TX_BURST 4
#endif
#ifndef BLE_COC_TX_INTERVAL_MS
#define BLE_COC_TX_INTERVAL_MS 2
#endif
#ifndef BLE_COC_IDLE_INTERVAL_MS
#define BLE_COC_IDLE_INTERVAL_MS 100
#endif
#ifndef BLE_COC_CONN_INTERVAL
#define BLE_COC_CONN_INTERVAL 12 /* 1.25 ms units */
#endif
#ifndef BLE_COC_HEAP_RESERVE
#define BLE_COC_HEAP_RESERVE 32768 /* Bluedroid: minimum before TX allocation */
#endif
#ifndef BLE_COC_DEVICE_NAME
#define BLE_COC_DEVICE_NAME "LE-CoC-BD" /* Bluedroid standalone advertiser */
#endif
#ifndef BLE_COC_BLUEDROID_STACK_WORDS
#define BLE_COC_BLUEDROID_STACK_WORDS 2048
#endif
#ifndef BLE_COC_BLUEDROID_PRIORITY
#define BLE_COC_BLUEDROID_PRIORITY 5
#endif
#ifndef BLE_COC_USE_STATIC_RANDOM
#define BLE_COC_USE_STATIC_RANDOM 0 /* Separate identity when switching Hosts. */
#endif

#if BLE_COC_PSM < 0x80 || BLE_COC_PSM > 0xff
#error "Flash Studio expects a dynamic LE PSM in 0x80..0xff"
#endif
#if BLE_COC_RX_MTU < 23 || BLE_COC_RX_MTU > 4096 || BLE_COC_RX_BUFFERS < 1
#error "Invalid CoC receive buffer configuration"
#endif
#if BLE_COC_TX_SDU_MAX < 23 || BLE_COC_TX_SDU_MAX > 4096
#error "Invalid CoC TX SDU size"
#endif
#if BLE_COC_RX_CREDITS < 1 || BLE_COC_RX_CREDITS > 65535 || BLE_COC_TX_QUEUE_LIMIT < 1
#error "Invalid CoC credits or queue limit"
#endif
#if BLE_COC_TX_BURST < 1 || BLE_COC_TX_INTERVAL_MS < 1 || BLE_COC_IDLE_INTERVAL_MS < 1
#error "Invalid CoC scheduler configuration"
#endif

/* Little-endian UUID bytes, shared with Flash Studio's discovery filters. */
#define BLE_COC_NIMBLE_UUID_BYTES \
    0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x00,0x10,0xf0,0xb1
#define BLE_COC_BLUEDROID_UUID_BYTES \
    0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x01,0x10,0xf0,0xb1
