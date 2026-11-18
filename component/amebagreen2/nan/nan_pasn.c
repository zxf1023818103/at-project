#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/nan_defs.h"
#include "common/sae.h"
#include "common/wpa_common.h"
#include "crypto/crypto.h"
#include "crypto/sha256.h"
#include "pasn/pasn_common.h"

#define PASN_START_LEN 645
#define PASN_RX_LEN 598
#define AUTH_MAX 512
#define ATTR_MAX 64
#define PASS_MAX 16
#define REALTEK_OUI 0x00e04c
#define NAN_SUBCMD_PASN_SET_KEY 0x1909
#define PAIRING_SETUP 1
#define PAIRING_VERIFICATION 2
#include "nan_pasn.h"
#ifdef AT_NAN_BLE_OOB
#include "nan_oob.h"
#endif
extern int green2_nan_mgmt(const unsigned char *, size_t);
extern int green2_nan_vendor(unsigned short, const void *, size_t);
extern int green2_nan_store(void *, size_t, int);
extern void green2_nan_authenticated(const unsigned char *, unsigned char);
extern void green2_nan_heap_check(const char *);


struct pasn_start_event {
	u8 deinit, role, pairing_type, peer[6], bssid[6];
	u8 auth_method, nik_caching, pass_len, pass[PASS_MAX];
	u16 frame_len;
	u8 frame[AUTH_MAX];
	u8 attrs_len, attrs[ATTR_MAX], own_pmkid[16], peer_pmkid[16];
} __attribute__((packed));

struct pasn_rx_event {
	u8 seq, pairing_type, pass_len, pass[PASS_MAX];
	u16 frame_len;
	u8 frame[AUTH_MAX];
	u8 attrs_len, attrs[ATTR_MAX];
} __attribute__((packed));

/* The shipped radio ABI includes prev_pmkid. The public vendor header's
 * 25-byte version omits it and puts auth_method at the wrong offset. */
struct pasn_pmkid_event {
	u8 role, status, peer[6], pmkid[16], prev_pmkid[16], auth_method;
} __attribute__((packed));

struct nik_cache_entry {
	u32 lifetime, start_ms;
	u8 role, auth, peer_inst, nik[16], pmkid[16], pass_len, pass[16];
};

struct nik_cache_file {
	u8 version, reserved[3], own_nik[16];
	struct nik_cache_entry entries[8];
};

_Static_assert(sizeof(struct pasn_start_event) == PASN_START_LEN,
	       "PASN start event ABI changed");
_Static_assert(sizeof(struct pasn_rx_event) == PASN_RX_LEN,
	       "PASN rx event ABI changed");
_Static_assert(sizeof(struct pasn_pmkid_event) == 41,
	       "PASN PMKID event ABI changed");
_Static_assert(sizeof(struct nik_cache_file) == 500,
	       "Green2 NIK cache ABI changed");

struct bridge {
	unsigned int freq;
	struct rsn_pmksa_cache *pmksa;
	struct pasn_data *pasn;
	struct sae_pt *pt;
	u8 own[6], peer[6], bssid[6];
	u8 pairing_type;
	u8 verified_peer_pmkid[16];
	bool peer_pmkid_valid;
	unsigned int auth2_sent;
	u8 auth1_frame[AUTH_MAX], auth2_frame[AUTH_MAX];
	size_t auth1_len, auth2_len;
	struct os_reltime auth1_started;
	bool keys_installed;
	u8 setup_password_hash[32];
	u8 setup_peer_pmkid[16];
	bool setup_peer_pmkid_valid;
#ifdef AT_NAN_BLE_OOB
	uint32_t oob_generation;
#endif
};

static struct bridge state;
struct pairing_storage {
    u8 magic[4];
    struct nik_cache_file nik;
    struct { u8 nik[16], npk[32]; } keys[8];
};
static struct pairing_storage persistent;
static u8 pending_npk[32];
static bool pending_npk_valid;
static int store_npk(struct bridge *b)
{
    if (pasn_get_pmk_len(b->pasn) != sizeof(pending_npk)) return -1;
    memcpy(pending_npk, pasn_get_pmk(b->pasn), sizeof(pending_npk));
    pending_npk_valid = true;
    return 0;
}

static int prepare_verification(struct bridge *b, const u8 *frame,
				 size_t frame_len, const u8 own_pmkid[16])
{
	struct nik_cache_file cache = {0};
	struct wpa_ie_data rsn = {0};
	u8 record[4 + 32] = {0}, message[3 + 6 + 8], digest[32];
	const u8 *nonce = NULL, *tag = NULL;
	bool rsn_seen = false;
	size_t pos = 30;
	int i, match = -1, ret = -1;
	const char *failure = "malformed verification frame";

	if (frame_len < pos)
		goto done;
	while (pos < frame_len) {
		if (frame_len - pos < 2) goto done;
		u8 id = frame[pos], len = frame[pos + 1];
		const u8 *value = frame + pos + 2;
		size_t end = pos + 2 + len;
		if (end > frame_len) goto done;
		if (id == WLAN_EID_RSN) {
			if (rsn_seen || wpa_parse_wpa_ie_rsn(frame + pos, 2 + len, &rsn)) goto done;
			rsn_seen = true;
		}
		if (id == WLAN_EID_VENDOR_SPECIFIC && len >= 4 &&
		    !memcmp(value, "\x50\x6f\x9a\x13", 4)) {
			size_t ap = 4;
			while (ap < len) {
				if (len - ap < 3) goto done;
				u8 aid = value[ap];
				u16 alen = value[ap + 1] | (value[ap + 2] << 8);
				ap += 3;
				if (ap + alen > len) goto done;
				if (aid == NAN_ATTR_NIRA) {
					if (nonce || alen != 17 || value[ap] != NAN_NIRA_CIPHER_VER_128) goto done;
					nonce = value + ap + 1;
					tag = value + ap + 9;
				}
				ap += alen;
			}
		}
		pos = end;
	}
	if (!nonce || !tag || rsn.num_pmkid != 1 || !rsn.pmkid ||
	    memcmp(rsn.pmkid, nonce, 8) || memcmp(rsn.pmkid + 8, tag, 8))
		goto done;
    memcpy(&cache, &persistent.nik, sizeof(cache));
    if (cache.version != 1) goto done;
	memcpy(message, "NIR", 3);
	memcpy(message + 3, b->peer, 6);
	memcpy(message + 9, nonce, 8);
	for (i = 0; i < 8; i++) {
		if (cache.entries[i].auth != 1 ||
		    (cache.entries[i].role != 1 && cache.entries[i].role != 2)) continue;
		if (hmac_sha256(cache.entries[i].nik, 16,
				message, sizeof(message), digest) == 0 &&
		    os_memcmp_const(digest, tag, 8) == 0) {
			match = i;
			break;
		}
	}
	failure = "NIRA does not match cached identity";
	if (match < 0) goto done;
    failure = "no NPK bound to authenticated NIK";
    for (i = 0; i < 8; ++i) {
        if (!os_memcmp_const(persistent.keys[i].nik, cache.entries[match].nik, 16)) {
            memcpy(record + 4, persistent.keys[i].npk, 32);
            break;
        }
    }
    if (i == 8) goto done;
	failure = "PMKSA insertion failed";
	if (pasn_responder_pmksa_cache_add(b->pmksa, b->own, b->peer,
				    record + 4, 32, rsn.pmkid,
				    WPA_KEY_MGMT_SAE))
		goto done;
	pasn_set_custom_pmkid(b->pasn, own_pmkid);
	memcpy(b->verified_peer_pmkid, rsn.pmkid,
	       sizeof(b->verified_peer_pmkid));
	b->peer_pmkid_valid = true;
	printf("Validated NIRA and loaded NPK for verification peer="
	       MACSTR "\n", MAC2STR(b->peer));
	fflush(stdout);
	ret = 0;
done:
	if (ret) fprintf(stderr, "Verification rejected: %s\n", failure);
	forced_memzero(&cache, sizeof(cache));
	forced_memzero(record, sizeof(record));
	forced_memzero(digest, sizeof(digest));
	return ret;
}

/* hostap requires this callback whenever a custom outgoing PMKID is set.
 * Accept only the incoming PMKID whose NIRA was authenticated for this peer
 * by prepare_verification(); never accept an arbitrary cached-key request.
 */
static int validate_custom_pmkid(void *ctx, const u8 *addr, const u8 *pmkid)
{
	struct bridge *b = ctx;

	if (!addr || !pmkid || b->pairing_type != PAIRING_VERIFICATION ||
	    !b->peer_pmkid_valid || memcmp(addr, b->peer, sizeof(b->peer)))
		return -1;
	return os_memcmp_const(pmkid, b->verified_peer_pmkid,
			      sizeof(b->verified_peer_pmkid)) ? -1 : 0;
}

/* hostap's responder PMKSA code references AP hooks; no AP/RADIUS runs here. */
struct hostapd_data;
struct sta_info;
struct vlan_description;
struct radius_class_data;
int ap_sta_set_vlan(struct hostapd_data *hapd, struct sta_info *sta,
		    struct vlan_description *desc)
{
	(void) hapd; (void) sta; (void) desc;
	return -1;
}
void radius_free_class(struct radius_class_data *data)
{
	(void) data;
}
int radius_copy_class(struct radius_class_data *dst,
		      const struct radius_class_data *src)
{
	(void) dst; (void) src;
	return -1;
}

static int send_mgmt(void *ctx, const u8 *frame, size_t len, int noack,
		     unsigned int freq, unsigned int wait)
{
	struct bridge *b = ctx;
	int ret;

	(void) noack; (void) freq; (void) wait;
	if (len < 30 || len > AUTH_MAX || frame[0] != 0xb0 || frame[1] != 0 ||
	    frame[24] != 7 || frame[26] != 2 || frame[28] != 0 ||
	    frame[29] != 0 || memcmp(frame + 4, b->peer, 6) ||
	    memcmp(frame + 10, b->own, 6)) {
		fprintf(stderr, "refusing invalid Auth2\n");
		return -1;
	}
    ret = green2_nan_mgmt(frame, len);
    if (ret < 0) return ret;
	b->auth2_sent++;
	if (frame != b->auth2_frame)
		memcpy(b->auth2_frame, frame, len);
	b->auth2_len = len;
	printf("Auth2 submitted: %zu bytes, peer=" MACSTR "\n",
	       len, MAC2STR(b->peer));
	fflush(stdout);
	return 0;
}

/* Firmware ABI recovered from rtw_cfgvendor_nan_pasn_setkeys in the matching
 * Green2 lib_wifi_whc_dev.a: TK[16] | KDK[32] | peer NMI[6] | ND-PMK[32].
 * rtw_nan_kdk_to_kek and rtw_nan_kdk_to_pmk consume the field at offset 16.
 */
static int install_keys(struct bridge *b)
{
	u8 payload[16 + 32 + 6 + 32] = {0};
	u8 nmi_pair[12];
	int ret = -1;

	if (b->pasn->ptk.tk_len != 16 || b->pasn->ptk.kdk_len != 32)
		return -1;
	memcpy(payload, b->pasn->ptk.tk, 16);
	memcpy(payload + 16, b->pasn->ptk.kdk, 32);
	memcpy(nmi_pair, b->peer, 6); /* iPhone is pairing initiator */
	memcpy(nmi_pair + 6, b->own, 6);
	if (sha256_prf(b->pasn->ptk.kdk, 32, "NDP PMK Derivation",
		       nmi_pair, sizeof(nmi_pair), payload + 54, 32))
		goto done;
	memcpy(payload + 48, b->peer, 6);
    ret = green2_nan_vendor(NAN_SUBCMD_PASN_SET_KEY, payload, sizeof(payload));
done:
	forced_memzero(payload, sizeof(payload));
	forced_memzero(nmi_pair, sizeof(nmi_pair));
	return ret < 0 ? -1 : 0;
}

static void reset_session(struct bridge *b)
{
	forced_memzero(pending_npk, sizeof(pending_npk));
	pending_npk_valid = false;
	if (b->pasn) {
        /* pasn_data_deinit only frees the outer configuration. The handshake
         * owns ECDH/SAE objects and the PT passed to pasn_set_pt(). */
        wpa_pasn_reset(b->pasn);
        b->pt = NULL;
        pasn_data_deinit(b->pasn);
    }
	if (b->pt) sae_deinit_pt(b->pt);
	/* Only one live handshake is supported. Persistent identities are reloaded
	 * explicitly after authenticating NIRA, so old PMKSAs need not accumulate. */
	if (b->pmksa) pasn_responder_pmksa_cache_flush(b->pmksa);
	b->pasn = NULL;
	b->pt = NULL;
	b->auth2_sent = 0;
	b->auth1_len = b->auth2_len = 0;
	b->keys_installed = false;
	forced_memzero(b->setup_password_hash, sizeof(b->setup_password_hash));
	forced_memzero(b->setup_peer_pmkid, sizeof(b->setup_peer_pmkid));
	b->setup_peer_pmkid_valid = false;
	memset(b->auth1_frame, 0, sizeof(b->auth1_frame));
	memset(b->auth2_frame, 0, sizeof(b->auth2_frame));
	b->pairing_type = 0;
	b->peer_pmkid_valid = false;
	memset(b->verified_peer_pmkid, 0, sizeof(b->verified_peer_pmkid));
	memset(b->own, 0, sizeof(b->own));
	memset(b->peer, 0, sizeof(b->peer));
	memset(b->bssid, 0, sizeof(b->bssid));
}

/* After a previous verification, Green2 may label a new PIN Auth1 as
 * verification. Only recognize an unambiguous SAE setup on the wire: no
 * cached PMKID/NIRA, SAE commit, matching PASN parameters and bootstrap IE.
 * This selects the handshake; libpasn still authenticates the PIN and MIC.
 */
static bool auth1_is_sae_setup(const u8 *frame, size_t len,
			       const u8 **npba, size_t *npba_len)
{
	bool rsn_seen = false, wrapped_seen = false, params_seen = false;
	bool bootstrap_seen = false;
	size_t pos = offsetof(struct ieee80211_mgmt, u.auth.variable);
	struct wpa_ie_data rsn;
	struct wpa_pasn_params_data params;

	if (!frame || len < pos)
		return false;
	while (pos < len) {
		const u8 *ie = frame + pos;
		const u8 *data = ie + 2;
		size_t n;
		if (len - pos < 2 || (n = ie[1]) > len - pos - 2)
			return false;
		if (ie[0] == WLAN_EID_RSN) {
			if (rsn_seen || wpa_parse_wpa_ie_rsn(ie, n + 2, &rsn) ||
			    rsn.key_mgmt != WPA_KEY_MGMT_SAE || rsn.num_pmkid ||
			    rsn.pairwise_cipher != WPA_CIPHER_CCMP)
				return false;
			rsn_seen = true;
		} else if (ie[0] == WLAN_EID_EXTENSION && n) {
			if (data[0] == WLAN_EID_EXT_WRAPPED_DATA) {
				if (wrapped_seen || n < 9 ||
				    WPA_GET_LE16(data + 1) != WLAN_AUTH_SAE ||
				    WPA_GET_LE16(data + 3) != 1 ||
				    WPA_GET_LE16(data + 5) != WLAN_STATUS_SAE_HASH_TO_ELEMENT ||
				    WPA_GET_LE16(data + 7) != 19)
					return false;
				wrapped_seen = true;
			} else if (data[0] == WLAN_EID_EXT_PASN_PARAMS) {
				if (params_seen || n > 253 ||
				    wpa_pasn_parse_parameter_ie(ie, n + 2, false, &params) ||
				    params.wrapped_data_format != WPA_PASN_WRAPPED_DATA_SAE ||
				    params.group != 19 || !params.pubkey_len)
					return false;
				params_seen = true;
			}
		} else if (ie[0] == WLAN_EID_VENDOR_SPECIFIC && n >= 4 &&
			   !memcmp(data, "\x50\x6f\x9a\x13", 4)) {
			size_t a = 4;
			while (a < n) {
				u16 size;
				if (n - a < 3 || (size = WPA_GET_LE16(data + a + 1)) > n - a - 3)
					return false;
				if (data[a] == NAN_ATTR_NIRA)
					return false;
				if (data[a] == NAN_ATTR_NPBA) {
					if (bootstrap_seen || size < 5)
						return false;
					bootstrap_seen = true;
					if (npba) *npba = data + a;
					if (npba_len) *npba_len = 3 + size;
				}
				a += 3 + size;
			}
		}
		pos += 2 + n;
	}
	return rsn_seen && wrapped_seen && params_seen && bootstrap_seen;
}

/* The stale firmware state affects the Auth2 attributes as well as the event
 * type. Keep its local CSIA (including publish IDs), replace verification NIRA
 * with local DCEA and echo the setup NPBA. hostap's nan_bootstrap_rx_request()
 * saves the initiator's NPBA for nan_pairing_prepare_pasn_elems() in this role.
 * Build separately so a malformed/oversized event is never partially changed.
 */
static int correct_setup_event(struct pasn_start_event *e)
{
	u8 attrs[ATTR_MAX], dcea[] = { NAN_ATTR_DCEA, 2, 0, 0,
		NAN_DEV_CAPA_EXT_INFO_1_PAIRING_SETUP };
	const u8 *npba = NULL;
	size_t npba_len = 0, pos = 0, used = 0;
	bool csia_seen = false, dcea_seen = false;

	if (e->frame_len > AUTH_MAX || e->attrs_len > ATTR_MAX)
		return -1;
	if (e->pairing_type != PAIRING_VERIFICATION ||
	    !auth1_is_sae_setup(e->frame, e->frame_len, &npba, &npba_len))
		return 0;
	if (e->nik_caching)
		dcea[4] |= NAN_DEV_CAPA_EXT_INFO_1_NPK_NIK_CACHING;
	while (pos < e->attrs_len) {
		const u8 *a = e->attrs + pos;
		size_t n;
		if (e->attrs_len - pos < 3 ||
		    (n = WPA_GET_LE16(a + 1)) > e->attrs_len - pos - 3)
			return -1;
		if (a[0] == NAN_ATTR_CSIA) {
			if (csia_seen || n < 3 || !(n & 1))
				return -1;
			csia_seen = true;
		}
		if (a[0] == NAN_ATTR_DCEA) {
			if (dcea_seen || n != 2)
				return -1;
			dcea_seen = true;
			dcea[3] = a[3]; /* local regulatory capability, if supplied */
		} else if (a[0] != NAN_ATTR_NIRA && a[0] != NAN_ATTR_NPBA) {
			memcpy(attrs + used, a, n + 3);
			used += n + 3; /* subset of the already bounded event */
		}
		pos += n + 3;
	}
	if (!csia_seen || sizeof(dcea) + npba_len > sizeof(attrs) - used)
		return -1;
	memcpy(attrs + used, dcea, sizeof(dcea));
	used += sizeof(dcea);
	memcpy(attrs + used, npba, npba_len);
	used += npba_len;
	memcpy(e->attrs, attrs, used);
	e->attrs_len = used;
	e->pairing_type = PAIRING_SETUP;
	return 1;
}

static bool auth1_is_retry(const struct bridge *b,
			   const struct pasn_start_event *e)
{
	struct os_reltime now;
	if (!b->pasn || !b->auth2_len || b->auth1_len != e->frame_len ||
	    e->frame_len < 30 || e->frame_len > AUTH_MAX ||
	    b->pairing_type != e->pairing_type ||
	    memcmp(b->auth1_frame + 4, e->frame + 4, 18) ||
	    memcmp(b->auth1_frame + 24, e->frame + 24, e->frame_len - 24) ||
	    os_get_reltime(&now))
		return false;
	/* Ignore only MAC retry/sequence/duration changes. A changed SAE commit,
	 * PASN public key, bootstrap token or peer always starts a new exchange. */
	return now.sec >= b->auth1_started.sec &&
	       now.sec - b->auth1_started.sec < 10;
}

static int on_start(struct bridge *b, const void *data, size_t len)
{
	struct pasn_start_event e;
	const struct ieee80211_mgmt *auth;
	char password[PASS_MAX + 1] = {0};
	u8 extra[2 + 4 + ATTR_MAX];
	u8 rsnxe[4];
	int ret = -1;

	if (!data || len != sizeof(e)) return -1;
    memcpy(&e, data, sizeof(e));
	if (e.deinit || e.role != 2 || e.auth_method != 1 ||
#ifndef AT_NAN_BLE_OOB
        !e.pass_len ||
#endif
	    e.pass_len > PASS_MAX ||
	    e.frame_len < offsetof(struct ieee80211_mgmt, u.auth.variable) ||
	    e.frame_len > AUTH_MAX || e.attrs_len > ATTR_MAX)
		goto done;
	auth = (const struct ieee80211_mgmt *) e.frame;
	if (memcmp(auth->sa, e.peer, 6) ||
	    memcmp(auth->bssid, e.bssid, 6) ||
	    le_to_host16(auth->u.auth.auth_alg) != WLAN_AUTH_PASN ||
	    le_to_host16(auth->u.auth.auth_transaction) != 1)
		goto done;
	ret = correct_setup_event(&e);
	if (ret < 0) goto done;
	if (ret > 0)
		fprintf(stderr, "Corrected stale verification event and Auth2 attributes to SAE setup\n");
	if (auth1_is_retry(b, &e)
#ifdef AT_NAN_BLE_OOB
        && (b->keys_installed || b->pairing_type != PAIRING_SETUP ||
            green2_nan_oob_valid(b->oob_generation))
#endif
    ) {
		printf("Auth1 retransmission: retaining PASN handshake%s\n",
		       b->keys_installed ? " (already completed)" : ", resending Auth2");
		ret = b->keys_installed ? 0 :
			send_mgmt(b, b->auth2_frame, b->auth2_len, 0, b->freq, 0);
		goto done;
	}
	reset_session(b);
	ret = -1;
	if (os_get_reltime(&b->auth1_started))
		goto done;
	b->auth1_len = e.frame_len;
	memcpy(b->auth1_frame, e.frame, e.frame_len);
	memcpy(b->own, auth->da, 6);
	memcpy(b->peer, e.peer, 6);
	memcpy(b->bssid, e.bssid, 6);
	b->pairing_type = e.pairing_type;
#ifdef AT_NAN_BLE_OOB
    if (e.pairing_type == PAIRING_SETUP) {
        if (!green2_nan_oob_password(password, &b->oob_generation)) {
            printf("[NAN-BLE] setup rejected: no active BLE pairing window\n");
            goto done;
        }
        e.pass_len = 6;
        /* A rediscovered peer may have inherited its previous password from
         * the NP cache. Update that peer too, so its authenticated NIK record
         * carries the same credential used by the AP's SAE calculation. */
        struct { u8 peer[6]; char pass[17]; u8 length; } pw = {0};
        _Static_assert(sizeof(pw) == 24, "vendor set-password ABI");
        memcpy(pw.peer, b->peer, 6); memcpy(pw.pass, password, 6); pw.length = 6;
        int pw_rc = green2_nan_vendor(0x1907, &pw, sizeof(pw));
        forced_memzero(&pw, sizeof(pw));
        if (pw_rc) goto done;
    } else {
        memcpy(password, e.pass, e.pass_len);
    }
#else
    memcpy(password, e.pass, e.pass_len);
#endif
	b->pasn = pasn_data_init();
	if (!b->pasn) goto done;
	pasn_set_responder_pmksa(b->pasn, b->pmksa);
	pasn_set_own_addr(b->pasn, b->own);
	pasn_set_peer_addr(b->pasn, b->peer);
	pasn_set_bssid(b->pasn, b->bssid);
	b->pasn->group = 19;
	b->pasn->pasn_groups = calloc(2, sizeof(int));
	if (!b->pasn->pasn_groups) goto done;
	b->pasn->pasn_groups[0] = 19;
	pasn_set_cipher(b->pasn, WPA_CIPHER_CCMP);
	pasn_set_akmp(b->pasn, WPA_KEY_MGMT_SAE);
	pasn_set_rsn_pairwise(b->pasn, WPA_CIPHER_CCMP);
	pasn_set_wpa_key_mgmt(b->pasn, WPA_KEY_MGMT_SAE);
	pasn_enable_kdk_derivation(b->pasn);
	if (e.pairing_type == PAIRING_SETUP) {
		const u8 *parts[] = { &e.pass_len, (const u8 *)password };
		size_t lengths[] = { 1, e.pass_len };
		if (sha256_vector(2, parts, lengths, b->setup_password_hash)) goto done;
		b->pt = sae_derive_pt(b->pasn->pasn_groups,
				      (const u8 *) NAN_PASN_SSID,
				      strlen(NAN_PASN_SSID),
				      (const u8 *) password, e.pass_len,
				      NULL, 0);
		if (!b->pt || pasn_set_pt(b->pasn, b->pt)) goto done;
	} else if (e.pairing_type == PAIRING_VERIFICATION) {
		if (prepare_verification(b, e.frame, e.frame_len,
					 e.own_pmkid)) {
			fprintf(stderr, "Verification NIRA/NPK unavailable\n");
			goto done;
		}
	} else {
		goto done;
	}
	rsnxe[0] = WLAN_EID_RSNX;
	rsnxe[1] = 2;
	rsnxe[2] = 1 | (1 << WLAN_RSNX_CAPAB_SAE_H2E);
	rsnxe[3] = 0;
	pasn_set_rsnxe_ie(b->pasn, rsnxe);
	extra[0] = WLAN_EID_VENDOR_SPECIFIC;
	extra[1] = 4 + e.attrs_len;
	extra[2] = 0x50; extra[3] = 0x6f;
	extra[4] = 0x9a; extra[5] = 0x13;
	memcpy(extra + 6, e.attrs, e.attrs_len);
	if (pasn_set_extra_ies(b->pasn, extra, 6 + e.attrs_len)) goto done;
	pasn_register_callbacks(b->pasn, b, send_mgmt, validate_custom_pmkid,
				NULL, NULL);
	ret = handle_auth_pasn_1(b->pasn, b->own, b->peer, auth,
				 e.frame_len, false);
	if (!b->auth2_sent) ret = -1;
done:
	forced_memzero(password, sizeof(password));
	forced_memzero(&e, sizeof(e));
	if (ret) {
		fprintf(stderr, "Auth1 processing failed\n");
		reset_session(b);
	}
	return ret;
}

static int on_rx(struct bridge *b, const void *data, size_t len)
{
	struct pasn_rx_event e;
	const struct ieee80211_mgmt *auth;
	int ret;

	if (!b->pasn || !b->auth2_sent || !data || len != sizeof(e)) return -1;
    memcpy(&e, data, sizeof(e));
	if (e.seq != 3 || e.frame_len < 30 || e.frame_len > AUTH_MAX)
		return -1;
	auth = (const struct ieee80211_mgmt *) e.frame;
	if (memcmp(auth->da, b->own, 6) || memcmp(auth->sa, b->peer, 6) ||
	    memcmp(auth->bssid, b->bssid, 6) ||
	    le_to_host16(auth->u.auth.auth_alg) != WLAN_AUTH_PASN ||
	    le_to_host16(auth->u.auth.auth_transaction) != 3)
		return -1;
	if (b->keys_installed) {
		printf("Ignoring Auth3 after keys already installed\n");
		memset(&e, 0, sizeof(e));
		return 0;
	}
#ifdef AT_NAN_BLE_OOB
    if (b->pairing_type == PAIRING_SETUP && !green2_nan_oob_valid(b->oob_generation)) {
        printf("[NAN-BLE] Auth3 rejected: pairing window expired or replaced\n");
        forced_memzero(&e, sizeof(e));
        reset_session(b);
        return -1;
    }
#endif
	ret = handle_auth_pasn_3(b->pasn, b->own, b->peer, auth, e.frame_len);
    green2_nan_heap_check("Auth3 crypto complete");
	printf("Auth3 verification %s: peer=" MACSTR ", TK=%zu, KDK=%zu\n",
	       ret ? "FAILED" : "OK", MAC2STR(b->peer),
	       b->pasn->ptk.tk_len, b->pasn->ptk.kdk_len);
	fflush(stdout);
	if (!ret)
		ret = install_keys(b);
    green2_nan_heap_check("key installation complete");
	if (!ret)
		b->keys_installed = true;
	if (!ret && b->pairing_type == PAIRING_SETUP) {
		if (store_npk(b))
			fprintf(stderr, "Could not retain NPK for pairing verification\n");
		else
			printf("NPK retained for pairing verification\n");
		fflush(stdout);
	}
	if (!ret) green2_nan_authenticated(b->peer, b->pairing_type);
	forced_memzero(&e, sizeof(e));
	return ret;
}


int green2_pasn_init(void)
{
    if (!state.pmksa) state.pmksa = pasn_responder_pmksa_cache_init();
    if (!state.pmksa) return -1;
    if (green2_nan_store(&persistent, sizeof(persistent), 0) ||
        memcmp(persistent.magic, "GNP1", 4) || persistent.nik.version != 1) {
        memset(&persistent, 0, sizeof(persistent));
        memcpy(persistent.magic, "GNP1", 4);
        persistent.nik.version = 1;
        if (os_get_random(persistent.nik.own_nik, 16)) return -1;
        if (green2_nan_store(&persistent, sizeof(persistent), 1)) return -1;
    }
    return green2_nan_vendor(0x190a, &persistent.nik, sizeof(persistent.nik));
}
static int on_pmkid_update(struct bridge *b, const void *data, size_t len)
{
    struct pasn_pmkid_event e;
    if (!data || len != sizeof(e)) return -1;
    if (!pending_npk_valid || !b->keys_installed || b->pairing_type != PAIRING_SETUP)
        return 0;
    memcpy(&e, data, sizeof(e));
    /* status=1 is the radio's success value. Only a completed SAE setup may
     * nominate an identity for its pending NPK; unrelated peers cannot change
     * that nomination. The cache handler also verifies the NIRA with its NIK. */
    int rc = -1;
    if (e.role == 2 && e.status == 1 && e.auth_method == 1 &&
        !memcmp(e.peer, b->peer, sizeof(b->peer))) {
        memcpy(b->setup_peer_pmkid, e.pmkid, sizeof(b->setup_peer_pmkid));
        b->setup_peer_pmkid_valid = true;
        rc = 0;
    }
    forced_memzero(&e, sizeof(e));
    return rc;
}
int green2_pasn_event(unsigned short id, const void *data, size_t len)
{
    if (id == 29) return on_start(&state, data, len);
    if (id == 30) return on_rx(&state, data, len);
    if (id == 31) return on_pmkid_update(&state, data, len);
    return -1;
}
int green2_pasn_peer_authorized(const unsigned char peer[6])
{
    return peer && state.keys_installed && !memcmp(peer, state.peer, 6);
}
int green2_pasn_cache_update(const void *data, size_t len, unsigned char peer_instance)
{
    struct nik_cache_file next;
    if (!data || len != sizeof(next)) return -1;
    memcpy(&next, data, len);
    if (next.version != 1 || memcmp(next.own_nik, persistent.nik.own_nik, 16)) {
        printf("[NAN] cache rejected: version=%u own_identity_changed=%u\n", next.version,
               !!memcmp(next.own_nik, persistent.nik.own_nik, 16));
        forced_memzero(&next, sizeof(next));
        return -1;
    }
    if (pending_npk_valid && !state.setup_peer_pmkid_valid) {
        printf("[NAN] cache deferred: waiting for authenticated peer PMKID\n");
        forced_memzero(&next, sizeof(next));
        return -1;
    }
    int index = -1;
    for (unsigned i = 0; i < 8; ++i) {
        if (next.entries[i].pass_len > PASS_MAX) {
            printf("[NAN] cache rejected: invalid password length entry=%u\n", i);
            forced_memzero(&next, sizeof(next));
            return -1;
        }
        /* Lifetime zero is unlimited, as used by iOS. auth/role mark occupancy. */
        if (pending_npk_valid && state.keys_installed &&
            next.entries[i].auth == 1 && next.entries[i].role == 2 &&
            next.entries[i].peer_inst == peer_instance) {
            /* Instances and passwords can both be reused. The PMKID update
             * names the authenticated peer's NIRA (nonce || HMAC tag), which
             * must match this cache entry and validate with this peer's NIK. */
            if (os_memcmp_const(next.entries[i].pmkid, state.setup_peer_pmkid, 16))
                continue;
            u8 message[3 + 6 + 8];
            u8 digest[32];
            memcpy(message, "NIR", 3);
            memcpy(message + 3, state.peer, 6);
            memcpy(message + 9, state.setup_peer_pmkid, 8);
            int identity_matches = !hmac_sha256(next.entries[i].nik, 16,
                message, sizeof(message), digest) &&
                !os_memcmp_const(digest, state.setup_peer_pmkid + 8, 8);
            forced_memzero(digest, sizeof(digest));
            if (!identity_matches) continue;
            const u8 *parts[] = { &next.entries[i].pass_len, next.entries[i].pass };
            size_t lengths[] = { 1, next.entries[i].pass_len };
            int match = !sha256_vector(2, parts, lengths, digest) &&
                !os_memcmp_const(digest, state.setup_password_hash, sizeof(digest));
            forced_memzero(digest, sizeof(digest));
            if (!match) continue;
            if (index >= 0) {
                printf("[NAN] cache rejected: ambiguous authenticated identity instance=%u\n", peer_instance);
                forced_memzero(&next, sizeof(next));
                return -1;
            }
            index = i;
        }
    }
    if (pending_npk_valid && index < 0) {
        printf("[NAN] cache rejected: no matching authenticated identity instance=%u\n", peer_instance);
        forced_memzero(&next, sizeof(next));
        return -1;
    }
    struct pairing_storage candidate = persistent;
    if (index >= 0) {
        memcpy(candidate.keys[index].nik, next.entries[index].nik, 16);
        memcpy(candidate.keys[index].npk, pending_npk, 32);
    }
    memcpy(&candidate.nik, &next, len);
    int rc = green2_nan_store(&candidate, sizeof(candidate), 1);
    if (!rc) memcpy(&persistent, &candidate, sizeof(persistent));
    forced_memzero(&candidate, sizeof(candidate));
    forced_memzero(&next, sizeof(next));
    if (!rc && index >= 0) {
        forced_memzero(pending_npk, sizeof(pending_npk));
        pending_npk_valid = false;
        forced_memzero(state.setup_password_hash, sizeof(state.setup_password_hash));
        forced_memzero(state.setup_peer_pmkid, sizeof(state.setup_peer_pmkid));
        state.setup_peer_pmkid_valid = false;
        printf("[NAN] authenticated pairing persisted slot=%d\n", index);
    }
    return rc;
}
void green2_pasn_stop(void)
{
    reset_session(&state);
    if (state.pmksa) pasn_responder_pmksa_cache_deinit(state.pmksa);
    state.pmksa = NULL;
}
void green2_pasn_pause(void)
{
    reset_session(&state);
}
int green2_pasn_resume(void)
{
    if (!state.pmksa || memcmp(persistent.magic, "GNP1", 4) ||
        persistent.nik.version != 1) return -1;
    /* Radio stop removes peer state. Restore only its identity cache from RAM;
     * a new PASN exchange must authorize the next NDP. No flash reload or init. */
    return green2_nan_vendor(0x190a, &persistent.nik, sizeof(persistent.nik));
}
