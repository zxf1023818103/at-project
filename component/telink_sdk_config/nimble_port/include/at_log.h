/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
void tl3238_ble_log(const char *format, ...);
#define log_error(...) tl3238_ble_log(__VA_ARGS__)
#define log_info(...) tl3238_ble_log(__VA_ARGS__)
