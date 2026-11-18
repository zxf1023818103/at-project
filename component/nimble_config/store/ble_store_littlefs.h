/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "lfs.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"

#include "ble_bond_store.h"
#define BLE_LFS_BONDS BLE_BOND_MAX_BONDS
#define BLE_LFS_CCCDS BLE_BOND_MAX_CCCDS
#define BLE_LFS_BLOB_SIZE BLE_BOND_SIZE
#define BLE_LFS_PATH BLE_BOND_PATH
#define BLE_LFS_TEMP BLE_BOND_TEMP
typedef BleBondStore BleStoreLittlefs;
#ifndef BLE_STORE_GATT_DATABASE_ID
#define BLE_STORE_GATT_DATABASE_ID 1u
#endif

/* All calls are serialized by the NimBLE host lock, including a complete
 * temporary-file / sync / rename transaction. The caller owns this state. */
int ble_store_littlefs_init(BleStoreLittlefs *store, lfs_t *fs);
/* Call after GATT registration, from the Host sync callback. A bonded peer
 * may have cached the other Host's attribute table on the same identity. */
int ble_store_littlefs_on_sync(void);
/* Authentication callbacks must reject RAM-only bonds after a write error. */
int ble_store_littlefs_ready(void);
int ble_store_littlefs_clear(void);
void ble_store_littlefs_counts(unsigned *our, unsigned *peer, unsigned *cccd);

/* Compatibility names for existing board diagnostics. Shared implementation. */
#include "lfs_blob.h"
#define ble_lfs_read_exact lfs_blob_read_exact
#define ble_lfs_replace lfs_blob_replace
