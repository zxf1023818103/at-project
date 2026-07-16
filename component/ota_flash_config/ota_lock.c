/*
 * ota_lock.c - the shared OTA mutual-exclusion lock (see ota_flash.h): BLE (mcumgr), AWS IoT
 * (ota_pal), and the ota_http console command all write the SAME inactive dual-bank slot; running
 * two at once corrupts the image. Pure FreeRTOS logic (critical section + tick math), identical on
 * every platform — compiled into each platform's ota_flash_port next to its slot writer.
 *
 * A BLE claim self-expires 5 s after the last refresh so an abandoned (phone-disconnected) upload
 * doesn't wedge the lock against a later AWS job. On release the port's
 * ota_flash_on_lock_released() hook closes any half-open write session (relock the fw region,
 * free decoder state): an OTA that aborted BEFORE ota_flash_finish() (connect fail, bad header,
 * HTTP parse error, ...) left the session open. Called outside the critical section — the SR
 * write must not run scheduler-suspended. Idempotent on the port side.
 */
#include "ota_flash.h"

#include "FreeRTOS.h"
#include "task.h"

static volatile ota_src_t  s_holder = OTA_SRC_NONE;
static volatile TickType_t s_ble_until;   /* BLE claim self-expiry (abandoned upload) */

/* Drop a stale BLE claim. Call inside a critical section. */
static void expire_stale( void )
{
    if( s_holder == OTA_SRC_BLE && ( int32_t ) ( s_ble_until - xTaskGetTickCount() ) <= 0 )
    {
        s_holder = OTA_SRC_NONE;
    }
}

int ota_lock_try( ota_src_t src )
{
    int rc;

    taskENTER_CRITICAL();
    expire_stale();
    if( s_holder == OTA_SRC_NONE || s_holder == src )
    {
        s_holder = src;
        if( src == OTA_SRC_BLE ) { s_ble_until = xTaskGetTickCount() + pdMS_TO_TICKS( 5000 ); }
        rc = 0;
    }
    else { rc = -1; }
    taskEXIT_CRITICAL();
    return rc;
}

void ota_lock_release( ota_src_t src )
{
    int was_holder = 0;

    taskENTER_CRITICAL();
    if( s_holder == src )
    {
        s_holder = OTA_SRC_NONE;
        was_holder = 1;
    }
    taskEXIT_CRITICAL();

    if( was_holder )
    {
        ota_flash_on_lock_released();
    }
}

ota_src_t ota_lock_holder( void )
{
    ota_src_t h;

    taskENTER_CRITICAL();
    expire_stale();
    h = s_holder;
    taskEXIT_CRITICAL();
    return h;
}
