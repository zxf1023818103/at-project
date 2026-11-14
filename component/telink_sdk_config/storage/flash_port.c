/* SPDX-License-Identifier: Apache-2.0 */
#include "lfs_storage.h"
#include "lfs_blob.h"
#include "driver.h"
#include "FreeRTOS.h"
#include "ble_bond_store.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

void *ble_bond_alloc(size_t size) { return pvPortMalloc(size); }
void ble_bond_free(void *pointer) { vPortFree(pointer); }

/* A single mount shared by either Host and other filesystem consumers.
 * First mount is performed by the startup task before publishing the Host. */
static TlLfs *filesystem;
static SemaphoreHandle_t lock;
TlLfs *tl3238_lfs_state(void) { return filesystem; }
static int range(uint32_t a, size_t n)
{ return a >= TL_LFS_BASE && a <= TL_LFS_BASE+TL_LFS_SIZE && n <= TL_LFS_BASE+TL_LFS_SIZE-a; }
int tl_storage_read(uint32_t a, void *data, size_t n)
{
    if (!range(a,n)) return LFS_ERR_INVAL;
    taskENTER_CRITICAL(); flash_dread(a,n,data); taskEXIT_CRITICAL();
    return 0;
}
int tl_storage_prog(uint32_t a, const void *data, size_t n)
{
    if (!range(a,n) || !n || n > TL_FLASH_PAGE-(a%TL_FLASH_PAGE)) return LFS_ERR_INVAL;
    uint8_t verify[TL_FLASH_PAGE];
    taskENTER_CRITICAL();
    flash_page_program(a,n,(unsigned char *)data);
    flash_dread(a,n,verify);
    taskEXIT_CRITICAL();
    return memcmp(data,verify,n) ? LFS_ERR_IO : 0;
}
int tl_storage_erase(uint32_t a)
{
    if (!range(a,TL_FLASH_SECTOR) || (a%TL_FLASH_SECTOR)) return LFS_ERR_INVAL;
    taskENTER_CRITICAL(); flash_erase_sector(a); taskEXIT_CRITICAL();
    uint8_t verify[TL_FLASH_PAGE];
    for (unsigned off=0; off<TL_FLASH_SECTOR; off+=sizeof verify) {
        int rc=tl_storage_read(a+off,verify,sizeof verify);
        if (rc) return rc;
        for (unsigned i=0; i<sizeof verify; ++i) if (verify[i]!=0xff) return LFS_ERR_IO;
    }
    vTaskDelay(1);
    return 0;
}
int tl_storage_lock(void)
{ return xSemaphoreTake(lock,portMAX_DELAY)==pdPASS ? 0 : LFS_ERR_IO; }
int tl_storage_unlock(void)
{ return xSemaphoreGive(lock)==pdPASS ? 0 : LFS_ERR_IO; }
void tl_storage_yield(void) { vTaskDelay(1); }

lfs_t *vfs_lfs_mount(void)
{
    if (!filesystem) {
        filesystem=pvPortMalloc(sizeof *filesystem);
        if (!filesystem) return NULL;
        memset(filesystem,0,sizeof *filesystem);
        lock=xSemaphoreCreateMutex();
    }
    if (filesystem->mounted) return &filesystem->fs;
    if (!lock) { filesystem->error=LFS_ERR_NOMEM; return NULL; }
    if (filesystem->error) return NULL;
    uint32_t mid=flash_read_mid();
    if (((mid>>16)&255)!=20) { filesystem->error=LFS_ERR_INVAL; return NULL; }
    return tl_lfs_mount(filesystem) ? NULL : &filesystem->fs;
}
