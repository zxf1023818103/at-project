/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
/* Validate first; emit only whole standard HCI events. No allocation.
 * Returns packet count, or -1 without emitting when malformed / insufficient
 * queue space. Caller keeps its producer context serialized across the call. */
int tl3238_cs_hci_split(const uint8_t *p, unsigned n, unsigned available,
                        void (*emit)(const uint8_t *, unsigned, void *), void *arg);
