/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include <stdarg.h>
esp_err_t tl3238_bluedroid_hci_attach(void);
int tl3238_bd_printf(const char *format, ...) __attribute__((format(__printf__, 1, 2)));
int tl3238_bd_vprintf(const char *format, va_list args);
