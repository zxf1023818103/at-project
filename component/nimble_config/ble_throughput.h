/*
 * ble_throughput.h — cross-platform NimBLE BLE throughput test (bl602/bl616 + amebad).
 *
 * One shared code path, runtime role via the `bletp` console command:
 *   peripheral (default at boot): advertise the throughput service; when the central
 *     subscribes, blast max-payload notifications until it disconnects, then report Mbps.
 *   central (`bletp central [secs]`): scan for a peer advertising the throughput marker,
 *     connect, negotiate 2M PHY + DLE + MTU + a short conn interval, subscribe, count the
 *     received bytes for `secs`, report goodput Mbps, disconnect, revert to advertising.
 *
 * Data always flows peripheral -> central (notifications). Swapping which board runs
 * `bletp central` measures the other direction / GAP-role pairing with the SAME code.
 *
 * Status (both directions HW-verified, same firmware): amebad->bl602 ~0.39 Mbps goodput;
 * bl602->amebad ~0.06 Mbps (asymmetry = AmebaD's controller->host HCI-UART RX path +
 * OOM backpressure; see transport_amebad.c). The amebad RX-side crash under the flood was
 * the upstream hci_h4_sm_rx OOM assert + a desyncing RX loop — fixed in transport_amebad.c
 * (consumed-bytes retry loop) + NDEBUG on the amebad nimble lib. Run WiFi-dropped
 * (`wifi drop`) for a clean BLE measurement — the shared radio otherwise starves the
 * scan/connection (1M PHY; the 2M switch destabilised the coex-contended link).
 *
 * Wiring into the shared nimble_app.c:
 *   - nimble_start()      -> ble_tp_gatt_register()      (register the throughput GATT service)
 *   - nimble_advertise()  -> adds the 16-bit BLE_TP_ADV_UUID16 marker so a central can find us
 *   - nimble_gap_event()  -> ble_tp_peripheral_event(ev) (start/stop the TX blast on subscribe)
 *   - a platform's CLI init calls ble_tp_cli_register().
 */
#ifndef BLE_THROUGHPUT_H
#define BLE_THROUGHPUT_H

#include <stdint.h>

/* 16-bit UUID advertised by the peripheral (in the adv packet) as the throughput-test marker;
 * the central scans for it. Not a real 16-bit GATT service — just a discovery tag. */
#define BLE_TP_ADV_UUID16   0xFFF0

#ifdef __cplusplus
extern "C" {
#endif

struct ble_gap_event;   /* forward decl (avoids pulling NimBLE headers into non-BLE TUs) */

/* Register the throughput GATT service (one NOTIFY characteristic). Call from nimble_start()
 * after ble_svc_gatt_init(), before the host task starts. Returns 0 on success. */
int  ble_tp_gatt_register(void);

/* Forward the peripheral's GAP events (CONNECT / SUBSCRIBE / DISCONNECT / MTU) here so the
 * throughput module can start/stop the notification blast. Safe to call for every event. */
void ble_tp_peripheral_event(struct ble_gap_event *event);

/* Role entry points (also driven by the `bletp` command). */
void ble_tp_start_central(uint32_t secs);   /* stop adv, scan+connect+subscribe, measure RX */
void ble_tp_start_peripheral(void);          /* (re)advertise; blast on subscribe */

/* Register the `bletp` console command. Call from each platform's CLI init (NimBLE images). */
void ble_tp_cli_register(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_THROUGHPUT_H */
