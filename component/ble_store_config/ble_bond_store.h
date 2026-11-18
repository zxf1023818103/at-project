/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "lfs_blob.h"
#include <stdint.h>

/* One wire format for all BLE Hosts. Addresses and integer fields are LE;
 * AES keys retain Bluetooth octet order. Never persist a Host C structure.
 * Capacity is a product setting, not a NimBLE/Bluedroid setting. A smaller
 * build may load a larger file only when all live records fit. */
#ifndef BLE_BOND_MAX_BONDS
#define BLE_BOND_MAX_BONDS 4
#endif
#ifndef BLE_BOND_MAX_CCCDS
#define BLE_BOND_MAX_CCCDS 16
#endif
#define BLE_BOND_PATH "/ble.bonds"
#define BLE_BOND_TEMP "/ble.tmp"
enum { BLE_BOND_HEADER=96, BLE_BOND_SEC=80, BLE_BOND_CCCD=16,
       BLE_BOND_OUR=1, BLE_BOND_PEER=2, BLE_BOND_SUB=3,
       BLE_BOND_LTK=1, BLE_BOND_IRK=2, BLE_BOND_CSRK=4,
       BLE_BOND_AUTH=8, BLE_BOND_SC=16, BLE_BOND_LID=32 };
#define BLE_BOND_SIZE (BLE_BOND_HEADER + 2u*BLE_BOND_MAX_BONDS*BLE_BOND_SEC + BLE_BOND_MAX_CCCDS*BLE_BOND_CCCD)
typedef struct {
    lfs_t *fs;
    unsigned ready, writes;
    int error;
    unsigned exists;
    uint8_t blob[BLE_BOND_SIZE];
} BleBondStore;

/* Caller serializes the complete transaction. Only one BLE Host owns this
 * store at a time. Failure latches until reinitialization; no RAM-only success. */
int ble_bond_init(BleBondStore *s, lfs_t *fs);
int ble_bond_save(BleBondStore *s);
int ble_bond_fail(BleBondStore *s, int error);
uint8_t *ble_bond_records(BleBondStore *s, int type, unsigned *size,
                         unsigned *capacity, unsigned *count_offset);
int ble_bond_find(BleBondStore *s, int type, const uint8_t address[7]);
int ble_bond_put(BleBondStore *s, int type, const uint8_t *record);
void ble_bond_remove(BleBondStore *s, int type, unsigned index);
uint32_t ble_bond_get32(const uint8_t *p);
void ble_bond_put32(uint8_t *p, uint32_t v);
/* Defaults use libc. Platforms whose libc heap is not initialized override
 * these in their existing LFS port, independently of the selected Host. */
void *ble_bond_alloc(size_t size);
void ble_bond_free(void *pointer);
