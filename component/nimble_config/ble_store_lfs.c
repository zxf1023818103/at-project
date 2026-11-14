/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_store_littlefs.h"
#include <stdio.h>

/* Shared startup for all non-Telink hosts; Telink owns the same backend state
 * in its diagnostic port. There is no volatile fallback. */
static BleStoreLittlefs store;
int ble_store_lfs_init(void)
{
    int rc=ble_store_littlefs_init(&store,vfs_lfs_mount());
    unsigned our,peer,cccd;
    ble_store_littlefs_counts(&our,&peer,&cccd);
    printf("[BLEstore] LFS ready=%u our=%u peer=%u cccd=%u error=%d\r\n",
           store.ready,our,peer,cccd,store.error);
    return rc;
}
