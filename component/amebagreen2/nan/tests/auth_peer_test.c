#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint8_t __wrap_rtw_nan_pasn_rx_checking(uint8_t *, uint8_t *, uint16_t);

static uint8_t address[6], frame[2305], peer[544];
static unsigned allocations, native_calls;
static int exists, allocation_fails;
static uint8_t native_result;
static uint8_t *expected_address, *expected_frame;
static uint16_t expected_length;

uint8_t *rtw_nan_peer_info_get(const uint8_t *mac)
{
    assert(!memcmp(mac, address, 6));
    return exists ? peer : NULL;
}

uint8_t *rtw_nan_peer_info_alloc(const uint8_t *mac)
{
    assert(!memcmp(mac, address, 6));
    assert(!exists);
    ++allocations;
    if (allocation_fails)
        return NULL;
    memset(peer, 0, sizeof(peer));
    exists = 1;
    return peer;
}

uint8_t __real_rtw_nan_pasn_rx_checking(uint8_t *mac, uint8_t *data, uint16_t length)
{
    assert(mac == expected_address && data == expected_frame && length == expected_length);
    ++native_calls;
    return native_result;
}

static void fresh(void)
{
    const uint8_t source[] = {2, 3, 4, 5, 6, 7};
    memset(frame, 0, sizeof(frame));
    memset(peer, 0xa5, sizeof(peer));
    memcpy(address, source, 6);
    frame[0] = 0xb0;
    frame[4] = 2; frame[9] = 8;
    memcpy(frame + 10, source, 6);
    frame[24] = 7; frame[26] = 1;
    /* Structurally complete IEs, not an authenticated PASN exchange. */
    frame[30] = 48; frame[31] = 2; frame[32] = 1;
    frame[34] = 255; frame[35] = 1; frame[36] = 100;
    exists = allocation_fails = 0;
    allocations = native_calls = 0;
    native_result = 0;
}

static uint8_t run(uint8_t *mac, uint8_t *data, uint16_t length)
{
    expected_address = mac; expected_frame = data; expected_length = length;
    return __wrap_rtw_nan_pasn_rx_checking(mac, data, length);
}

static void assert_unmodified_frame(void)
{
    uint8_t copy[sizeof(frame)];
    memcpy(copy, frame, sizeof(copy));
    (void)run(address, frame, 37);
    assert(!memcmp(copy, frame, sizeof(copy)));
}

static void test_early_existing_and_failure(void)
{
    fresh();
    assert_unmodified_frame();
    assert(allocations == 1 && native_calls == 1 && exists);
    /* Creating the context cannot authorize it: native rejection is preserved. */
    for (unsigned i = 0; i < sizeof(peer); ++i) assert(peer[i] == 0);
    frame[1] = 8; /* retry of the same Auth1: reuse the context */
    native_result = 1;
    assert(run(address, frame, 37) == 1);
    assert(allocations == 1 && native_calls == 2);

    fresh(); exists = 1;
    assert_unmodified_frame();
    assert(allocations == 0 && native_calls == 1);
    for (unsigned i = 0; i < sizeof(peer); ++i) assert(peer[i] == 0xa5);

    fresh(); allocation_fails = 1;
    assert(run(address, frame, 37) == 0);
    assert(allocations == 1 && native_calls == 1 && !exists);
    allocation_fails = 0;
    assert(run(address, frame, 37) == 0);
    assert(allocations == 2 && native_calls == 2 && exists);
}

static void no_allocation(void)
{
    native_result = 0x5a;
    assert(run(address, frame, 37) == native_result);
    assert(allocations == 0 && native_calls == 1);
}

static void test_other_transactions_and_headers(void)
{
    const uint8_t flags[] = {1, 2, 4, 0x40, 0x80};
    for (unsigned i = 0; i < sizeof(flags); ++i) {
        fresh(); frame[1] = flags[i]; no_allocation();
    }
    fresh(); frame[22] = 1; no_allocation();
    fresh(); frame[0] = 0xd0; no_allocation();
    fresh(); frame[24] = 3; no_allocation();
    fresh(); frame[25] = 1; no_allocation();
    fresh(); frame[26] = 2; no_allocation();
    fresh(); frame[26] = 3; no_allocation();
    fresh(); frame[27] = 1; no_allocation();
    fresh(); frame[28] = 1; no_allocation();
    fresh(); frame[29] = 1; no_allocation();
    fresh(); frame[4] |= 1; no_allocation();
    fresh(); memset(frame + 4, 0, 6); no_allocation();
    fresh(); address[0] |= 1; memcpy(frame + 10, address, 6); no_allocation();
    fresh(); memset(address, 0, 6); memset(frame + 10, 0, 6); no_allocation();
    fresh(); frame[15] ^= 1; no_allocation();
    fresh(); frame[35] = 2; no_allocation(); /* final IE extends past end */
    fresh(); frame[31] = 255; no_allocation();
}

static void test_bounds(void)
{
    fresh();
    for (unsigned n = 0; n < 30; ++n)
        assert(run(address, frame, n) == 0);
    assert(run(NULL, frame, 37) == 0);
    assert(run(address, NULL, 37) == 0);
    assert(run(address, frame, 2305) == 0);
    assert(allocations == 0 && native_calls == 0);
    /* Short IE lists go to the native validator but never create a peer. */
    assert(run(address, frame, 30) == 0);
    assert(run(address, frame, 31) == 0);
    assert(run(address, frame, 36) == 0);
    assert(allocations == 0 && native_calls == 3);
    /* The upper bound and zero-length IEs must not overflow or loop forever. */
    fresh();
    memset(frame + 30, 0, sizeof(frame) - 30);
    assert(run(address, frame, 2304) == 0);
    assert(allocations == 1 && native_calls == 1);
    fresh();
    memset(frame + 30, 0, sizeof(frame) - 30);
    assert(run(address, frame, 2303) == 0);
    assert(allocations == 0 && native_calls == 1);
}

int main(void)
{
    test_early_existing_and_failure();
    test_other_transactions_and_headers();
    test_bounds();
    puts("PASN early-peer context tests passed");
    return 0;
}
