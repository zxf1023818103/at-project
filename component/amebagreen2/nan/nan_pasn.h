#ifndef GREEN2_NAN_PASN_H
#define GREEN2_NAN_PASN_H
#include <stddef.h>
int green2_pasn_init(void);
int green2_pasn_event(unsigned short id, const void *data, size_t len);
int green2_pasn_cache_update(const void *data, size_t len, unsigned char peer_instance);
int green2_pasn_peer_authorized(const unsigned char peer[6]);
void green2_pasn_timers(void);
/* NAN-task-only: wait no longer than the next hostap timer or max_wait_ms. */
unsigned green2_pasn_next_timeout_ms(unsigned max_wait_ms);
void green2_pasn_stop(void);
/* Discard live-session authorization; retain initialized cache and identities. */
void green2_pasn_pause(void);
int green2_pasn_resume(void);
#endif
