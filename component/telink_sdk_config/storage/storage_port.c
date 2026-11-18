/* SPDX-License-Identifier: Apache-2.0 */
#include "storage_port.h"
#include "lfs_storage.h"
#include "ble_store_littlefs.h"
#include "driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "FreeRTOS_CLI.h"
#include "nimble/nimble_port.h"
#include <stdio.h>
#include <string.h>

/* Heap-owned: do not take more of the almost full statically assigned DRAM.
 * Both the filesystem and the bond transaction execute on the host task. */
static struct storage_state {
    TlLfs *lfs;
    BleStoreLittlefs bonds;
    SemaphoreHandle_t response;
    struct ble_npl_event request;
    volatile unsigned busy;
    unsigned operation;
    char reply[384];
} *state;

int tl3238_storage_ready(void) { return state && state->lfs->mounted && state->bonds.ready; }
int tl3238_storage_error(void)
{ return !state ? LFS_ERR_NOMEM : state->lfs->error ? state->lfs->error : state->bonds.error; }

static void flash_info(char *out, size_t n)
{
    /* Read-only diagnostics, including when LittleFS could not mount.
     * SFDP 5Ah uses the same 3 address bytes / 8 dummy clocks / single data
     * line transaction format as the SDK's security-register read command.
     * This does not issue WREN, WRSR, block lock/unlock or OTP commands. */
    uint8_t sfdp[128];
    taskENTER_CRITICAL();
    uint32_t mid=flash_read_mid();
    unsigned sr1=flash_read_status(FLASH_READ_STATUS_CMD_LOWBYTE);
    unsigned sr2=flash_read_status(FLASH_READ_STATUS_CMD_HIGHBYTE);
    unsigned cr=flash_read_status(FLASH_READ_CONFIGURE_CMD);
    DISABLE_BTB;
    flash_mspi_read_ram(0x5a0097a8u,0,sfdp,sizeof sfdp);
    ENABLE_BTB;
    taskEXIT_CRITICAL();
    int used=snprintf(out,n,"FLASH mid=0x%06lx sr1=0x%02x sr2=0x%02x cr=0x%02x wps=%u bp=0x%02x cmp=%u\r\nSFDP=",
        (unsigned long)mid,sr1,sr2,cr,(cr>>2)&1,(sr1>>2)&31,(sr2>>6)&1);
    for (unsigned i=0; i<sizeof sfdp && used>0 && (size_t)used+4<n; ++i)
        used+=snprintf(out+used,n-used,"%02x",sfdp[i]);
    if (used>0 && (size_t)used+3<n) snprintf(out+used,n-used,"\r\n");
}

static void request_cb(struct ble_npl_event *event)
{
    (void)event;
    int rc=0;
    if (state->operation==4) { flash_info(state->reply,sizeof state->reply); goto done; }
    if (!tl3238_storage_ready()) rc=tl3238_storage_error();
    else if (state->operation==1 || state->operation==2) {
        static const uint8_t expected[]="TL3238 LittleFS persistence probe v1";
        uint8_t actual[sizeof expected];
        if (state->operation==1)
            rc=ble_lfs_replace(&state->lfs->fs,"/probe.bin","/probe.tmp",expected,sizeof expected);
        if (!rc) rc=ble_lfs_read_exact(&state->lfs->fs,"/probe.bin",actual,sizeof actual);
        if (!rc && memcmp(actual,expected,sizeof actual)) rc=LFS_ERR_CORRUPT;
    } else if (state->operation==3) {
        /* Explicit local CLI action; NimBLE removes peers from its resolving
         * list as well as the disk store. It also terminates their links. */
        ble_addr_t peers[BLE_LFS_BONDS]; int count=0;
        rc=ble_store_util_bonded_peers(peers,&count,BLE_LFS_BONDS);
        for (int i=0; !rc && i<count; ++i) rc=ble_gap_unpair(&peers[i]);
        if (!rc) rc=ble_store_littlefs_clear();
    }
    unsigned our,peer,cccd;
    ble_store_littlefs_counts(&our,&peer,&cccd);
    lfs_ssize_t blocks=state->lfs->mounted ? lfs_fs_size(&state->lfs->fs) : -1;
    snprintf(state->reply,sizeof state->reply,
        "LFS ready=%u base=0x%05x size=%u blocks=%u used=%ld formatted=%u error=%d\r\n"
        "BONDS ready=%u our=%u peer=%u cccd=%u max=%u writes=%u error=%d\r\n"
        "STORAGE op=%u rc=%d\r\n",
        state->lfs->mounted,TL_LFS_BASE,TL_LFS_SIZE,TL_LFS_BLOCKS,(long)blocks,
        state->lfs->formatted,state->lfs->error,state->bonds.ready,our,peer,cccd,
        BLE_LFS_BONDS,state->bonds.writes,state->bonds.error,state->operation,rc);
done:
    xSemaphoreGive(state->response);
    taskENTER_CRITICAL(); state->busy=0; taskEXIT_CRITICAL();
}
static BaseType_t storage_command(char *out, size_t n, const char *command)
{
    BaseType_t length, extra;
    const char *arg=FreeRTOS_CLIGetParameter(command,1,&length);
    unsigned op=0;
    if (arg) {
        if (FreeRTOS_CLIGetParameter(command,2,&extra)) goto usage;
        if (length==4 && !strncmp(arg,"test",4)) op=1;
        else if (length==5 && !strncmp(arg,"check",5)) op=2;
        else if (length==11 && !strncmp(arg,"clear-bonds",11)) op=3;
        else if (length==5 && !strncmp(arg,"flash",5)) op=4;
        else goto usage;
    }
    taskENTER_CRITICAL();
    if (state->busy) { taskEXIT_CRITICAL(); snprintf(out,n,"Storage busy\r\n"); return pdFALSE; }
    state->busy=1; taskEXIT_CRITICAL();
    xSemaphoreTake(state->response,0); state->operation=op;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(),&state->request);
    if (xSemaphoreTake(state->response,pdMS_TO_TICKS(5000))==pdPASS) snprintf(out,n,"%s",state->reply);
    else snprintf(out,n,"Storage command timed out\r\n");
    return pdFALSE;
usage:
    snprintf(out,n,"Usage: storage [test|check|clear-bonds|flash]\r\n"); return pdFALSE;
}
int tl3238_storage_init(void)
{
    state=pvPortMalloc(sizeof *state);
    configASSERT(state);
    memset(state,0,sizeof *state);
    state->response=xSemaphoreCreateBinary();
    configASSERT(state->response);
    ble_npl_event_init(&state->request,request_cb,NULL);
    static const CLI_Command_Definition_t cmd={"storage",
        "storage [test|check|clear-bonds|flash]: storage and Flash diagnostics\r\n",storage_command,-1};
    configASSERT(FreeRTOS_CLIRegisterCommand(&cmd)==pdPASS);
    lfs_t *fs=vfs_lfs_mount();
    state->lfs=tl3238_lfs_state();
    configASSERT(state->lfs);
    return ble_store_littlefs_init(&state->bonds,fs);
}
