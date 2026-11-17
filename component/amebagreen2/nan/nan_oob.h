#ifndef GREEN2_NAN_OOB_H
#define GREEN2_NAN_OOB_H
#include "nan_oob_protocol.h"
int green2_nan_oob_request(void);
int green2_nan_oob_pending(void);
void green2_nan_oob_open(void);
void green2_nan_oob_close(void);
void green2_nan_oob_error(void);
void green2_nan_oob_read(uint8_t out[NAN_OOB_PACKET_SIZE]);
int green2_nan_oob_password(char pin[7], uint32_t *generation);
int green2_nan_oob_valid(uint32_t generation);
#endif
