/*
 * dot11d_bl602.c — the bl602/BL702 (wifi4) port of the 802.11d country auto-detect
 * (component/80211d_config, dot11d.h).
 *
 * scan : one broadcast wifi_mgmr scan (own task-notify waiter, synchronous), then walk the saved
 *        scan items via wifi_mgmr_scan_ap_all. The per-AP Country IE reaches the items through
 *        the at-project wifi4 patch (bl_rx.c IE-7 extract -> scan_item.country -> ap_item.country).
 * apply: wifi_mgmr_set_country_code — runtime channel-list update, no driver reboot (unlike
 *        AmebaD's channel-plan reboot).
 *
 * The wl80211 chips (bl616/cl) don't carry the Country IE in their scan records; this port is
 * bl602/BL702-only (the CMakeLists gates it) and main.cpp's call site sits in the BL602/BL702
 * region.
 */
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "wifi_mgmr_ext.h"   /* wifi_mgmr_scan_adv / wifi_mgmr_scan_ap_all / wifi_mgmr_set_country_code */
#include "dot11d.h"

static TaskHandle_t s_scan_waiter;

static void prv_scan_done_cb( void * data, void * param )
{
    ( void ) data;
    ( void ) param;
    if( s_scan_waiter != NULL )
    {
        xTaskNotifyGive( s_scan_waiter );
        s_scan_waiter = NULL;
    }
}

static void prv_tally_cb( wifi_mgmr_ap_item_t * env, uint32_t * param1, wifi_mgmr_ap_item_t * item )
{
    ( void ) env;
    dot11d_tally_add( ( dot11d_tally_t * ) param1, ( char ) item->country[ 0 ], ( char ) item->country[ 1 ] );
}

int dot11d_port_scan( dot11d_tally_t * t )
{
    /* Broadcast scan (all channels), synchronous: wait for the scan-done callback. */
    s_scan_waiter = xTaskGetCurrentTaskHandle();
    wifi_mgmr_scan_adv( NULL, prv_scan_done_cb, NULL, 0, NULL, NULL, 0, 0 );
    ( void ) ulTaskNotifyTake( pdTRUE, pdMS_TO_TICKS( 4000 ) );
    wifi_mgmr_scan_ap_all( NULL, ( uint32_t * ) t, prv_tally_cb );
    return 0;
}

void dot11d_port_apply( const char cc[ 3 ] )
{
    wifi_mgmr_set_country_code( ( char * ) cc );
}
