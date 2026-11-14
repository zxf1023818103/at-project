/* SPDX-License-Identifier: Apache-2.0 */
#include "cs_hci.h"
#include <string.h>
int tl3238_cs_hci_split(const uint8_t *p, unsigned n, unsigned available,
                        void (*emit)(const uint8_t *, unsigned, void *), void *arg)
{
    if (!p || !emit || !n || (p[0] != 0x31 && p[0] != 0x32)) return -1;
    unsigned header = p[0] == 0x31 ? 16 : 9;
    if (n < header) return -1;
    unsigned at = header, count = p[header - 1], fragments = 1, used = header;
    for (unsigned i = 0; i < count; i++) {
        if (at + 3 > n || at + 3 + p[at + 2] > n) return -1;
        unsigned step = 3 + p[at + 2];
        if (step > 246) return -1;
        if (used + step > 255) { fragments++; used = 9; }
        used += step; at += step;
    }
    if (at != n || fragments > available) return -1;
    at = header;
    unsigned done = 0, index = 0;
    uint8_t packet[255];
    do {
        unsigned size = index ? 9 : header, nr = 0;
        if (!index) memcpy(packet, p, header);
        else {
            memcpy(packet, p, 4);
            packet[0] = 0x32;
            memcpy(packet + 4, p + header - 5, 5);
        }
        unsigned packet_header = size;
        while (done < count && size + 3u + p[at + 2] <= sizeof packet) {
            unsigned step = 3u + p[at + 2];
            memcpy(packet + size, p + at, step);
            size += step; at += step; done++; nr++;
        }
        packet[packet_header - 1] = nr;
        if (done < count) {
            packet[packet_header - 5] = 1;
            packet[packet_header - 4] = 1;
            packet[packet_header - 3] = 0;
        }
        emit(packet, size, arg);
        index++;
    } while (done < count);
    return fragments;
}
