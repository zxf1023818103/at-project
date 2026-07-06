/*
 * wifi_port.h — the platform WiFi HAL that the shared wifi_service
 * (component/wifi_service_config) drives. Each platform implements these over its
 * native driver (Bouffalo wifi4 wifi_mgmr_* / Realtek RTK wifi_*), so the connect
 * strategy + reconnect monitor + provisioning policy lives in ONE place and only
 * the driver mechanics differ per platform.
 *
 * All blocking calls run in the wifi_service task context (never an ISR).
 */
#ifndef WIFI_PORT_H
#define WIFI_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stack (in words) for the wifi_service task, which runs the platform's scan/connect: RTK's
 * wifi_scan/wifi_connect need ~16 KB, Bouffalo wifi4 far less. Each platform sizes its own. */
unsigned wifi_port_task_stack_words( void );

/* Bring up the STA interface (wifi_on / netif register / IP-stack init as the platform needs)
 * so a connect can follow. Runs once, in the wifi_service task, before the first connect. 0 = ok. */
int wifi_port_enable( void );

/* Disable the DRIVER's own autoreconnect, so wifi_service is the single reconnect
 * authority (otherwise the driver's retry races the shared monitor). */
void wifi_port_autoreconnect_disable( void );

/* Blocking directed scan for `ssid`. On success (returns 0) fills *chan (1..14 = 2.4 GHz,
 * >14 = 5 GHz) and *sec (opaque platform security id to feed back to wifi_port_connect /
 * cache as WIFI_SECURITY; set to -1 if the platform auto-detects). Non-zero = not found. */
int wifi_port_scan( const char * ssid, uint8_t * chan, int * sec );

/* Blocking associate to ssid/pass on `chan` (0 = let the driver choose) with `sec` from a
 * prior scan or the WIFI_SECURITY cache (-1 = unknown/auto-detect). 0 = associated. */
int wifi_port_connect( const char * ssid, const char * pass, uint8_t chan, int sec );

/* Drop the current association. */
void wifi_port_disconnect( void );

/* Serialize the opaque security id to/from the WIFI_SECURITY env string. A platform that
 * auto-detects security returns -1 / "" (the service then never caches WIFI_SECURITY). */
int          wifi_port_sec_from_str( const char * s );
const char * wifi_port_sec_to_str( int sec );

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PORT_H */
