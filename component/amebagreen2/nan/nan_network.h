#ifndef GREEN2_NAN_NETWORK_H
#define GREEN2_NAN_NETWORK_H
#include <stddef.h>
#include <stdint.h>
void green2_nan_net_init(void);
void green2_nan_mac_set(const uint8_t mac[6]);
uint8_t *green2_nan_mac(void);
void green2_nan_iid(uint8_t iid[8]);
void green2_nan_net_up(const uint8_t own[6], const uint8_t peer[6], const uint8_t iid[8]);
void green2_nan_net_down(void);
void green2_nan_rx(const void *data, size_t len);
void green2_nan_rx_flush(void);
void green2_nan_server_start(void);
void green2_nan_net_status(char *out, size_t len);
#ifdef AT_NAN_AUTO_SLEEP
void green2_nan_net_autosleep_reset(void);
#endif
#endif
