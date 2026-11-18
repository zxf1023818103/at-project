/* SPDX-License-Identifier: Apache-2.0 */
#include "ota_storage.h"
#include "mcumgr_compat.h"
#include "driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "img_mgmt/img_mgmt.h"
#include <stdio.h>
#include <string.h>

static uint32_t flash_mid;
/* SWS diagnostics survive a blocked flash call: address, length, phase, count.
 * Phase tens: read=1, program=2, erase=3; units: 1 before critical section,
 * 2 inside primitive, 3 primitive returned, 4 critical section released. */
volatile uint32_t tl3238_ota_io_trace[4];
/* Flash access is synchronous on the NimBLE host task. Explicitly mask global
 * interrupts around the RAM-resident primitives; RTOS ISR callees use XIP.
 * Do not use flash_read_page / flash_write_page: on connection the CS SDK
 * redirects those pointers to a queue that requires interrupts to progress. */
int tl_ota_flash_read(uint32_t address, void *data, size_t length)
{
    tl3238_ota_io_trace[0]=address; tl3238_ota_io_trace[1]=length;
    tl3238_ota_io_trace[2]=11; taskENTER_CRITICAL();
    tl3238_ota_io_trace[2]=12; flash_dread(address, length, data);
    tl3238_ota_io_trace[2]=13; taskEXIT_CRITICAL();
    tl3238_ota_io_trace[2]=14; tl3238_ota_io_trace[3]++; return 0;
}
int tl_ota_flash_write(uint32_t address, const void *data, size_t length)
{
    tl3238_ota_io_trace[0]=address; tl3238_ota_io_trace[1]=length;
    tl3238_ota_io_trace[2]=21; taskENTER_CRITICAL();
    tl3238_ota_io_trace[2]=22; flash_page_program(address, length, (unsigned char *)data);
    tl3238_ota_io_trace[2]=23; taskEXIT_CRITICAL();
    tl3238_ota_io_trace[2]=24; tl3238_ota_io_trace[3]++; return 0;
}
int tl_ota_flash_erase(uint32_t address)
{
    tl3238_ota_io_trace[0]=address; tl3238_ota_io_trace[1]=4096;
    tl3238_ota_io_trace[2]=31; taskENTER_CRITICAL();
    tl3238_ota_io_trace[2]=32; flash_erase_sector(address);
    tl3238_ota_io_trace[2]=33; taskEXIT_CRITICAL();
    tl3238_ota_io_trace[2]=34; tl3238_ota_io_trace[3]++; vTaskDelay(1); return 0;
}
void tl_ota_flash_yield(void) { vTaskDelay(1); }

static BaseType_t ota_command(char *out, size_t length, const char *command)
{
    BaseType_t n;
    if (FreeRTOS_CLIGetParameter(command, 1, &n)) {
        snprintf(out, length, "Usage: ota\r\n");
        return pdFALSE;
    }
    snprintf(out, length,
        "OTA ready=%u active=0x%05lx next=0x%05lx max=%u build=%lu size=%lu\r\n"
        "flash_mid=0x%06lx uploading=%u offset=%lu pending=%u permanent_only=1 plic_preempt=%lu\r\n",
        tl_ota.ready, (unsigned long)tl_ota.active, (unsigned long)(tl_ota.active^TL_OTA_BANK_SIZE),
        TL_OTA_IMAGE_MAX, (unsigned long)tl_ota.running.build, (unsigned long)tl_ota.running.size,
        (unsigned long)flash_mid, tl_ota.uploading, (unsigned long)tl_ota.offset, tl_ota.pending,
        g_plic_preempt_en);
    return pdFALSE;
}
static const CLI_Command_Definition_t ota_cli={"ota", "ota: native A/B image status\r\n", ota_command, -1};
void tl3238_ota_init(void)
{
    flash_mid=flash_read_mid();
    unsigned capacity=(flash_mid>>16)&255;
    /* Same zero/nonzero XIP-offset selection as SDK ext_pm.c. This project
     * supports only ROM boot banks 0 and 0x80000; compare physical header with
     * the currently mapped image before permitting any writes. */
    uint32_t active=reg_mspi_xip_core_offset ? TL_OTA_BANK_SIZE : 0;
    uint8_t header[TL_OTA_HEADER_SIZE];
    tl_ota_flash_read(active, header, sizeof header);
    if (capacity>=20 && capacity<=24 &&
        !memcmp(header, (const void *)0x20000000, sizeof header))
        (void)tl_ota_init(active, 1u<<capacity);
    img_mgmt_register_group();
    FreeRTOS_CLIRegisterCommand(&ota_cli);
}
