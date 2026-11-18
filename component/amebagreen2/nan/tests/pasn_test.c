/* Exercise the complete responder using a hostap PASN initiator. Test keys
 * are generated afresh; no captured phone secrets are required. */
#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "common/ieee802_11_defs.h"
#include "common/nan_defs.h"
#include "common/wpa_common.h"
#include "common/sae.h"
#include "crypto/sha256.h"
#include "pasn/pasn_common.h"
#include "nan_pasn.h"
#include <assert.h>

static u8 tx[512], rx[512], installed[86];
static size_t tx_len, rx_len;
static unsigned installations, authenticated;
static unsigned expected_type = 1;
static u8 nik_cache[500], stored[888];
static int stored_valid, store_fail;
static unsigned store_reads;
static const u8 peer[6] = {2,1,2,3,4,5}, own[6] = {2,1,2,3,4,6};
#ifdef AT_NAN_BLE_OOB
static int window_open;
static uint32_t window_generation = 1;
int green2_nan_oob_password(char pin[7], uint32_t *generation)
{
    if (!window_open) return 0;
    memcpy(pin, "123456", 7); *generation = window_generation; return 1;
}
int green2_nan_oob_valid(uint32_t generation)
{ return window_open && generation == window_generation; }
#endif

int green2_nan_store(void *p, size_t n, int write)
{
    assert(n == sizeof(stored));
    if (write) { if (store_fail) return -1; memcpy(stored, p, n); stored_valid = 1; return 0; }
    ++store_reads;
    if (!stored_valid) return -1;
    memcpy(p, stored, n); return 0;
}
int green2_nan_mgmt(const u8 *p, size_t n)
{ assert(n <= sizeof(rx)); memcpy(rx, p, n); rx_len = n; return 0; }
int green2_nan_vendor(unsigned short id, const void *p, size_t n)
{
    if (id == 0x1909) {
        assert(n == sizeof(installed)); memcpy(installed, p, n); installations++;
    } else if (id == 0x1907) {
        const u8 *pw = p;
        assert(n == 24 && !memcmp(pw, peer, 6) && !memcmp(pw + 6, "123456", 6) && pw[23] == 6);
    } else { assert(id == 0x190a && n == 500); memcpy(nik_cache, p, n); }
    return 0;
}
void green2_nan_authenticated(const u8 *p, u8 type)
{
    assert(!memcmp(p, peer, 6) && type == expected_type); authenticated++;
#ifdef AT_NAN_BLE_OOB
    if (type == 1) window_open = 0;
#endif
}
static int capture(void *ctx, const u8 *p, size_t n, int noack, unsigned freq, unsigned wait)
{
    (void)ctx; (void)noack; (void)freq; (void)wait;
    assert(n <= sizeof(tx)); memcpy(tx, p, n); tx_len = n; return 0;
}
static int validate_responder(void *ctx, const u8 *addr, const u8 *pmkid)
{ return memcmp(addr, own, 6) || memcmp(ctx, pmkid, 16) ? -1 : 0; }

static void make_pmkid(u8 pmkid[16], const u8 nik[16], const u8 nmi[6])
{
    u8 message[17], tag[32];
    assert(!os_get_random(pmkid, 8));
    memcpy(message, "NIR", 3); memcpy(message + 3, nmi, 6);
    memcpy(message + 9, pmkid, 8);
    assert(!hmac_sha256(nik, 16, message, sizeof(message), tag));
    memcpy(pmkid + 8, tag, 8);
}

static void verify_cached_pairing(void)
{
    u8 identity[16], tag[32], message[17], local_pmkid[16];
    u8 ie[26] = {221, 24, 0x50, 0x6f, 0x9a, 0x13,
                 NAN_ATTR_NIRA, 17, 0, NAN_NIRA_CIPHER_VER_128};
    memset(identity, 0x42, sizeof(identity));
    memset(local_pmkid, 0xa5, sizeof(local_pmkid));
    assert(!os_get_random(ie + 10, 8));
    memcpy(message, "NIR", 3); memcpy(message + 3, peer, 6); memcpy(message + 9, ie + 10, 8);
    assert(!hmac_sha256(identity, sizeof(identity), message, sizeof(message), tag));
    memcpy(ie + 18, tag, 8);
    struct pasn_data *p = pasn_data_init(); assert(p);
    struct rsn_pmksa_cache *pmksa = pasn_initiator_pmksa_cache_init(); assert(pmksa);
    assert(!pasn_initiator_pmksa_cache_add(pmksa, peer, own, stored + 520, 32, ie + 10, WPA_KEY_MGMT_SAE));
    pasn_set_initiator_pmksa(p, pmksa);
    pasn_set_custom_pmkid(p, ie + 10);
    assert(!pasn_set_extra_ies(p, ie, sizeof(ie)));
    p->auth_alg = WLAN_AUTH_PASN;
    pasn_enable_kdk_derivation(p);
    pasn_register_callbacks(p, local_pmkid, capture, validate_responder, NULL, NULL);
    assert(!wpa_pasn_verify(p, peer, own, own, WPA_KEY_MGMT_SAE, WPA_CIPHER_CCMP, 19, 2437, NULL, 0, NULL, 0, NULL));
    u8 start[645] = {0};
    start[1] = 2; start[2] = 2; memcpy(start + 3, peer, 6); memcpy(start + 9, own, 6);
    start[15] = start[16] = 1; start[17] = 6; memcpy(start + 18, "123456", 6);
    WPA_PUT_LE16(start + 34, tx_len); memcpy(start + 36, tx, tx_len);
    memcpy(start + 613, local_pmkid, 16);
    /* A valid NIRA must also have an NPK bound to that exact identity. */
    stored[504] ^= 1;
    assert(!green2_pasn_init());
    assert(green2_pasn_event(29, start, sizeof(start)) < 0);
    green2_pasn_stop(); stored[504] ^= 1; assert(!green2_pasn_init());
    /* Reject a forged NIRA and any truncated trailing IE. */
    start[36 + tx_len - 1] ^= 1;
    assert(green2_pasn_event(29, start, sizeof(start)) < 0);
    start[36 + tx_len - 1] ^= 1;
    WPA_PUT_LE16(start + 34, tx_len + 1);
    assert(green2_pasn_event(29, start, sizeof(start)) < 0);
    WPA_PUT_LE16(start + 34, tx_len);
    expected_type = 2;
    assert(!green2_pasn_event(29, start, sizeof(start)));
    struct wpa_pasn_params_data params = {0};
    assert(!wpa_pasn_auth_rx(p, rx, rx_len, &params));
    assert(tx[26] == 3);
    u8 auth3[598] = {3,2,6}; memcpy(auth3 + 3, "123456", 6);
    WPA_PUT_LE16(auth3 + 19, tx_len); memcpy(auth3 + 21, tx, tx_len);
    assert(!green2_pasn_event(30, auth3, sizeof(auth3)));
    assert(installations == 2 && authenticated == 2 && green2_pasn_peer_authorized(peer));
    assert(!memcmp(installed, p->ptk.tk, 16) && !memcmp(installed + 16, p->ptk.kdk, 32));
    green2_pasn_stop(); wpa_pasn_reset(p); pasn_data_deinit(p); pasn_initiator_pmksa_cache_deinit(pmksa);
}
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern int wpa_debug_level; wpa_debug_level = MSG_WARNING;
    int groups[] = {19, 0};
    const u8 rsnxe[] = {WLAN_EID_RSNX, 2, 1 | (1 << WLAN_RSNX_CAPAB_SAE_H2E), 0};
    assert(!green2_pasn_init());
    struct pasn_data *p = pasn_data_init(); assert(p);
    struct rsn_pmksa_cache *pmksa = pasn_initiator_pmksa_cache_init(); assert(pmksa);
    pasn_set_initiator_pmksa(p, pmksa);
    struct sae_pt *pt = sae_derive_pt(groups, (const u8 *)NAN_PASN_SSID, strlen(NAN_PASN_SSID), (const u8 *)"123456", 6, NULL, 0);
    assert(pt && !pasn_set_pt(p, pt));
    p->auth_alg = WLAN_AUTH_PASN;
    pasn_enable_kdk_derivation(p); pasn_set_rsnxe_ie(p, rsnxe);
    pasn_register_callbacks(p, NULL, capture, NULL, NULL, NULL);
    assert(!wpas_pasn_start(p, peer, own, own, WPA_KEY_MGMT_SAE, WPA_CIPHER_CCMP, 19, 2437, NULL, 0, NULL, 0, NULL));
    u8 start[645] = {0};
    start[1] = 2; start[2] = 1; memcpy(start + 3, peer, 6); memcpy(start + 9, own, 6);
    start[15] = start[16] = 1; start[17] = 6; memcpy(start + 18, "123456", 6);
    WPA_PUT_LE16(start + 34, tx_len); memcpy(start + 36, tx, tx_len);
    u8 pmkid_update[41] = {2, 1};
    memcpy(pmkid_update + 2, peer, 6); pmkid_update[40] = 1;
    /* A PMKID event before authenticated Auth3 cannot nominate a key. */
    assert(!green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)));
#ifdef AT_NAN_BLE_OOB
    assert(green2_pasn_event(29, start, sizeof(start)) < 0);
    assert(!installations && !authenticated);
    window_open = 1;
    /* The AP must use the active BLE credential even if the NP/cache still
     * reports a previous password. This also covers an empty NP password. */
    start[17] = 0; memset(start + 18, 0, 16);
#endif
    assert(!green2_pasn_event(29, start, sizeof(start)));
    struct wpa_pasn_params_data params = {0};
    assert(!wpa_pasn_auth_rx(p, rx, rx_len, &params));
    assert(tx[26] == 3);
    u8 auth3[598] = {3,1,6}; memcpy(auth3 + 3, "123456", 6);
    WPA_PUT_LE16(auth3 + 19, tx_len); memcpy(auth3 + 21, tx, tx_len);
#ifdef AT_NAN_BLE_OOB
    if (argc > 1) {
        if (!strcmp(argv[1], "expire")) window_open = 0;
        else window_generation++;
        assert(green2_pasn_event(30, auth3, sizeof(auth3)) < 0);
        assert(!installations && !authenticated && !green2_pasn_peer_authorized(peer));
        green2_pasn_stop(); wpa_pasn_reset(p); pasn_data_deinit(p);
        pasn_initiator_pmksa_cache_deinit(pmksa);
        puts("PASS: expired/replaced BLE credential cannot install NAN keys");
        return 0;
    }
#endif
    auth3[21 + tx_len - 1] ^= 1;
    assert(green2_pasn_event(30, auth3, sizeof(auth3)) < 0 && !installations);
    auth3[21 + tx_len - 1] ^= 1;
    assert(!green2_pasn_event(30, auth3, sizeof(auth3)));
    assert(installations == 1 && authenticated == 1);
    assert(green2_pasn_peer_authorized(peer) && !green2_pasn_peer_authorized(own));
    assert(p->ptk.tk_len == 16 && p->ptk.kdk_len == 32);
    assert(!memcmp(installed, p->ptk.tk, 16) && !memcmp(installed + 16, p->ptk.kdk, 32));
    assert(!memcmp(installed + 48, peer, 6));
    assert(!green2_pasn_event(30, auth3, sizeof(auth3)) && installations == 1);
    /* iOS uses lifetime zero for persistent pairings. Bind the NPK to the
     * authenticated NIK rather than treating that lifetime as an empty slot. */
    nik_cache[20 + 8] = 2; nik_cache[20 + 9] = 1; nik_cache[20 + 10] = 7;
    memset(nik_cache + 20 + 11, 0x42, 16);
    nik_cache[20 + 43] = 6; memcpy(nik_cache + 20 + 44, "123456", 6);
    assert(green2_pasn_cache_update(nik_cache, 499, 7) < 0);
    nik_cache[4] ^= 1;
    assert(green2_pasn_cache_update(nik_cache, 500, 7) < 0);
    nik_cache[4] ^= 1;
    /* Historical peers can reuse BOTH the service instance and the static
     * PIN. Bind only the NIRA nominated for this authenticated peer. */
    u8 old_stored[sizeof(stored)]; memcpy(old_stored, stored, sizeof(stored));
    make_pmkid(nik_cache + 47, nik_cache + 31, peer);
    memcpy(nik_cache + 80, nik_cache + 20, 60);
    memset(nik_cache + 91, 0x88, 16);
    make_pmkid(nik_cache + 107, nik_cache + 91, peer);
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    assert(!memcmp(old_stored, stored, sizeof(stored)));
    memcpy(pmkid_update + 8, nik_cache + 47, 16);
    assert(green2_pasn_event(31, pmkid_update, 25) < 0); /* stale public ABI */
    assert(green2_pasn_event(31, pmkid_update, 40) < 0);
    pmkid_update[2] ^= 1;
    assert(green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)) < 0);
    pmkid_update[2] ^= 1;
    pmkid_update[0] = 1;
    assert(green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)) < 0);
    pmkid_update[0] = 2; pmkid_update[1] = 0;
    assert(green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)) < 0);
    pmkid_update[1] = 1; pmkid_update[40] = 0;
    assert(green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)) < 0);
    pmkid_update[40] = 1;
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    /* Matching PMKID bytes alone are insufficient: reject a NIRA calculated
     * for another NMI, then a corrupt tag, without changing saved keys. */
    make_pmkid(nik_cache + 47, nik_cache + 31, own);
    memcpy(pmkid_update + 8, nik_cache + 47, 16);
    assert(!green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)));
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    make_pmkid(nik_cache + 47, nik_cache + 31, peer);
    nik_cache[62] ^= 1;
    memcpy(pmkid_update + 8, nik_cache + 47, 16);
    assert(!green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)));
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    nik_cache[62] ^= 1;
    memcpy(pmkid_update + 8, nik_cache + 47, 16);
    assert(!green2_pasn_event(31, pmkid_update, sizeof(pmkid_update)));
    /* Duplicate identity records remain ambiguous even after NIRA validation. */
    u8 historical[60]; memcpy(historical, nik_cache + 80, sizeof(historical));
    memcpy(nik_cache + 80, nik_cache + 20, 60);
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    memcpy(nik_cache + 80, historical, sizeof(historical));
    memcpy(nik_cache + 64, "654321", 6);
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    memcpy(nik_cache + 64, "123456", 6);
    assert(!memcmp(old_stored, stored, sizeof(stored)));
    store_fail = 1;
    assert(green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7) < 0);
    assert(!memcmp(old_stored, stored, sizeof(stored)));
    store_fail = 0;
    assert(!green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7));
    assert(!memcmp(stored + 504, nik_cache + 31, 16));
    assert(p->pmk_len == 32 && !memcmp(stored + 520, p->pmk, 32));
    assert(!memcmp(old_stored + 552, stored + 552, 48));
    /* Ordinary cache refresh does not bind a new key. A historical credential
     * with the same instance/password must not prevent the refresh. */
    memcpy(nik_cache + 124, "123456", 6);
    assert(!green2_pasn_cache_update(nik_cache, sizeof(nik_cache), 7));
    /* Parking must revoke the old link's authorization. Resume restores the
     * retained identity, without reading flash or authorizing a new link. */
    unsigned reads = store_reads;
    u8 retained_nik[500]; memcpy(retained_nik, nik_cache, sizeof(retained_nik));
    green2_pasn_pause();
    assert(!green2_pasn_peer_authorized(peer));
    memset(nik_cache, 0, sizeof(nik_cache));
    assert(!green2_pasn_resume() && store_reads == reads);
    assert(!green2_pasn_peer_authorized(peer));
    assert(!memcmp(nik_cache, retained_nik, sizeof(nik_cache)));
    green2_pasn_stop();
    assert(!green2_pasn_peer_authorized(peer));
    assert(green2_pasn_resume() < 0); /* A full stop cannot use the resume path. */
    /* Reinitialization loads the same persistent identity without leaking a
     * second responder cache or authorizing a peer before verification. */
    assert(!green2_pasn_init() && !green2_pasn_peer_authorized(peer));
    assert(!memcmp(stored + 4, nik_cache, sizeof(nik_cache)));
    green2_pasn_stop();
    wpa_pasn_reset(p); pasn_data_deinit(p); pasn_initiator_pmksa_cache_deinit(pmksa);
    verify_cached_pairing();
    puts("PASS: PASN/SAE, TK/KDK, tampered/duplicate Auth3, persistent NIK/NPK and NIRA verification");
    return 0;
}
