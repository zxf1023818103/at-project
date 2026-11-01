#pragma once
#include "esp_err.h"
/* BT-only combo-path helper: call after RF initialization and before the
 * controller. Do not call when the official RF/coexistence manager owns routing. */
void bluedroid_bouffalo_use_combo_rf(void);
/* Call once, from a FreeRTOS task, after starting the B0 dual-mode controller. */
esp_err_t bluedroid_bouffalo_hci_attach(void);
