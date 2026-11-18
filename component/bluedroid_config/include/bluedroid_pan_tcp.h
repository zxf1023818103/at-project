#pragma once
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "esp_pan_api.h"

/* Register one PAN interface and DHCP endpoint before FreeRTOS_IPInit_Multi().
 * PANU is one interface; other Ethernet/Wi-Fi interfaces can coexist. */
NetworkInterface_t *bluedroid_pan_tcp_register(const uint8_t mac[6]);
/* Forward every PAN callback here, from the BTC task. */
void bluedroid_pan_tcp_event(esp_pan_cb_event_t event, esp_pan_cb_param_t *param);
void bluedroid_pan_tcp_status(void);
