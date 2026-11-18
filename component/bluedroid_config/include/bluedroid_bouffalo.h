#pragma once
#include "esp_err.h"
#include <stdbool.h>
/* BT-only combo-path helper: call after RF initialization and before the
 * controller. Do not call when the official RF/coexistence manager owns routing. */
void bluedroid_bouffalo_use_combo_rf(void);
/* Query the pinned controller library after controller initialization. */
bool bluedroid_bouffalo_classic_available(void);
/* Call once, from a FreeRTOS task, after starting the configured dual-mode controller. */
esp_err_t bluedroid_bouffalo_hci_attach(void);
#if defined(CONFIG_PAN_GATT_BR) || defined(CONFIG_PAN_GATT_LE)
/* Standalone fixture diagnostics: measures host RX-to-controller submission,
 * excluding radio transmission and peer scheduling. No per-packet logging. */
void bluedroid_bouffalo_gatt_timing_reset(unsigned short rx_handle);
void bluedroid_bouffalo_gatt_timing_report(void);
/* BTU-only, single-link fixture control-response backpressure. */
void bluedroid_bouffalo_gatt_control_reset(void);
void bluedroid_bouffalo_gatt_control_flush(void);
void bluedroid_bouffalo_gatt_control_congestion(bool full);
bool bluedroid_bouffalo_gatt_control_pending(void);
#endif

#if defined(BL616)
#define BLUEDROID_BOARD_NAME "BL616"
#else
#define BLUEDROID_BOARD_NAME "BL618DG"
#endif
