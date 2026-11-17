#pragma once
#include <stdint.h>

/* The reader implements HCI LE Read Peer Resolvable Address. */
typedef int (*green2_read_peer_rpa)(uint8_t identity_type,
                                   const uint8_t identity[6], uint8_t rpa[6]);
/* Returns 1 only when a missing RPA was recovered; never replaces a supplied one. */
int green2_recover_peer_rpa(uint8_t peer_type, const uint8_t identity[6],
                          uint8_t peer_rpa[6], green2_read_peer_rpa read_rpa);
