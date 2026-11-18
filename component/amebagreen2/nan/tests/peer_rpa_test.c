#include "peer_rpa.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static const uint8_t identity[6] = {1, 2, 3, 4, 5, 6};
static uint8_t reply[6] = {7, 8, 9, 10, 11, 0x42};
static int calls, result;
static uint8_t requested_type;
static int read_rpa(uint8_t type, const uint8_t address[6], uint8_t rpa[6])
{
    assert(!memcmp(identity, address, 6));
    calls++; requested_type = type;
    memcpy(rpa, reply, 6);
    return result;
}
int main(void)
{
    const uint8_t zero[6] = {0};
    uint8_t rpa[6] = {0};
    /* Both resolved identity types query the matching controller entry. */
    for (uint8_t type = 2; type <= 3; type++) {
        memset(rpa, 0, 6);
        assert(green2_recover_peer_rpa(type, identity, rpa, read_rpa) == 1);
        assert(requested_type == type - 2 && !memcmp(rpa, reply, 6));
    }
    /* Never touch an RPA already supplied by the connection event. */
    calls = 0;
    assert(green2_recover_peer_rpa(2, identity, rpa, read_rpa) == 0 && calls == 0);
    memset(rpa, 0, 6);
    for (unsigned type = 0; type < 256; type++) {
        if (type == 2 || type == 3) continue;
        assert(!green2_recover_peer_rpa(type, identity, rpa, read_rpa));
    }
    assert(calls == 0);
    assert(!green2_recover_peer_rpa(2, zero, rpa, read_rpa) && calls == 0);
    assert(!green2_recover_peer_rpa(2, identity, rpa, NULL));
    result = 1;
    assert(!green2_recover_peer_rpa(2, identity, rpa, read_rpa));
    assert(!memcmp(rpa, zero, 6));
    result = 0;
    /* Reject a public/static address or invalid prand without changing state. */
    const uint8_t invalid[][6] = {{0}, {1,2,3,4,5,0xc0}, {1,2,3,0,0,0x40}, {1,2,3,255,255,0x7f}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        memcpy(reply, invalid[i], 6);
        assert(!green2_recover_peer_rpa(3, identity, rpa, read_rpa));
        assert(!memcmp(rpa, zero, 6));
    }
    puts("resolved peer RPA recovery PASS");
}
