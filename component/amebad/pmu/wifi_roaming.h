#ifndef _AMEBAD_WIFI_ROAMING_H_
#define _AMEBAD_WIFI_ROAMING_H_

/*
 * Tickless WiFi-roaming state owned by the PMC (power-management) blob
 * lib_pmc_hp.a — relocated here off the SDK example header
 * component/common/example/tickless_wifi_roaming/example_tickless_wifi_roaming.*
 * so these globals live with the lib that defines them:
 *   roaming_sema       the roaming semaphore (lib_pmc_hp.a, B)
 *   roaming_type_flag  1=tickless, 2=normal, 3=DecisionDisconnect (lib_pmc_hp.a,
 *                      common); lib_wlan.a references it (U) and resolves here.
 *
 * No consumer in the minimal driver-only image yet (tickless roaming not wired);
 * this is the declaration home for when it is.
 */
#include "FreeRTOS.h"
#include "semphr.h"

extern unsigned char    roaming_type_flag;   /* lib_pmc_hp.a */
extern xSemaphoreHandle roaming_sema;        /* lib_pmc_hp.a */

#endif /* _AMEBAD_WIFI_ROAMING_H_ */
