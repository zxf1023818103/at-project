#ifndef GREEN2_NAN_OOB_PROTOCOL_H
#define GREEN2_NAN_OOB_PROTOCOL_H
#include <stdint.h>
#define NAN_OOB_PACKET_SIZE 20
#define NAN_OOB_WINDOW_SECONDS 300
enum { NAN_OOB_CLOSED, NAN_OOB_READY, NAN_OOB_PENDING, NAN_OOB_ERROR };
int nan_oob_pin(uint32_t entropy, char pin[7]);
void nan_oob_packet(uint8_t out[NAN_OOB_PACKET_SIZE], unsigned state,
                    unsigned seconds, const char pin[7], const uint8_t id[6]);
#endif
