/* The pinned NDL RX could destroy an established peer on a new NDP
 * Request. That also discarded PASN keys needed by the next NDP. Preserve
 * only an already authenticated peer receiving a protected, same-NMI request.
 * Normal timeout, unpair, key teardown and non-authenticated RX are unchanged.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

int green2_nan_ndl_keep_peer(const uint8_t *peer, const uint8_t *frame,
                            size_t length, unsigned subtype)
{
    static const uint8_t ndp_request[] = {9, 9, 0x50, 0x6f, 0x9a, 0x18, 5};
    uint32_t context;
    /* Replay the original CMP/BNE for other action subtypes. */
    if (subtype != 5)
        return 1;
    if (!peer || !frame || length < 31 || length > 2304 ||
        frame[0] != 0xd0 || (frame[1] & 0x43) != 0x40 ||
        (frame[4] & 1) || memcmp(frame+24, ndp_request, sizeof(ndp_request)) ||
        memcmp(frame+10, peer+67, 6) || !(peer[177] & 0x40))
        return 0;
    memcpy(&context, peer+388, sizeof(context));
    return context != 0;
}

/* Both NDP (16) and NDPE (41) carry the same fixed 14-byte prefix. Reject
 * inconsistent/short attributes instead of guessing which transaction won.
 */
const uint8_t *green2_nan_ndp_request_attr(const uint8_t *frame, size_t length)
{
    const uint8_t *found = NULL;
    size_t pos = 31;
    unsigned seen = 0;
    if (!frame || length < pos || length > 2304)
        return NULL;
    while (pos < length) {
        unsigned id, size, bit;
        const uint8_t *attr = frame + pos;
        if (length-pos < 3)
            return NULL;
        id = attr[0];
        size = attr[1] | ((unsigned)attr[2] << 8);
        if (!size || size > length-pos-3)
            return NULL;
        if (id == 16 || id == 41) {
            bit = id == 16 ? 1 : 2;
            if ((seen & bit) || size < 11 || attr[4] != 0 || !attr[12])
                return NULL;
            if (found && memcmp(found+3, attr+3, 10))
                return NULL;
            found = attr;
            seen |= bit;
        }
        pos += size+3;
    }
    return found;
}

int green2_nan_ndp_confirm_pending(const uint8_t *entry, const uint8_t *attr)
{
    /* Private offsets pinned by the vendor archive: transaction token=11,
     * initiator NDI=14, NDP ID=20, negotiation state=428 (4=await Confirm).
     */
    return entry && attr && entry[428] == 4 && entry[11] == attr[3] &&
           entry[20] == attr[12] && !memcmp(entry+14, attr+6, 6);
}

#if defined(__arm__) || defined(__thumb__)
extern uint8_t *rtw_nan_ndp_entry_find(const uint8_t *, const uint8_t *, unsigned);
/* r4=peer, r7=frame+10, r8=frame_end, r6=action subtype. The caller already
 * validated attributes. No pointers/keys are modified by this adapter.
 */
static __attribute__((used,noinline)) int ndl_gate_c(const uint8_t *peer,
    const uint8_t *source, const uint8_t *end, unsigned subtype)
{
    size_t length = (size_t)(end-(source-10));
    int keep = green2_nan_ndl_keep_peer(peer, source-10, length, subtype);
    if (keep && subtype == 5 && peer[110] == 0) {
        const uint8_t *attr = green2_nan_ndp_request_attr(source-10, length);
        if (attr && green2_nan_ndp_confirm_pending(
                rtw_nan_ndp_entry_find(peer, attr+12, 2), attr)) {
            /* NDL response already sent. Replaying its Request must not
             * change NDL None -> Rx Request while NDP awaits Confirm.
             * The caller still runs its normal NDP duplicate/state checks.
             */
            printf("[NANFIX] kept NDL state for duplicate NDP request awaiting confirm\n");
            return 2;
        }
    }
    if (keep && subtype == 5)
        printf("[NANFIX] preserved authenticated peer for new NDP request\n");
    return keep;
}

__attribute__((naked)) void green2_nan_ndl_rx_gate(void)
{
    __asm__ volatile(
        "push {r0-r3, r12, lr}\n" /* 24 bytes: keep AAPCS 8-byte alignment */
        "mov r0, r4\n"
        "mov r1, r7\n"
        "mov r2, r8\n"
        "mov r3, r6\n"
        "bl ndl_gate_c\n"
        "cmp r0, #0\n"
        "beq 1f\n"
        "cmp r0, #2\n"
        "beq 2f\n"
        "ldr r0, [sp, #20]\n"
        "add r0, r0, #128\n" /* return +0x40 -> native keep path +0xc0 */
        "str r0, [sp, #20]\n"
        "b 1f\n"
        "2: ldr r0, [sp, #20]\n"
        "add r0, r0, #152\n" /* return +0x40 -> native epilogue +0xd8 */
        "str r0, [sp, #20]\n"
        "1: pop {r0-r3, r12, lr}\n"
        "bx lr\n");
}
#endif
