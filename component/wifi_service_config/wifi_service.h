/*
 * wifi_service.h — the shared, cross-platform WiFi provisioning + reconnect service.
 *
 * ONE copy of the connection policy for every platform: read WIFI_SSID / WIFI_PASSWORD
 * from the KV-backed env (no built-in default -> an unprovisioned device stays offline),
 * connect via the cached WIFI_CHANNEL for an instant re-associate else fall back to a
 * directed scan, cache the channel (and WIFI_SECURITY if the platform reports it), then
 * supervise the link and re-associate on drop. The platform driver's OWN autoreconnect is
 * disabled (wifi_port_autoreconnect_disable) so this is the single reconnect authority.
 *
 * Everything platform-specific (the actual scan/associate/security handling) is behind the
 * wifi_port HAL (wifi_port.h); each platform supplies its own wifi_port_* implementation.
 */
#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Events the platform WiFi glue feeds to the shared monitor (from a task, never an ISR). */
typedef enum
{
    WIFI_SVC_EV_CONNECTED = 0,   /* association came up */
    WIFI_SVC_EV_DISCONNECT,      /* association lost / connect attempt failed */
    WIFI_SVC_EV_GOTIP,           /* DHCP lease acquired (informational) */
} wifi_svc_event_t;

/* Start the service: spawns the single provisioning + reconnect-monitor task. Idempotent. */
void wifi_service_start( void );

/* Feed a driver WiFi event to the monitor. Call from the platform's WiFi event handler. */
void wifi_service_on_event( wifi_svc_event_t ev );

/* Console `wifi forget`: disconnect, stop reconnecting, erase WIFI_* from the KV store so the
 * next boot does not re-use the credential or the cached channel. */
void wifi_forget( void );

/* Re-enable the reconnect monitor after a `wifi forget` (e.g. fresh credentials were set). */
void wifi_reconnect_enable( void );

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SERVICE_H */
