#include "ameba_soc.h"
#include "ameba_ota.h"
#include "flash_api.h"
#include "ota_platform.h"
#include "board_flash_lock.h"
#include "FreeRTOS.h"
#include "task.h"

int ota_platform_validate_layout(uint32_t off, uint32_t len)
{
    TickType_t start=xTaskGetTickCount();
    while (!IPC_SEMTake(IPC_SEM_CRYPTO,0xfff)) {
        if (xTaskGetTickCount()-start>pdMS_TO_TICKS(2000)) return -1;
        vTaskDelay(1);
    }
#ifdef AT_ENABLE_TFM
    extern int dplus_tfm_validate_layout(uint32_t,uint32_t);
    int rc=dplus_tfm_validate_layout(off,len);
#else
    taskENTER_CRITICAL();
    extern int dplus_verify_flash_layout(uint32_t,uint32_t,int);
    int rc=dplus_verify_flash_layout(off,len,0);
    taskEXIT_CRITICAL();
#endif
    IPC_SEMFree(IPC_SEM_CRYPTO);
    return rc ? -1 : 0;
}

int ota_platform_active_slot(void)
{
#ifdef AT_ENABLE_TFM
    extern int dplus_tfm_active_slot(void);
    return dplus_tfm_active_slot();
#else
    return ota_get_cur_index(OTA_IMGID_APP);
#endif
}
static int relock;
int ota_platform_begin(void)
{
    relock=board_flash_is_fw_locked();
    return board_flash_protect(BOARD_FLASH_UNLOCK,NULL,NULL,NULL,NULL) ? 0 : -1;
}
void ota_platform_end(void)
{
    if (relock) { board_flash_lock_fw(); relock=0; }
}
int ota_platform_read(uint32_t off, void *buf, uint32_t len)
{
    flash_t flash;
    return flash_stream_read(&flash, off, len, buf) == 1 ? 0 : -1;
}
int ota_platform_write(uint32_t off, const void *buf, uint32_t len)
{
    flash_t flash;
    return flash_stream_write(&flash, off, len, (uint8_t *)buf) == 1 ? 0 : -1;
}
int ota_platform_erase(uint32_t off)
{
    flash_t flash;
    flash_erase_sector(&flash, off);
    return 0;
}
void ota_platform_reset(void) { System_Reset(); }
