/*
 * aws_mac.c - the SHARED aws_get_mac(): AWS Thing name / MQTT client id = the module's
 * Bluetooth MAC as 12 lowercase hex + NUL.
 *
 * One implementation for every platform over the at_netif_bt_mac() port accessor
 * (component/freertos-plus-tcp_config/at_netif.h: amebad = efuse 0x190, bouffalo = the
 * mfg/efuse MAC its BT controller loads as BD_ADDR). Replaces the two per-platform copies,
 * which had drifted: bl602's used the WiFi STA MAC while amebad's used the BT MAC - the same
 * value on bouffalo (one efuse field) but semantically different. The device identity is now
 * consistently the BT MAC everywhere: Thing name == hostname suffix == BLE advert name.
 *
 * Cached after the first successful read (the amebad efuse read goes through TF-M on secure
 * images - not something to repeat on every publish path).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "aws_iot.h"

extern void at_netif_bt_mac(uint8_t mac[6]);

int aws_get_mac(char *out, size_t out_len)
{
    static char cached[13];
    static int  have;

    if (out == NULL || out_len < 13) return -1;
    if (!have) {
        static const char hex[] = "0123456789abcdef";
        uint8_t mac[6] = {0};
        int i, nz = 0;
        at_netif_bt_mac(mac);
        for (i = 0; i < 6; i++) {
            nz |= mac[i];
            cached[i * 2]     = hex[(mac[i] >> 4) & 0xf];
            cached[i * 2 + 1] = hex[mac[i] & 0xf];
        }
        cached[12] = '\0';
        if (nz == 0) {          /* blank/unreadable MAC: report the failure, don't cache */
            return -2;
        }
        have = 1;
    }
    memcpy(out, cached, 13);
    return 0;
}
