/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ras_codec.h"
#include "ras_control.h"
#include "ras_selftest.h"
static struct ras_record local;
static struct ras_rx remote, received;
static struct cs_distance_work work;
static struct cs_distance_result result;
static void distance(float meters)
{
    assert(!ras_synthetic_pair(&local, &remote, meters));
    assert(!cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result));
    assert(abs(result.distance_mm - (int)lroundf(meters * 1000)) <= 10);
    assert(result.channels == 72 && result.residual_mrad < 10);
}
int main(void)
{
    /* Real procedures include role-dependent Mode 0 data and may span subevents. */
    struct ras_record init = {0}, refl = {0};
    const uint8_t iraw[] = {0,2,5, 0,216,1,0,0, 2,40,9, 0,1,0,0,0,0,0,0,16};
    const uint8_t rraw[] = {0,2,3, 0,216,1, 2,40,9, 0,1,0,0,0,0,0,0,16};
    struct ras_fragment f = {.counter=0xabcd,.config=2,.start_acl_event=9,.paths=1,.num_steps=2,.procedure_status=1};
    for (unsigned i=0;i<2;i++) {
        f.start_acl_event=9+i; f.procedure_status=i?0:1;
        f.steps=iraw; f.steps_len=sizeof iraw; assert(!ras_record_append(&init,&f,0,-9));
        f.steps=rraw; f.steps_len=sizeof rraw; assert(!ras_record_append(&refl,&f,1,-9));
    }
    /* Erratum 26610: the fourth header octet is Previously Used / RFU, not a mask. */
    const uint8_t header[] = {0xcd,0x2b,0xf7,0x00};
    assert(!memcmp(init.body,header,sizeof header) && !memcmp(refl.body,header,sizeof header));
    assert(init.complete && refl.complete && init.subevents==2 && init.step_count==4);
    assert(init.len==52 && refl.len==48);
    assert(cs_distance_estimate(&init,refl.body,refl.len,0,&work,&result)==RAS_QUALITY);
    distance(2.5f);
    assert(local.body[3] == 0 && remote.body[3] == 0);
    /* Core Vol 1, Part E, 2.4.1/2.4.2: ignore all values on receipt, including
     * legacy mask 0x01 and nonzero upper RFU bits, without shifting step data. */
    for (unsigned reserved = 0; reserved < 256; reserved++) {
        remote.body[3] = reserved;
        assert(!cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result));
        assert(abs(result.distance_mm - 2500) <= 10 && result.channels == 72);
    }
    distance(0.2f); distance(2.5f); distance(10.0f); distance(25.0f); distance(50.0f);
    assert(!cs_distance_estimate(&local, remote.body, remote.len, 123, &work, &result));
    assert(abs(result.distance_mm - 49877) < 10);
    /* MTU 23 wraps the 6-bit segment counter, also covers typical MTU 247. */
    for (unsigned mtu = 23; mtu <= 247; mtu += 224) {
        memset(&received, 0, sizeof received);
        unsigned offset = 0, seq = 61; uint8_t p[244];
        while (offset < remote.len) {
            unsigned n = ras_segment(remote.body, remote.len, offset, seq++, mtu, p, sizeof p);
            assert(n >= 2 && n <= mtu - 3);
            assert(!ras_rx_segment(&received, p, n)); offset += n - 1;
        }
        assert(received.complete && received.len == remote.len);
        assert(!memcmp(received.body, remote.body, remote.len));
        assert(!cs_distance_estimate(&local, received.body, received.len, 0, &work, &result));
    }
    uint8_t packet[244];
    memset(&received, 0, sizeof received);
    unsigned n = ras_segment(remote.body, remote.len, 0, 0, 23, packet, sizeof packet);
    assert(!ras_rx_segment(&received, packet, n));
    assert(ras_rx_segment(&received, packet, n) == RAS_SEQUENCE); /* Duplicate first. */
    n = ras_segment(remote.body, remote.len, 38, 2, 23, packet, sizeof packet);
    assert(ras_rx_segment(&received, packet, n) == RAS_SEQUENCE); /* Lost segment. */
    remote.body[0] ^= 1;
    assert(cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result) == RAS_MISMATCH);
    remote.body[0] ^= 1;
    assert(cs_distance_estimate(&local, remote.body, remote.len - 1, 0, &work, &result));
    remote.body[4] ^= 1;
    assert(cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result) == RAS_MISMATCH);
    distance(2.5f);
    /* One locally aborted step still occupies its step index, with no payload. */
    local.body[12] = 0x80;
    memmove(local.body + 13, local.body + 22, local.len - 22); local.len -= 9;
    assert(!cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result));
    assert(result.channels == 71 && result.pairs == 71);
    distance(2.5f);
    for (unsigned i = 0; i < 72; i++) remote.body[12 + 10*i + 5] = 2; /* Bad tone quality. */
    assert(cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result) == RAS_QUALITY);
    assert(result.channels == 0);
    distance(2.5f);
    /* Corrupt phase across frequencies must fail the residual/coherence gate. */
    for (unsigned i = 0; i < 72; i++) {
        remote.body[12+10*i+2] ^= (i*71); remote.body[12+10*i+3] ^= (i*137);
    }
    assert(cs_distance_estimate(&local, remote.body, remote.len, 0, &work, &result) == RAS_QUALITY);
    /* Mandatory CP semantics and optional Abort, including in-flight ACK rules. */
    uint8_t cp[] = {0,37,0}, reply[2];
    assert(ras_control_request(cp, 3, RAS_SERVER_IDLE, 1, 37, reply) == RAS_GET);
    assert(ras_control_request(cp, 2, RAS_SERVER_IDLE, 1, 37, reply) == RAS_REPLY && reply[1] == 3);
    assert(ras_control_request(cp, 3, RAS_SERVER_IDLE, 0, 37, reply) == RAS_REPLY && reply[1] == 8);
    assert(ras_control_request(cp, 3, RAS_SERVER_SEND, 1, 37, reply) == RAS_REPLY && reply[1] == 7);
    cp[0] = 1;
    assert(ras_control_request(cp, 3, RAS_SERVER_ACK, 1, 37, reply) == RAS_ACK && reply[1] == 1);
    assert(ras_control_request(cp, 3, RAS_SERVER_SEND, 1, 37, reply) == RAS_REPLY && reply[1] == 7);
    cp[0] = 3;
    assert(ras_control_request(cp, 1, RAS_SERVER_SEND, 1, 37, reply) == RAS_ABORT && reply[1] == 1);
    assert(ras_control_request(cp, 1, RAS_SERVER_IDLE, 1, 37, reply) == RAS_REPLY && reply[1] == 5);
    cp[0] = 4;
    assert(ras_control_request(cp, 3, RAS_SERVER_IDLE, 1, 37, reply) == RAS_REPLY && reply[1] == 2);
    /* Parser fuzzing uses the actual production parser under sanitizers. */
    uint8_t fuzz[3000]; unsigned rng = 0x185b;
    distance(2.5f);
    for (unsigned i = 0; i < 100000; i++) {
        for (unsigned j = 0; j < sizeof fuzz; j++) { rng = rng*1664525+1013904223; fuzz[j] = rng >> 24; }
        unsigned len = rng % sizeof fuzz;
        if (len >= 4) { memcpy(fuzz, local.body, 4); }
        (void)cs_distance_estimate(&local, fuzz, len, 0, &work, &result);
        memset(&received, 0, sizeof received);
        (void)ras_rx_segment(&received, fuzz, len);
        (void)ras_control_request(fuzz, len, rng%3, 1, 37, reply);
    }
    puts("PASS: RAS erratum 26610 header / all 256 reserved values, MTU23/247, rolling counter, missing/duplicate/truncated data, CP Get/ACK/Abort, PBR 0.2/2.5/10/25/50m, quality gates, 100000 malformed packets (ASan/UBSan)");
    return 0;
}
