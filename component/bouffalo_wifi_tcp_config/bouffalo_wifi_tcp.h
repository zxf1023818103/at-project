#pragma once
#include "FreeRTOS_Routing.h"
/* Wait for FHOST to register interfaces before IPInit_Multi. */
BaseType_t bouffalo_wifi_tcp_interfaces_ready(void);
void bouffalo_wifi_tcp_started(void);
void bouffalo_wifi_tcp_network_event(eIPCallbackEvent_t event, NetworkEndPoint_t *endpoint);
void bouffalo_wifi_tcp_status(void);
