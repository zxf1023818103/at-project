#include "utils/includes.h"
#include "utils/common.h"
#include "utils/wpabuf.h"
#include "crypto/crypto.h"
#include "crypto/sha256.h"
#include "crypto/aes_siv.h"
#include "common/sae.h"
#include "common/nan_defs.h"
#include "common/wpa_common.h"
#include <assert.h>
static void hashes(void)
{
    const u8 *parts[] = {(const u8 *)"abc"}; size_t sizes[] = {3}; u8 out[32], expected[32];
    assert(!sha256_vector(1, parts, sizes, out));
    assert(!hexstr2bin("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", expected, 32));
    assert(!memcmp(out, expected, 32));
    u8 key[20]; memset(key, 0x0b, sizeof(key));
    assert(!hmac_sha256(key, sizeof(key), (const u8 *)"Hi There", 8, out));
    assert(!hexstr2bin("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", expected, 32));
    assert(!memcmp(out, expected, 32));
}
static void ecdh(void)
{
    struct crypto_ecdh *a = crypto_ecdh_init(19), *b = crypto_ecdh_init(19);
    assert(a && b); assert(!crypto_ecdh_init(20));
    struct wpabuf *ap = crypto_ecdh_get_pubkey(a, 0), *bp = crypto_ecdh_get_pubkey(b, 0);
    assert(ap && bp);
    struct wpabuf *as = crypto_ecdh_set_peerkey(a, 0, wpabuf_head(bp), wpabuf_len(bp));
    struct wpabuf *bs = crypto_ecdh_set_peerkey(b, 0, wpabuf_head(ap), wpabuf_len(ap));
    assert(as && bs && wpabuf_len(as) == 32 && wpabuf_len(bs) == 32);
    assert(!memcmp(wpabuf_head(as), wpabuf_head(bs), 32));
    u8 invalid[64]; memset(invalid, 0xff, sizeof(invalid));
    assert(!crypto_ecdh_set_peerkey(a, 0, invalid, 32));
    assert(!crypto_ecdh_set_peerkey(a, 1, invalid, 64));
    assert(!crypto_ecdh_set_peerkey(a, 0, wpabuf_head(bp), 31));
    wpabuf_clear_free(as); wpabuf_clear_free(bs); wpabuf_free(ap); wpabuf_free(bp);
    crypto_ecdh_deinit(a); crypto_ecdh_deinit(b);
}
static void sae_pair(const char *other, int success)
{
    int groups[] = {19, 0};
    const u8 aa[] = {2,1,2,3,4,5}, ba[] = {2,1,2,3,4,6};
    struct sae_data a = {0}, b = {0};
    struct sae_pt *at = sae_derive_pt(groups, (const u8 *)NAN_PASN_SSID, strlen(NAN_PASN_SSID), (const u8 *)"123456", 6, NULL, 0);
    struct sae_pt *bt = sae_derive_pt(groups, (const u8 *)NAN_PASN_SSID, strlen(NAN_PASN_SSID), (const u8 *)other, strlen(other), NULL, 0);
    assert(at && bt);
    assert(!sae_set_group(&a, 19) && !sae_set_group(&b, 19));
    a.akmp = b.akmp = WPA_KEY_MGMT_SAE;
    assert(!sae_prepare_commit_pt(&a, at, aa, ba, NULL, NULL));
    assert(!sae_prepare_commit_pt(&b, bt, ba, aa, NULL, NULL));
    struct wpabuf *ac = wpabuf_alloc(1024), *bc = wpabuf_alloc(1024);
    assert(ac && bc);
    assert(!sae_write_commit(&a, ac, NULL, NULL, 0));
    assert(!sae_write_commit(&b, bc, NULL, NULL, 0));
    assert(!sae_parse_commit(&a, wpabuf_head(bc), wpabuf_len(bc), NULL, NULL, groups, 1, NULL));
    assert(!sae_parse_commit(&b, wpabuf_head(ac), wpabuf_len(ac), NULL, NULL, groups, 1, NULL));
    assert(!sae_process_commit(&a) && !sae_process_commit(&b));
    assert((!memcmp(a.pmk, b.pmk, 32)) == success);
    struct wpabuf *confirm = wpabuf_alloc(128); assert(confirm);
    assert(!sae_write_confirm(&a, confirm));
    assert((sae_check_confirm(&b, wpabuf_head(confirm), wpabuf_len(confirm), NULL) == 0) == success);
    if (success) {
        ((u8 *)wpabuf_mhead(confirm))[wpabuf_len(confirm) - 1] ^= 1;
        assert(sae_check_confirm(&b, wpabuf_head(confirm), wpabuf_len(confirm), NULL));
    }
    wpabuf_clear_free(confirm); wpabuf_clear_free(ac); wpabuf_clear_free(bc);
    sae_clear_data(&a); sae_clear_data(&b); sae_deinit_pt(at); sae_deinit_pt(bt);
}
int main(void)
{
    extern int wpa_debug_level; wpa_debug_level = 0;
    hashes(); ecdh(); sae_pair("123456", 1); sae_pair("654321", 0);
    puts("PASS: SHA/HMAC vectors, P-256 ECDH, invalid points, SAE H2E, wrong PIN and modified confirmation");
    return 0;
}
