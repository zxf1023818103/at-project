/*
 * AmebaD KM4 QUIC LAN speed test — ngtcp2 (v1.23.0) + picotls TLS 1.3 on the upstream
 * mbedTLS/PSA backend (the wolfSSL replacement; component/picotls_config + component/amebad/mbedtls_port).
 *
 * Connects to the host quic_perf server (project/amebad/quic_perf) over the LAN
 * and measures QUIC throughput in both directions using the quic-interop "perf"
 * protocol (ALPN "perf"):
 *   - DOWNLOAD phase: open a bidi stream, send an 8-byte big-endian uint64 =
 *     bytes to receive, then sink the response and time it  -> RX Mbps.
 *   - UPLOAD phase: a fresh connection whose request asks for 0 download bytes;
 *     the chip then streams the upload payload and FINs -> TX Mbps.
 * The two phases run as separate connections so each yields a clean peak
 * unidirectional number (the host server handles one stream per connection).
 *
 *   Host side (see doc/amebad_quic_test.md):
 *     project/amebad/quic_perf/quic_perf server <host-ip> 4433 cert.pem key.pem
 *   (a self-signed cert is fine — the chip runs SSL_VERIFY_NONE.)
 *
 * Structure mirrors ngtcp2's examples/simpleclient.c, with three substitutions:
 *   - crypto backend: quictls  -> picotls/mbedTLS (ngtcp2_crypto_picotls_*)
 *   - socket layer:   POSIX    -> FreeRTOS-Plus-TCP (FreeRTOS_socket/sendto/recvfrom)
 *   - event loop:     libev    -> one task with a recvfrom timeout == ngtcp2 expiry
 *
 * Randomness: ngtcp2 callbacks + picotls use ptls_mbedtls_random_bytes (PSA
 * psa_generate_random over the mbedTLS entropy). The AmebaD entropy source is still
 * the placeholder xorshift (see component/amebad/mbedtls_port) — a real TRNG is a follow-up.
 */
#include <string.h>
#include "at_log.h"
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

/* FreeRTOS-Plus-TCP with ipconfigIPv4_BACKWARD_COMPATIBLE=1 #defines `sin_addr`
 * as `sin_address.ulIP_IPv4`, which would mangle ngtcp2_sockaddr_in's member of
 * the same name. Drop the macro before the ngtcp2 headers and access the
 * freertos_sockaddr's real union member (sin_address.ulIP_IPv4) directly. */
#ifdef sin_addr
#  undef sin_addr
#endif

#include <picotls.h>
#include <picotls/mbedtls.h>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_picotls.h>

#include "psa/crypto.h"           /* psa_crypto_init — PSA backs ptls_mbedtls_random_bytes */
#include "mbedtls/build_info.h"   /* MBEDTLS_SHA384_C — gates the AES-256-GCM-SHA384 suite */

extern void mbedtls_threading_freertos_init(void);   /* MBEDTLS_THREADING_ALT callbacks */
/* The libc/POSIX bits picotls pulls (posix_memalign / aligned_alloc / inet_pton) live in the
 * newlib retarget — component/syscall_config/newlib_syscalls.c — with the malloc family. */

/* ---- test target (the WSL host's ngtcp2 example server) ---- */
#define QUIC_SERVER_A0      192
#define QUIC_SERVER_A1      168
#define QUIC_SERVER_A2      4
#define QUIC_SERVER_A3      250
#define QUIC_SERVER_PORT    4567
#define QUIC_ALPN           "perf"
#define QUIC_HANDSHAKE_TIMEOUT_MS  10000U   /* give up after 10 s */

/* perf transfer sizes per direction. 4 MB ~ a few seconds over WiFi. */
#define QUIC_PERF_XFER_BYTES   (4u * 1024u * 1024u)
#define QUIC_PERF_TIMEOUT_MS   60000U       /* whole-transfer cap           */
/* QUIC flow-control windows. These MUST stay small: ngtcp2 buffers received
 * (and sent-but-unacked) data up to these limits in the FreeRTOS heap, which is
 * only 250 KB on KM4. A 512 KB window exhausted the heap mid-download and the
 * task span forever in vApplicationMallocFailedHook. The LAN BDP is only a few
 * KB (sub-ms RTT), so a 48 KB window costs no throughput. */
#define QUIC_FLOW_WINDOW       (48u * 1024u)
#define QUIC_PERF_SEND_CHUNK   (8u * 1024u)
/* Cap upload bytes written-but-unacked so ngtcp2's retransmit buffer (heap) can
 * not grow past what KM4 RAM allows when the LAN congestion window opens up. */
#define QUIC_UPLOAD_INFLIGHT_CAP (32u * 1024u)

struct quic_client {
    ngtcp2_crypto_conn_ref conn_ref;
    Socket_t sock;
    ngtcp2_crypto_picotls_ctx cptls;   /* ptls_t + handshake_properties (the TLS native handle) */
    ngtcp2_conn *conn;
    ngtcp2_ccerr last_error;
    ngtcp2_sockaddr_in local_addr;
    ngtcp2_sockaddr_in remote_addr;
    struct freertos_sockaddr fr_remote;

    /* ---- perf protocol / measurement state ---- */
    int64_t  stream_id;        /* the perf bidi stream (-1 until opened)     */
    uint64_t want_download;    /* bytes to ask the server to send (RX)       */
    uint64_t want_upload;      /* bytes to stream to the server (TX)         */
    uint64_t send_remaining;   /* upload bytes still to push                 */
    uint64_t acked_total;      /* upload bytes acknowledged by the server    */
    uint64_t recv_total;       /* download bytes received                    */
    int      hdr_sent;         /* 8-byte request header written              */
    int      send_fin_done;    /* our FIN queued                            */
    int      peer_fin;         /* server FIN'd its side                      */
};

static struct quic_client s_client;
static uint8_t s_rxbuf[2048];

/* ngtcp2 wants nanosecond monotonic timestamps. The FreeRTOS tick is 1 ms
 * (configTICK_RATE_HZ == 1000), so ns = ticks * 1e6. ms resolution is plenty
 * for handshake retransmit timers. */
static ngtcp2_tstamp quic_timestamp(void)
{
    return (ngtcp2_tstamp)xTaskGetTickCount() * 1000000ULL;
}

/* ---- ngtcp2 callbacks not provided by the crypto helper ---- */

static void rand_cb(uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *ctx)
{
    (void)ctx;
    ptls_mbedtls_random_bytes(dest, destlen);   /* PSA random (mbedTLS backend) */
}

static int get_new_connection_id_cb(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                    ngtcp2_stateless_reset_token *token,
                                    size_t cidlen, void *user_data)
{
    (void)conn;
    (void)user_data;

    ptls_mbedtls_random_bytes(cid->data, cidlen);
    cid->datalen = cidlen;
    ptls_mbedtls_random_bytes(token->data, sizeof(token->data));
    return 0;
}

static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *conn_ref)
{
    struct quic_client *c = conn_ref->user_data;
    return c->conn;
}

/* ---- perf stream callbacks ---- */

/* Server -> chip download bytes land here; we only count them and keep the
 * receive flow-control window open so the transfer doesn't stall. */
static int recv_stream_data_cb(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id,
                               uint64_t offset, const uint8_t *data, size_t datalen,
                               void *user_data, void *stream_user_data)
{
    struct quic_client *c = user_data;
    (void)offset; (void)data; (void)stream_user_data;

    c->recv_total += datalen;
    ngtcp2_conn_extend_max_stream_offset(conn, stream_id, datalen);
    ngtcp2_conn_extend_max_offset(conn, datalen);

    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        c->peer_fin = 1;
    }
    return 0;
}

static int acked_stream_data_cb(ngtcp2_conn *conn, int64_t stream_id, uint64_t offset,
                                uint64_t datalen, void *user_data, void *stream_user_data)
{
    struct quic_client *c = user_data;
    (void)conn; (void)stream_id; (void)offset; (void)stream_user_data;
    c->acked_total += datalen;   /* upload delivery confirmed (incl. the 8B hdr) */
    return 0;
}

/* ---- picotls client TLS context (mbedTLS/PSA backend) ---- */

/* picotls needs a wall clock for ticket/cert freshness. We do neither (full
 * handshake, no resumption; server cert unverified), so a monotonic ms counter is
 * plenty — avoids needing a real RTC. */
static uint64_t quic_ptls_time(ptls_get_time_t *self)
{
    (void)self;
    return (uint64_t)xTaskGetTickCount();
}
static ptls_get_time_t s_ptls_get_time = { quic_ptls_time };
static ptls_context_t  s_ptls_ctx;

/* Cipher-suite preference: AES-GCM first. The RTL8721D IPSEC engine HW-accelerates
 * AES-GCM (MBEDTLS_GCM_ALT, ~48 MB/s vs ~419 KB/s software — see reference_amebad_hw_crypto
 * / amebad_crypto_mbedtls_vs_wolfssl.md), so we offer AES-GCM ahead of ChaCha20-Poly1305 and
 * put the faster AES-128-GCM first (fewer rounds; what the C# server uses). ChaCha is kept
 * last as a fallback for ChaCha-only peers (also HW-backed via chachapoly_alt). This overrides
 * the picotls default order (ptls_mbedtls_cipher_suites lists AES-256-GCM first). */
static ptls_cipher_suite_t *s_cipher_suites[] = {
    &ptls_mbedtls_aes128gcmsha256,
#if defined(MBEDTLS_SHA384_C)
    &ptls_mbedtls_aes256gcmsha384,
#endif
    &ptls_mbedtls_chacha20poly1305sha256,
    NULL,
};

static int client_ssl_init(struct quic_client *c)
{
    static const ptls_iovec_t alpn = { (uint8_t *)QUIC_ALPN, sizeof(QUIC_ALPN) - 1 };
    /* picotls reads our outgoing TLS extensions from additional_extensions, a
     * UINT16_MAX-terminated array the APP owns. ngtcp2's set_additional_extensions
     * fills [0] with the QUIC transport params (and asserts the array is non-NULL),
     * so we must pre-allocate it + the [1] sentinel. Connections run one at a time, so
     * a static array is safe; the binding frees only [0].data.base in deconfigure. */
    static ptls_raw_extension_t exts[2];

    /* One-time ptls context wired to the mbedTLS/PSA backend (P-256 + X25519 key
     * exchanges, AES-GCM / ChaCha20-Poly1305 cipher suites, PSA random). */
    if (s_ptls_ctx.random_bytes == NULL) {
        s_ptls_ctx.random_bytes  = ptls_mbedtls_random_bytes;
        s_ptls_ctx.get_time      = &s_ptls_get_time;
        s_ptls_ctx.key_exchanges = ptls_mbedtls_key_exchanges;
        s_ptls_ctx.cipher_suites = s_cipher_suites;   /* AES-GCM-first (HW); see s_cipher_suites */
        if (ngtcp2_crypto_picotls_configure_client_context(&s_ptls_ctx) != 0) {
            log_error("[QUIC] configure_client_context failed");
            return -1;
        }
    }

    ngtcp2_crypto_picotls_ctx_init(&c->cptls);
    c->cptls.ptls = ptls_client_new(&s_ptls_ctx);
    if (c->cptls.ptls == NULL) {
        log_error("[QUIC] ptls_client_new failed");
        return -1;
    }

    /* Bind the conn_ref so the binding's get_conn() can reach the ngtcp2_conn
     * (replaces wolfSSL_set_app_data). Smoke test: no verify_certificate set ->
     * server cert accepted unverified. */
    *ptls_get_data_ptr(c->cptls.ptls) = &c->conn_ref;

    /* QUIC mandates ALPN. SNI is informational here. */
    c->cptls.handshake_properties.client.negotiated_protocols.list  = &alpn;
    c->cptls.handshake_properties.client.negotiated_protocols.count = 1;
    ptls_set_server_name(c->cptls.ptls, "amebad-quic", 0);

    exts[0].type = UINT16_MAX;   /* filled with the QUIC transport params by the binding */
    exts[1].type = UINT16_MAX;   /* terminator */
    c->cptls.handshake_properties.additional_extensions = exts;
    /* configure_client_session needs the ngtcp2_conn, so it's deferred to
     * client_quic_init() (after ngtcp2_conn_client_new). */
    return 0;
}

/* ---- ngtcp2 connection ---- */

static int client_quic_init(struct quic_client *c)
{
    ngtcp2_path path = {
        .local  = { .addr = (ngtcp2_sockaddr *)&c->local_addr,
                    .addrlen = sizeof(c->local_addr) },
        .remote = { .addr = (ngtcp2_sockaddr *)&c->remote_addr,
                    .addrlen = sizeof(c->remote_addr) },
    };
    ngtcp2_callbacks callbacks = {
        .client_initial           = ngtcp2_crypto_client_initial_cb,
        .recv_crypto_data         = ngtcp2_crypto_recv_crypto_data_cb,
        .encrypt                  = ngtcp2_crypto_encrypt_cb,
        .decrypt                  = ngtcp2_crypto_decrypt_cb,
        .hp_mask                  = ngtcp2_crypto_hp_mask_cb,
        .recv_stream_data         = recv_stream_data_cb,
        .acked_stream_data_offset = acked_stream_data_cb,
        .recv_retry               = ngtcp2_crypto_recv_retry_cb,
        .rand                     = rand_cb,
        .get_new_connection_id2   = get_new_connection_id_cb,
        .update_key               = ngtcp2_crypto_update_key_cb,
        .delete_crypto_aead_ctx   = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
        .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
        .get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb,
        .version_negotiation      = ngtcp2_crypto_version_negotiation_cb,
    };
    ngtcp2_cid dcid, scid;
    ngtcp2_settings settings;
    ngtcp2_transport_params params;
    int rv;

    dcid.datalen = NGTCP2_MIN_INITIAL_DCIDLEN;
    ptls_mbedtls_random_bytes(dcid.data, dcid.datalen);
    scid.datalen = 8;
    ptls_mbedtls_random_bytes(scid.data, scid.datalen);

    ngtcp2_settings_default(&settings);
    settings.initial_ts = quic_timestamp();
    settings.max_tx_udp_payload_size = 1252;   /* keep packets < client_flush buf */
    /* log_printf left NULL: the ROM DiagPrintf can't handle ngtcp2's width/flag
     * format specifiers safely. */

    ngtcp2_transport_params_default(&params);
    params.initial_max_streams_bidi            = 8;
    params.initial_max_streams_uni             = 3;
    /* bidi_local gates the DOWNLOAD window (server -> chip); the rest are set
     * for completeness. Keep them bounded to limit on-target RAM. */
    params.initial_max_stream_data_bidi_local  = QUIC_FLOW_WINDOW;
    params.initial_max_stream_data_bidi_remote = QUIC_FLOW_WINDOW;
    params.initial_max_stream_data_uni         = QUIC_FLOW_WINDOW;
    params.initial_max_data                    = QUIC_FLOW_WINDOW * 2;

    rv = ngtcp2_conn_client_new(&c->conn, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1,
                                &callbacks, &settings, &params, NULL, c);
    if (rv != 0) {
        log_info("[QUIC] conn_client_new: %s", ngtcp2_strerror(rv));
        return -1;
    }

    ngtcp2_conn_set_tls_native_handle(c->conn, &c->cptls);
    if (ngtcp2_crypto_picotls_configure_client_session(&c->cptls, c->conn) != 0) {
        log_error("[QUIC] configure_client_session failed");
        return -1;
    }
    return 0;
}

/* ---- UDP I/O over FreeRTOS-Plus-TCP ---- */

static int client_send_packet(struct quic_client *c, const uint8_t *data, size_t datalen)
{
    int32_t sent = FreeRTOS_sendto(c->sock, data, datalen, 0,
                                   &c->fr_remote, sizeof(c->fr_remote));
    if (sent != (int32_t)datalen) {
        /* A transiently-full TX path (no network buffer) returns 0 / EWOULDBLOCK.
         * Dropping this datagram is safe — QUIC will retransmit — so don't tear
         * the connection down; just report it didn't go out this time. */
        return 0;
    }
    return 0;
}

static uint8_t s_sendbuf[QUIC_PERF_SEND_CHUNK];   /* zero filler for upload   */

/* Drain everything ngtcp2 wants to send: handshake/ACK frames plus, once the
 * perf stream is open, the 8-byte request header and the upload payload. */
static int client_flush(struct quic_client *c)
{
    uint8_t buf[1400];
    ngtcp2_path_storage ps;
    ngtcp2_pkt_info pi;

    ngtcp2_path_storage_zero(&ps);

    /* Send the 8-byte download-length request header first (no FIN). */
    if (c->stream_id >= 0 && !c->hdr_sent) {
        uint8_t hdr[8];
        ngtcp2_vec v;
        ngtcp2_ssize pd = 0, n;
        int i;
        for (i = 0; i < 8; i++) {
            hdr[i] = (uint8_t)(c->want_download >> (8 * (7 - i)));
        }
        v.base = hdr; v.len = sizeof(hdr);
        n = ngtcp2_conn_writev_stream(c->conn, &ps.path, &pi, buf, sizeof(buf),
                                      &pd, NGTCP2_WRITE_STREAM_FLAG_NONE,
                                      c->stream_id, &v, 1, quic_timestamp());
        if (n < 0) {
            if (n != NGTCP2_ERR_STREAM_DATA_BLOCKED) {
                log_info("[QUIC] hdr writev: %s", ngtcp2_strerror((int)n));
                ngtcp2_ccerr_set_liberr(&c->last_error, (int)n, NULL, 0);
                return -1;
            }
        } else if (pd == (ngtcp2_ssize)sizeof(hdr)) {
            c->hdr_sent = 1;
            c->send_remaining = c->want_upload;
            if (n > 0 && client_send_packet(c, buf, (size_t)n) != 0) return -1;
        } else {
            if (n > 0 && client_send_packet(c, buf, (size_t)n) != 0) return -1;
            return 0;   /* header didn't fit yet; retry next loop */
        }
    }

    for (;;) {
        int64_t sid = -1;
        ngtcp2_vec vec;
        size_t veccnt = 0;
        uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_NONE;
        ngtcp2_ssize pdatalen = 0, nwrite;

        if (c->stream_id >= 0 && c->hdr_sent && !c->send_fin_done) {
            /* Pace: only offer more payload while in-flight (written-but-unacked)
             * stays under the cap, so ngtcp2's retransmit buffer stays bounded.
             * acked_total includes the 8-byte request header. */
            uint64_t written  = c->want_upload - c->send_remaining;
            uint64_t acked    = (c->acked_total > 8) ? (c->acked_total - 8) : 0;
            uint64_t inflight = written - acked;
            if (inflight < QUIC_UPLOAD_INFLIGHT_CAP) {
                size_t chunk = c->send_remaining > sizeof(s_sendbuf) ? sizeof(s_sendbuf)
                                                                     : (size_t)c->send_remaining;
                sid = c->stream_id;
                vec.base = s_sendbuf;
                vec.len  = chunk;
                veccnt   = chunk ? 1 : 0;
                if (c->send_remaining <= chunk) flags = NGTCP2_WRITE_STREAM_FLAG_FIN;
            }
        }

        nwrite = ngtcp2_conn_writev_stream(c->conn, &ps.path, &pi, buf, sizeof(buf),
                                           &pdatalen, flags, sid,
                                           veccnt ? &vec : NULL, veccnt, quic_timestamp());
        if (nwrite < 0) {
            if (nwrite == NGTCP2_ERR_STREAM_DATA_BLOCKED || nwrite == NGTCP2_ERR_STREAM_SHUT_WR) {
                /* stream is flow-control blocked; flush only non-stream frames */
                ngtcp2_ssize m = ngtcp2_conn_writev_stream(c->conn, &ps.path, &pi, buf, sizeof(buf),
                                                           NULL, NGTCP2_WRITE_STREAM_FLAG_NONE,
                                                           -1, NULL, 0, quic_timestamp());
                if (m > 0 && client_send_packet(c, buf, (size_t)m) != 0) return -1;
                return 0;
            }
            log_info("[QUIC] writev_stream: %s (tls_alert=%d)",
                       ngtcp2_strerror((int)nwrite),
                       (int)ngtcp2_conn_get_tls_alert2(c->conn));
            ngtcp2_ccerr_set_liberr(&c->last_error, (int)nwrite, NULL, 0);
            return -1;
        }
        if (pdatalen > 0) c->send_remaining -= (uint64_t)pdatalen;
        if (sid >= 0 && (flags & NGTCP2_WRITE_STREAM_FLAG_FIN) && c->send_remaining == 0) {
            c->send_fin_done = 1;
        }
        if (nwrite == 0) return 0;
        if (client_send_packet(c, buf, (size_t)nwrite) != 0) return -1;
    }
}

static int client_read(struct quic_client *c, const uint8_t *data, size_t datalen)
{
    ngtcp2_path path = {
        .local  = { .addr = (ngtcp2_sockaddr *)&c->local_addr,
                    .addrlen = sizeof(c->local_addr) },
        .remote = { .addr = (ngtcp2_sockaddr *)&c->remote_addr,
                    .addrlen = sizeof(c->remote_addr) },
    };
    ngtcp2_pkt_info pi = {0};
    int rv = ngtcp2_conn_read_pkt(c->conn, &path, &pi, data, datalen, quic_timestamp());
    if (rv != 0) {
        log_info("[QUIC] read_pkt: %s", ngtcp2_strerror(rv));
        if (!c->last_error.error_code) {
            if (rv == NGTCP2_ERR_CRYPTO) {
                ngtcp2_ccerr_set_tls_alert(&c->last_error,
                                           ngtcp2_conn_get_tls_alert2(c->conn), NULL, 0);
            } else {
                ngtcp2_ccerr_set_liberr(&c->last_error, rv, NULL, 0);
            }
        }
        return -1;
    }
    return 0;
}

static int client_socket_init(struct quic_client *c)
{
    struct freertos_sockaddr bind_addr;

    c->sock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP);
    if (c->sock == FREERTOS_INVALID_SOCKET || c->sock == NULL) {
        log_error("[QUIC] socket() failed");
        return -1;
    }

    /* Bind to an ephemeral local port so recvfrom works and we can report the
     * local address into the ngtcp2 path. */
    (void)memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = FREERTOS_AF_INET;
    bind_addr.sin_port = 0;
    if (FreeRTOS_bind(c->sock, &bind_addr, sizeof(bind_addr)) != 0) {
        log_error("[QUIC] bind() failed");
        return -1;
    }

    /* Turn OFF UDP TX checksum for this socket: QUIC packets are AEAD-protected, so the
     * UDP checksum is redundant (the RTL8721D NetworkInterface does no checksum offload).
     * pvOptionValue == NULL clears FREERTOS_SO_UDPCKSUM_OUT; outgoing UDP goes out with
     * checksum 0 (legal in IPv4), the IP header checksum is still computed. NB: not the
     * QUIC blocker on this bench — handshake times out the same with checksum on/off. */
    (void)FreeRTOS_setsockopt(c->sock, 0, FREERTOS_SO_UDPCKSUM_OUT, NULL, 0);

    /* Remote (FreeRTOS form for sendto, ngtcp2 form for the path). */
    (void)memset(&c->fr_remote, 0, sizeof(c->fr_remote));
    c->fr_remote.sin_family = FREERTOS_AF_INET;
    c->fr_remote.sin_port = FreeRTOS_htons(QUIC_SERVER_PORT);
    c->fr_remote.sin_address.ulIP_IPv4 =
        FreeRTOS_inet_addr_quick(QUIC_SERVER_A0, QUIC_SERVER_A1,
                                 QUIC_SERVER_A2, QUIC_SERVER_A3);

    (void)memset(&c->remote_addr, 0, sizeof(c->remote_addr));
    c->remote_addr.sin_family = NGTCP2_AF_INET;
    c->remote_addr.sin_port = FreeRTOS_htons(QUIC_SERVER_PORT);
    c->remote_addr.sin_addr.s_addr = c->fr_remote.sin_address.ulIP_IPv4;

    /* Local address for the path: our IP + the bound ephemeral port. */
    {
        struct freertos_sockaddr local;
        (void)memset(&local, 0, sizeof(local));
        (void)FreeRTOS_GetLocalAddress(c->sock, &local);
        (void)memset(&c->local_addr, 0, sizeof(c->local_addr));
        c->local_addr.sin_family = NGTCP2_AF_INET;
        c->local_addr.sin_port = local.sin_port;          /* already network order */
        c->local_addr.sin_addr.s_addr = FreeRTOS_GetIPAddress();
    }

    return 0;
}

/* Run one perf connection. Exactly one of dl/ul is non-zero; on success
 * *kbps_out is the measured throughput for that direction in kbit/s. */
static int perf_connection(uint64_t dl, uint64_t ul, uint32_t *kbps_out)
{
    struct quic_client *c = &s_client;
    TickType_t hs_deadline, xfer_deadline, t_xfer_start = 0, hb_next = 0;
    int done = 0, failed = 0, stream_open = 0;

    *kbps_out = 0;

    (void)memset(c, 0, sizeof(*c));
    ngtcp2_ccerr_default(&c->last_error);
    c->conn_ref.get_conn  = get_conn;
    c->conn_ref.user_data = c;
    c->stream_id     = -1;
    c->want_download = dl;
    c->want_upload   = ul;

    if (client_socket_init(c) != 0 || client_ssl_init(c) != 0 ||
        client_quic_init(c) != 0) {
        log_error("[QUIC] init failed");
        failed = 1;
        goto cleanup;
    }

    if (client_flush(c) != 0) { failed = 1; goto cleanup; }  /* send Initial */

    hs_deadline   = xTaskGetTickCount() + pdMS_TO_TICKS(QUIC_HANDSHAKE_TIMEOUT_MS);
    xfer_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(QUIC_PERF_TIMEOUT_MS);

    while (!done && !failed) {
        TickType_t now = xTaskGetTickCount();
        ngtcp2_tstamp expiry, ts_now;
        TickType_t wait_ticks;
        int32_t nread;
        struct freertos_sockaddr from;
        socklen_t fromlen = sizeof(from);

        if ((int32_t)(now - hb_next) >= 0) {
            log_info("[QUIC] .hb so=%d rx=%d ack=%d sr=%d fin=%d",
                       stream_open, (int)c->recv_total, (int)c->acked_total,
                       (int)c->send_remaining, c->send_fin_done);
            hb_next = now + pdMS_TO_TICKS(1000);
        }

        if (!stream_open && (int32_t)(now - hs_deadline) >= 0) {
            log_info("[QUIC] handshake TIMEOUT");
            failed = 1; break;
        }
        if ((int32_t)(now - xfer_deadline) >= 0) {
            log_info("[QUIC] transfer TIMEOUT (rx=%d tx-acked=%d)",
                       (unsigned)c->recv_total, (unsigned)c->acked_total);
            failed = 1; break;
        }

        expiry = ngtcp2_conn_get_expiry2(c->conn);
        ts_now = quic_timestamp();
        if (expiry <= ts_now) {
            wait_ticks = 0;
        } else {
            uint64_t ms = (expiry - ts_now) / 1000000ULL;
            if (ms > 50) ms = 50;        /* poll often during bulk transfer */
            wait_ticks = pdMS_TO_TICKS((uint32_t)ms);
        }
        (void)FreeRTOS_setsockopt(c->sock, 0, FREERTOS_SO_RCVTIMEO,
                                  &wait_ticks, sizeof(wait_ticks));

        nread = FreeRTOS_recvfrom(c->sock, s_rxbuf, sizeof(s_rxbuf), 0, &from, &fromlen);
        if (nread > 0) {
            /* Drain EVERY datagram already queued before flushing, so a download
             * flood can't outrun us and exhaust the RX network-buffer pool (which
             * would then wedge sendto on buffer allocation). After the first
             * (blocking) read, switch to non-blocking and keep pulling. */
            TickType_t zero = 0;
            (void)FreeRTOS_setsockopt(c->sock, 0, FREERTOS_SO_RCVTIMEO, &zero, sizeof(zero));
            do {
                if (client_read(c, s_rxbuf, (size_t)nread) != 0) { failed = 1; break; }
                nread = FreeRTOS_recvfrom(c->sock, s_rxbuf, sizeof(s_rxbuf), 0, &from, &fromlen);
            } while (nread > 0);
            if (failed) break;
        } else {
            int rv = ngtcp2_conn_handle_expiry(c->conn, quic_timestamp());
            if (rv != 0) {
                log_info("[QUIC] handle_expiry: %s", ngtcp2_strerror(rv));
                failed = 1; break;
            }
        }

        /* Open the perf stream as soon as the handshake is up. */
        if (!stream_open && ngtcp2_conn_get_handshake_completed(c->conn)) {
            int64_t sid;
            if (ngtcp2_conn_open_bidi_stream(c->conn, &sid, c) == 0) {
                c->stream_id  = sid;
                stream_open   = 1;
                t_xfer_start  = xTaskGetTickCount();
            }
        }

        if (client_flush(c) != 0) { failed = 1; break; }

        if (stream_open) {
            int dl_done = (c->want_download == 0) ||
                          (c->recv_total >= c->want_download) || c->peer_fin;
            int ul_done = (c->want_upload == 0) ||
                          (c->send_fin_done && c->acked_total >= c->want_upload);
            if (dl_done && ul_done) done = 1;
        }
        /* Server tore the connection down (e.g. after sinking our upload). */
        if (ngtcp2_conn_in_closing_period2(c->conn) ||
            ngtcp2_conn_in_draining_period2(c->conn)) {
            done = 1;
        }
    }

    if (done && stream_open) {
        TickType_t ms = xTaskGetTickCount() - t_xfer_start;
        uint64_t bytes = (dl > 0) ? c->recv_total : c->acked_total;
        if (ms == 0) ms = 1;
        *kbps_out = (uint32_t)((bytes * 8ULL) / (uint64_t)ms);
        /* Tidy close so the server logs a clean teardown. */
        if (!ngtcp2_conn_in_closing_period2(c->conn) &&
            !ngtcp2_conn_in_draining_period2(c->conn)) {
            uint8_t buf[1280];
            ngtcp2_path_storage ps;
            ngtcp2_pkt_info pi;
            ngtcp2_ssize n;
            ngtcp2_path_storage_zero(&ps);
            n = ngtcp2_conn_write_connection_close(c->conn, &ps.path, &pi, buf,
                                                   sizeof(buf), &c->last_error,
                                                   quic_timestamp());
            if (n > 0) (void)client_send_packet(c, buf, (size_t)n);
        }
    }

cleanup:
    if (c->conn)       ngtcp2_conn_del(c->conn);
    if (c->cptls.ptls) {
        ngtcp2_crypto_picotls_deconfigure_session(&c->cptls);
        ptls_free(c->cptls.ptls);
    }
    if (c->sock && c->sock != FREERTOS_INVALID_SOCKET) FreeRTOS_closesocket(c->sock);
    return failed ? -1 : 0;
}

static void quic_client_task(void *arg)
{
    uint32_t rx_kbps = 0, tx_kbps = 0;
    int rx_ok, tx_ok;
    (void)arg;

    /* Wait until DHCP has handed us an IP. */
    while (FreeRTOS_GetIPAddress() == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    log_info("[QUIC] ngtcp2 %s + picotls/mbedTLS TLS1.3 perf -> %d.%d.%d.%d:%d (ALPN %s)",
               ngtcp2_version(0)->version_str,
               QUIC_SERVER_A0, QUIC_SERVER_A1, QUIC_SERVER_A2, QUIC_SERVER_A3,
               QUIC_SERVER_PORT, QUIC_ALPN);

    /* mbedTLS runtime: FreeRTOS threading-alt callbacks + PSA (ptls_mbedtls_random_bytes
     * and the whole picotls mbedTLS backend run on PSA). */
    mbedtls_threading_freertos_init();
    if (psa_crypto_init() != PSA_SUCCESS) {   /* PSA backs ptls_mbedtls_* + the whole handshake */
        log_error("[QUIC] psa_crypto_init failed");
        vTaskDelete(NULL);
        return;
    }

    /* Loop the test so the host server can be started at any time and a fresh
     * round will pick it up; also lets the console be read on demand. */
    for (int round = 1; ; round++) {
        log_info("[QUIC] ---- round %d ----", round);

        /* Phase 1: DOWNLOAD (chip RX). */
        log_info("[QUIC] download phase: requesting %d bytes...",
                   (int)QUIC_PERF_XFER_BYTES);
        rx_ok = (perf_connection(QUIC_PERF_XFER_BYTES, 0, &rx_kbps) == 0);

        vTaskDelay(pdMS_TO_TICKS(300));

        /* Phase 2: UPLOAD (chip TX). */
        log_info("[QUIC] upload phase: sending %d bytes...",
                   (int)QUIC_PERF_XFER_BYTES);
        tx_ok = (perf_connection(0, QUIC_PERF_XFER_BYTES, &tx_kbps) == 0);

        log_info("[QUIC] ============ LAN speed test (round %d) ============", round);
        if (rx_ok) {
            log_info("[QUIC]   DOWNLOAD (RX): %d kbps  (~%d Mbps)",
                       (int)rx_kbps, (int)(rx_kbps / 1000));
        } else {
            log_error("[QUIC]   DOWNLOAD (RX): FAILED");
        }
        if (tx_ok) {
            log_info("[QUIC]   UPLOAD   (TX): %d kbps  (~%d Mbps)",
                       (int)tx_kbps, (int)(tx_kbps / 1000));
        } else {
            log_error("[QUIC]   UPLOAD   (TX): FAILED");
        }
        log_info("[QUIC] =================================================");

        vTaskDelay(pdMS_TO_TICKS(8000));   /* pause between rounds */
    }
}

void quic_test_start(void)
{
    static int started = 0;
    if (!started) {
        started = 1;
        /* Big stack: TLS 1.3 handshake (picotls + mbedTLS ECDHE keygen are stack-heavy)
         * + ngtcp2 conn state. 16K words = 64 KB. */
        xTaskCreate(quic_client_task, "quic_cli", 16384, NULL, tskIDLE_PRIORITY + 2, NULL);
    }
}
