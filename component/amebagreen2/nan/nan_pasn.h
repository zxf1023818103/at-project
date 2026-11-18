#ifndef GREEN2_NAN_PASN_H
#define GREEN2_NAN_PASN_H
#include <stddef.h>
int green2_pasn_init(void);
int green2_pasn_event(unsigned short id, const void *data, size_t len);
int green2_pasn_cache_update(const void *data, size_t len, unsigned char peer_instance);
int green2_pasn_peer_authorized(const unsigned char peer[6]);
void green2_pasn_timers(void);
void green2_pasn_stop(void);
#endif
