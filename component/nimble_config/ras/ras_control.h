/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
enum { RAS_SERVER_IDLE, RAS_SERVER_SEND, RAS_SERVER_ACK };
enum { RAS_REPLY, RAS_GET, RAS_ACK, RAS_ABORT };
/* Validates the actual server CP requests. Optional feature: Abort (bit 2). */
int ras_control_request(const uint8_t *p, unsigned n, unsigned state,
                        int available, uint16_t counter, uint8_t reply[2]);
