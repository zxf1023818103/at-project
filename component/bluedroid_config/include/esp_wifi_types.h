#pragma once
/* Blufi's public header is included by BTC even with Blufi disabled. These
 * enum declarations are compile-only; this component exposes no Wi-Fi API. */
typedef enum { WIFI_MODE_NULL, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA } wifi_mode_t;
typedef enum { WIFI_AUTH_OPEN, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK,
               WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK } wifi_auth_mode_t;
