/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "ble_gap_priv.h"
#include "mbedtls/aes.h"
#include "mbedtls/platform_util.h"

extern void tl3238_ble_log(const char *format, ...);
/* Only upstream ble_gap.c is compiled with this entry point renamed. */
int tl3238_gap_rx_conn_complete(struct ble_gap_conn_complete *evt, uint8_t instance);

/* Core Vol 3, Part H, 2.2.2: ah(k, r) = AES-128(k, 0 || r) mod 2^24.
 * NimBLE stores IRKs and HCI addresses least significant octet first;
 * Mbed TLS takes the most significant octet first. */
static int rpa_matches(const uint8_t irk[16], const uint8_t rpa[6])
{
    mbedtls_aes_context aes;
    uint8_t key[16], block[16] = {0};
    for (unsigned i = 0; i < sizeof key; ++i) key[i] = irk[15 - i];
    for (unsigned i = 0; i < 3; ++i) block[15 - i] = rpa[3 + i];
    mbedtls_aes_init(&aes);
    int rc = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (!rc) rc = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, block, block);
    int match = !rc && block[15] == rpa[0] && block[14] == rpa[1] && block[13] == rpa[2];
    mbedtls_aes_free(&aes);
    mbedtls_platform_zeroize(key, sizeof key);
    mbedtls_platform_zeroize(block, sizeof block);
    return rc ? -1 : match;
}

static int resolve_rpa(const uint8_t rpa[6], ble_addr_t *identity)
{
    struct ble_store_key_sec key = { .peer_addr = *BLE_ADDR_ANY };
    struct ble_store_value_sec peer = {0};
    int found = 0;
    for (unsigned i = 0; i < MYNEWT_VAL(BLE_STORE_MAX_BONDS); ++i) {
        key.idx = i;
        int rc = ble_store_read_peer_sec(&key, &peer);
        if (rc == BLE_HS_ENOENT) break;
        if (rc) { found = 0; break; }
        if (!peer.irk_present) continue;
        rc = rpa_matches(peer.irk, rpa);
        if (rc < 0) { found = 0; break; }
        if (!rc) continue;
        /* Never choose between colliding identities or malformed records. */
        if (found || (peer.peer_addr.type != BLE_ADDR_PUBLIC &&
                      peer.peer_addr.type != BLE_ADDR_RANDOM)) {
            found = 0;
            break;
        }
        *identity = peer.peer_addr;
        found = 1;
    }
    mbedtls_platform_zeroize(&peer, sizeof peer);
    return found;
}

int ble_gap_rx_conn_complete(struct ble_gap_conn_complete *evt, uint8_t instance)
{
    /* TL323X can report an unresolved RPA despite a populated resolving list.
     * Resolve on the HOST task before GAP creates the connection: doing this
     * in the Controller transport callback would invert the host/HCI locks.
     * Leave failed events, public/static addresses and Controller-resolved
     * identities untouched. Link encryption still authenticates the peer. */
    if (!evt->status && evt->peer_addr_type == BLE_ADDR_RANDOM &&
        (evt->peer_addr[5] & 0xc0) == 0x40) {
        ble_addr_t identity;
        if (resolve_rpa(evt->peer_addr, &identity)) {
            memcpy(evt->peer_rpa, evt->peer_addr, sizeof evt->peer_rpa);
            memcpy(evt->peer_addr, identity.val, sizeof evt->peer_addr);
            evt->peer_addr_type = identity.type == BLE_ADDR_PUBLIC ?
                BLE_ADDR_PUBLIC_ID : BLE_ADDR_RANDOM_ID;
            tl3238_ble_log("BLE privacy host_resolved=1");
        }
    }
    return tl3238_gap_rx_conn_complete(evt, instance);
}
