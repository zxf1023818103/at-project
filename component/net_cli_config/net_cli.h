#ifndef NET_CLI_H
#define NET_CLI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register the cross-platform network-diagnostic console commands:
 *   resolve <host>  - DNS lookup (c-ares under CARES=ON, else the FreeRTOS-Plus-TCP resolver)
 *   ip6             - list the device's IPv6 addresses (link-local + SLAAC)
 * Shared out of AmebaD's app_km4/wifi_cli.c (iwpriv stays AmebaD-specific). Both platforms call this
 * from app_console_register_platform() in src/main.cpp. */
void net_cli_register(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_CLI_H */
