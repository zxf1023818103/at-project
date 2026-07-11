/*
 * nimble_platform_amebad.c — AmebaD strong overrides for the NimBLE platform hooks
 * declared __weak in component/nimble_config/nimble_app.c.
 *
 * These restore exactly what nimble_app.c used to call inline before it was made
 * cross-platform: HCI controller bring-up (bt_dtm_bringup), WiFi/BT coex, the HCI
 * RX task, the LittleFS-persistent bond store, and the WiFi-hostname device name.
 * Bouffalo has no such file — the __weak defaults (no-op / RAM store / fixed name)
 * already fit its on-chip, callback-driven controller.
 */
#include <stddef.h>

extern void bt_dtm_bringup(void);                    /* hci_dtm.c: controller bring-up */
extern void amebad_nimble_coex_init(void);           /* coex_amebad.c */
extern void amebad_nimble_coex_set_conn(int connected);
extern void nimble_amebad_transport_start_rx(void);  /* transport_amebad.c: HCI RX task */
extern void nimble_amebad_transport_stop_rx(void);
extern void ble_store_lfs_init(void);                /* ble_store_lfs.c: LittleFS bond store */

void nimble_platform_controller_bringup(void)  { bt_dtm_bringup(); }
void nimble_platform_coex_init(void)           { amebad_nimble_coex_init(); }
void nimble_platform_coex_set_conn(int conn)   { amebad_nimble_coex_set_conn(conn); }
void nimble_platform_transport_start_rx(void)  { nimble_amebad_transport_start_rx(); }
void nimble_platform_transport_stop_rx(void)   { nimble_amebad_transport_stop_rx(); }
void nimble_platform_store_init(void)          { ble_store_lfs_init(); }

/* nimble_platform_device_name is NOT overridden here: the shared weak default (nimble_app.c) already
 * returns the network hostname via pcApplicationHostnameHook(), so BLE == DHCP/mDNS name on every
 * platform. */
