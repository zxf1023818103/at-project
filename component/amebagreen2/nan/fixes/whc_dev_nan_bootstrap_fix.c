/* Fresh pairing must remain possible after the initiator forgets its keys.
 * The pinned SDF RX function rejects all plaintext unicast SDFs when the peer
 * has a TK. Exempt only a bounded bootstrap request; do not change peer keys,
 * PASN state, or the checks on ordinary/protected service discovery traffic.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

int green2_nan_fresh_bootstrap(const uint8_t *frame, size_t length)
{
    static const uint8_t public_nan[] = {4, 9, 0x50, 0x6f, 0x9a, 0x13};
    size_t pos = 30;
    unsigned sda = 0, npba = 0;
    uint8_t instance = 0;

    if (!frame || length < 30 || length > 2304 || frame[0] != 0xd0 ||
        (frame[1] & 0x43) || (frame[4] & 1) ||
        memcmp(frame + 24, public_nan, sizeof(public_nan)))
        return 0;
    while (pos < length) {
        unsigned id, size;
        const uint8_t *body;
        if (length - pos < 3)
            return 0;
        id = frame[pos];
        size = frame[pos + 1] | ((unsigned)frame[pos + 2] << 8);
        pos += 3;
        if (!size || size > length - pos)
            return 0;
        body = frame + pos;
        switch (id) {
        case 3: /* Service Descriptor: plain Followup, no optional fields. */
            if (sda++ || npba || size != 9 || body[8] != 2 ||
                !body[6] || !body[7])
                return 0;
            instance = body[6];
            break;
        case 14: /* Extension must belong to that Service Descriptor. */
            if (!sda || size < 3 || body[0] != instance)
                return 0;
            break;
        case 15: /* Device Capability */
            if (size != 9)
                return 0;
            break;
        case 44: /* NPBA: token, request/status, reason, bootstrapping methods. */
            if (!sda || npba++ || size != 5 || !body[0] ||
                body[1] != 1 || body[2] || !(body[3] | body[4]))
                return 0;
            break;
        case 11: /* Country Code */
        case 18: /* Availability */
        case 19: /* NAN Data Cluster */
        case 30: /* Element Container */
        case 221: /* Vendor attribute */
            /* Not interpreted by this SDF RX function; lengths still checked. */
            break;
        default:
            /* In particular never admit Identity, Shared Key, Cipher Suite,
             * NDP, or other security-bearing attributes through this exception. */
            return 0;
        }
        pos += size;
    }
    return sda == 1 && npba == 1;
}

/* Called only at the pinned memcmp site. Keep the original destination
 * comparison for all other frames, including unusual ToDS/FromDS headers. */
int green2_nan_sdf_rx_gate_c(const uint8_t *frame, size_t length,
                           unsigned peer_flags)
{
    static const uint8_t network_id[] = {0x51, 0x6f, 0x9a, 1, 0, 0};
    const uint8_t *destination;
    if (!frame || length < 24)
        return 1;
    destination = frame + ((frame[1] & 1) ? 16 : 4);
    if (!memcmp(destination, network_id, sizeof(network_id)))
        return 0;
    if (!green2_nan_fresh_bootstrap(frame, length))
        return 1;
    if (peer_flags & 0x40)
        printf("[NANFIX] fresh bootstrap admitted with previous peer TK\n");
    return 0;
}

#if defined(__arm__) || defined(__thumb__)
/* r5=frame, r9=length, r4=peer at rtw_nan_sdf_rx+0x56. No stack changes;
 * the C callee preserves the vendor function's callee-saved registers. */
__attribute__((naked)) int green2_nan_sdf_rx_gate(void)
{
    __asm__ volatile("mov r0, r5\n"
                     "mov r1, r9\n"
                     "ldrb r2, [r4, #177]\n"
                     "b green2_nan_sdf_rx_gate_c\n");
}
#endif
