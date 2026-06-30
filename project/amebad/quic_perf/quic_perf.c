/*
 * quic_perf — native (x86-64 / WSL2) QUIC throughput peer for the AmebaD
 * QUIC bring-up speed test.  ngtcp2 (v1.23) + wolfSSL TLS 1.3, POSIX UDP.
 *
 * Speaks the quic-interop "perf" protocol (ALPN "perf"):
 *   - The client opens one bidirectional stream and writes an 8-byte
 *     big-endian uint64 = the number of bytes it wants the server to send
 *     back (the DOWNLOAD size).  It then streams an arbitrary number of
 *     filler bytes (the UPLOAD) and closes its send side with FIN.
 *   - The server reads the 8-byte header, returns exactly that many bytes
 *     on the same stream (then FIN), and counts/discards everything the
 *     client uploads.
 * One stream therefore measures both directions; running download-only and
 * upload-only requests in turn gives clean unidirectional Mbps.
 *
 * This is the host end of the chip<->WSL2 LAN test.  It uses the same
 * ngtcp2 + ngtcp2_crypto_wolfssl stack the chip firmware uses, so the wire
 * format is identical by construction.
 *
 * Usage:
 *   quic_perf server <bind-ip> <port> <cert.pem> <key.pem>
 *   quic_perf client <ip> <port> <download_bytes> <upload_bytes>
 *
 * Build: see build.sh in this directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/random.h>
#include <pthread.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/quic.h>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_wolfssl.h>

#define ALPN              "perf"
#define MAX_UDP_PAYLOAD   1452
#define SEND_CHUNK        (256 * 1024)   /* per writev stream-data chunk    */
#define FLOW_WINDOW       (8 * 1024 * 1024)
#define ZEROBUF_LEN       (64 * 1024)

static uint8_t g_zerobuf[ZEROBUF_LEN];   /* upload/download filler (zeros)  */
static volatile int g_max_conns = 0;     /* server: stop after N conns (0=inf)*/
static volatile int g_server_ready = 0;  /* server has bound the socket      */

static int run_server(const char *ip, int port, const char *cert, const char *key);
static int run_client(const char *ip, int port, uint64_t dl, uint64_t ul);

/* ------------------------------------------------------------------ utils */

static uint64_t timestamp_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * NGTCP2_SECONDS + (uint64_t)ts.tv_nsec;
}

static void rand_bytes(uint8_t *dst, size_t len)
{
    while (len) {
        ssize_t n = getrandom(dst, len, 0);
        if (n <= 0) { /* fallback */ for (size_t i = 0; i < len; i++) dst[i] = (uint8_t)rand(); return; }
        dst += n; len -= (size_t)n;
    }
}

static void rand_cb(uint8_t *dest, size_t destlen, const ngtcp2_rand_ctx *ctx)
{
    (void)ctx;
    rand_bytes(dest, destlen);
}

static int get_new_connection_id_cb(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                    uint8_t *token, size_t cidlen, void *user)
{
    (void)conn; (void)user;
    rand_bytes(cid->data, cidlen);
    cid->datalen = cidlen;
    rand_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN);
    return 0;
}

static double mbps(uint64_t bytes, uint64_t ns)
{
    if (ns == 0) return 0.0;
    return ((double)bytes * 8.0) / ((double)ns / 1e9) / 1e6;
}

/* ------------------------------------------------------------------ conn */

struct peer {
    int                       sock;
    int                       is_server;
    ngtcp2_conn              *conn;
    ngtcp2_crypto_conn_ref    conn_ref;
    WOLFSSL_CTX              *ssl_ctx;
    WOLFSSL                  *ssl;

    struct sockaddr_storage   local_sa, remote_sa;
    socklen_t                 local_salen, remote_salen;

    int64_t  stream_id;        /* the single perf stream (-1 = none yet)     */
    int      stream_opened;

    /* perf protocol bookkeeping */
    uint64_t send_remaining;   /* bytes still to push onto the stream        */
    int      send_fin_done;    /* FIN has been queued                        */
    uint64_t sent_total;       /* stream bytes actually written (payload)    */
    uint64_t recv_total;       /* bytes received on the stream (excl. hdr)   */
    uint8_t  hdr[8];           /* 8-byte download-length header              */
    size_t   hdr_got;          /* bytes of hdr collected (server side)       */
    int      peer_fin;         /* remote closed its send side                */

    /* client request params */
    uint64_t want_download;
    uint64_t want_upload;

    /* timing */
    uint64_t t_start, t_first_byte, t_done;
    int      done;
};

static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *ref)
{
    return ((struct peer *)ref->user_data)->conn;
}

/* ---- stream callbacks (shared shape, role-specific behaviour) ---- */

static int recv_stream_data_cb(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id,
                               uint64_t offset, const uint8_t *data, size_t datalen,
                               void *user, void *stream_user)
{
    struct peer *p = user;
    (void)offset; (void)stream_user;
    size_t consumed = datalen;

    if (p->is_server) {
        const uint8_t *d = data; size_t n = datalen;
        /* collect the 8-byte download-length header first */
        while (p->hdr_got < sizeof(p->hdr) && n) {
            p->hdr[p->hdr_got++] = *d++; n--;
        }
        if (p->hdr_got == sizeof(p->hdr) && p->send_remaining == 0 && !p->send_fin_done &&
            p->stream_id == stream_id) {
            uint64_t dl = 0;
            for (int i = 0; i < 8; i++) dl = (dl << 8) | p->hdr[i];
            p->send_remaining = dl;
            if (dl == 0) p->send_fin_done = 1; /* nothing to send -> FIN later */
        }
        p->recv_total += n;   /* the rest is upload payload, just count it */
    } else {
        if (p->t_first_byte == 0 && datalen) p->t_first_byte = timestamp_ns();
        p->recv_total += datalen;   /* download bytes */
    }

    /* keep the receive flow-control window open */
    ngtcp2_conn_extend_max_stream_offset(conn, stream_id, consumed);
    ngtcp2_conn_extend_max_offset(conn, consumed);

    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN) {
        p->peer_fin = 1;
    }
    return 0;
}

static int stream_open_cb(ngtcp2_conn *conn, int64_t stream_id, void *user)
{
    struct peer *p = user;
    (void)conn;
    if (p->is_server && p->stream_id < 0) {
        p->stream_id = stream_id;
        p->stream_opened = 1;
        p->t_start = timestamp_ns();
    }
    return 0;
}

static int acked_stream_data_cb(ngtcp2_conn *conn, int64_t stream_id, uint64_t offset,
                                uint64_t datalen, void *user, void *stream_user)
{
    (void)conn; (void)stream_id; (void)offset; (void)datalen; (void)user; (void)stream_user;
    return 0;
}

/* ------------------------------------------------------------------ TLS */

static int server_alpn_select(WOLFSSL *ssl, const unsigned char **out, unsigned char *outlen,
                              const unsigned char *in, unsigned int inlen, void *arg)
{
    (void)ssl; (void)arg;
    /* in = client's ALPN list (len-prefixed). Pick "perf" if offered. */
    for (unsigned int i = 0; i + 1 <= inlen; ) {
        unsigned int l = in[i];
        if (i + 1 + l > inlen) break;
        if (l == strlen(ALPN) && memcmp(&in[i + 1], ALPN, l) == 0) {
            *out = &in[i + 1];
            *outlen = (unsigned char)l;
            return SSL_TLSEXT_ERR_OK;
        }
        i += 1 + l;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

static int tls_init(struct peer *p, const char *cert, const char *key)
{
    if (p->is_server) {
        p->ssl_ctx = wolfSSL_CTX_new(wolfTLSv1_3_server_method());
        if (!p->ssl_ctx) return -1;
        if (ngtcp2_crypto_wolfssl_configure_server_context(p->ssl_ctx) != 0) return -1;
        if (wolfSSL_CTX_use_certificate_chain_file(p->ssl_ctx, cert) != WOLFSSL_SUCCESS) {
            fprintf(stderr, "load cert %s failed\n", cert); return -1;
        }
        if (wolfSSL_CTX_use_PrivateKey_file(p->ssl_ctx, key, WOLFSSL_FILETYPE_PEM) != WOLFSSL_SUCCESS) {
            fprintf(stderr, "load key %s failed\n", key); return -1;
        }
        wolfSSL_CTX_set_alpn_select_cb(p->ssl_ctx, server_alpn_select, NULL);
        p->ssl = wolfSSL_new(p->ssl_ctx);
        if (!p->ssl) return -1;
        wolfSSL_set_app_data(p->ssl, &p->conn_ref);
        wolfSSL_set_accept_state(p->ssl);
    } else {
        p->ssl_ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
        if (!p->ssl_ctx) { fprintf(stderr, "CTX_new failed\n"); return -1; }
        if (ngtcp2_crypto_wolfssl_configure_client_context(p->ssl_ctx) != 0) {
            fprintf(stderr, "configure_client_context failed\n"); return -1; }
        /* Restrict key-share groups to curves the (constrained) peer actually
         * compiles in. wolfSSL's default preferred group is P-521, which a
         * P-256/X25519-only build can't generate -> BAD_FUNC_ARG. */
        { int g[] = { WOLFSSL_ECC_X25519, WOLFSSL_ECC_SECP256R1 };
          wolfSSL_CTX_set_groups(p->ssl_ctx, g, 2); }
        wolfSSL_CTX_set_verify(p->ssl_ctx, WOLFSSL_VERIFY_NONE, NULL);
        p->ssl = wolfSSL_new(p->ssl_ctx);
        if (!p->ssl) { fprintf(stderr, "wolfSSL_new failed\n"); return -1; }
        wolfSSL_set_app_data(p->ssl, &p->conn_ref);
        wolfSSL_set_connect_state(p->ssl);
        int ar = wolfSSL_UseALPN(p->ssl, (char *)ALPN, (unsigned)strlen(ALPN),
                                 WOLFSSL_ALPN_FAILED_ON_MISMATCH);
        if (ar != WOLFSSL_SUCCESS) { fprintf(stderr, "UseALPN failed (%d)\n", ar); return -1; }
    }
    return 0;
}

/* ------------------------------------------------------------------ ngtcp2 */

static void common_settings(ngtcp2_settings *s, ngtcp2_transport_params *p)
{
    ngtcp2_settings_default(s);
    s->initial_ts = timestamp_ns();
    s->max_tx_udp_payload_size = MAX_UDP_PAYLOAD;

    ngtcp2_transport_params_default(p);
    p->initial_max_streams_bidi            = 8;
    p->initial_max_streams_uni             = 8;
    p->initial_max_stream_data_bidi_local  = FLOW_WINDOW;
    p->initial_max_stream_data_bidi_remote = FLOW_WINDOW;
    p->initial_max_stream_data_uni         = FLOW_WINDOW;
    p->initial_max_data                    = FLOW_WINDOW;
}

static const ngtcp2_callbacks g_client_cbs = {
    .client_initial           = ngtcp2_crypto_client_initial_cb,
    .recv_crypto_data         = ngtcp2_crypto_recv_crypto_data_cb,
    .encrypt                  = ngtcp2_crypto_encrypt_cb,
    .decrypt                  = ngtcp2_crypto_decrypt_cb,
    .hp_mask                  = ngtcp2_crypto_hp_mask_cb,
    .recv_stream_data         = recv_stream_data_cb,
    .acked_stream_data_offset = acked_stream_data_cb,
    .recv_retry               = ngtcp2_crypto_recv_retry_cb,
    .rand                     = rand_cb,
    .get_new_connection_id    = get_new_connection_id_cb,
    .update_key               = ngtcp2_crypto_update_key_cb,
    .delete_crypto_aead_ctx   = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
    .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
    .get_path_challenge_data  = ngtcp2_crypto_get_path_challenge_data_cb,
    .version_negotiation      = ngtcp2_crypto_version_negotiation_cb,
};

static const ngtcp2_callbacks g_server_cbs = {
    .recv_client_initial      = ngtcp2_crypto_recv_client_initial_cb,
    .recv_crypto_data         = ngtcp2_crypto_recv_crypto_data_cb,
    .encrypt                  = ngtcp2_crypto_encrypt_cb,
    .decrypt                  = ngtcp2_crypto_decrypt_cb,
    .hp_mask                  = ngtcp2_crypto_hp_mask_cb,
    .recv_stream_data         = recv_stream_data_cb,
    .acked_stream_data_offset = acked_stream_data_cb,
    .stream_open              = stream_open_cb,
    .rand                     = rand_cb,
    .get_new_connection_id    = get_new_connection_id_cb,
    .update_key               = ngtcp2_crypto_update_key_cb,
    .delete_crypto_aead_ctx   = ngtcp2_crypto_delete_crypto_aead_ctx_cb,
    .delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb,
    .get_path_challenge_data  = ngtcp2_crypto_get_path_challenge_data_cb,
    .version_negotiation      = ngtcp2_crypto_version_negotiation_cb,
};

static void mkpath(ngtcp2_path *path, struct peer *p)
{
    path->local.addr     = (ngtcp2_sockaddr *)&p->local_sa;
    path->local.addrlen  = p->local_salen;
    path->remote.addr    = (ngtcp2_sockaddr *)&p->remote_sa;
    path->remote.addrlen = p->remote_salen;
    path->user_data      = NULL;
}

/* ------------------------------------------------------------------ I/O loop */

static int flush_send(struct peer *p)
{
    uint8_t buf[MAX_UDP_PAYLOAD];
    ngtcp2_path_storage ps;
    ngtcp2_pkt_info pi;
    ngtcp2_path_storage_zero(&ps);

    for (;;) {
        int64_t sid = -1;
        ngtcp2_vec vec; size_t veccnt = 0;
        uint32_t flags = NGTCP2_WRITE_STREAM_FLAG_NONE;

        /* The server must not touch its send side until it has parsed the
         * 8-byte download-length request; otherwise it FINs with 0 bytes. */
        int may_send = p->stream_id >= 0 && (p->send_remaining > 0 || !p->send_fin_done);
        if (p->is_server) may_send = may_send && p->stream_opened && p->hdr_got >= sizeof(p->hdr);
        if (may_send) {
            /* server sends download bytes; client sends the 8-byte header +
             * upload bytes -- both handled here via send_remaining + a small
             * pending header the client queues before the loop. */
            size_t chunk = p->send_remaining > SEND_CHUNK ? SEND_CHUNK : (size_t)p->send_remaining;
            if (chunk > ZEROBUF_LEN) chunk = ZEROBUF_LEN;
            sid = p->stream_id;
            vec.base = g_zerobuf;
            vec.len  = chunk;
            veccnt   = chunk ? 1 : 0;
            if (p->send_remaining <= chunk) flags = NGTCP2_WRITE_STREAM_FLAG_FIN;
        }

        ngtcp2_ssize pdatalen = 0;
        ngtcp2_ssize n = ngtcp2_conn_writev_stream(p->conn, &ps.path, &pi, buf, sizeof(buf),
                                                   &pdatalen, flags, sid,
                                                   veccnt ? &vec : NULL, veccnt, timestamp_ns());
        if (n < 0) {
            if (n == NGTCP2_ERR_WRITE_MORE) {
                if (pdatalen > 0) { p->send_remaining -= (uint64_t)pdatalen; }
                continue;
            }
            if (n == NGTCP2_ERR_STREAM_DATA_BLOCKED || n == NGTCP2_ERR_STREAM_SHUT_WR) {
                /* can't send stream data now; flush non-stream frames only */
                p->stream_id = p->stream_id; /* keep */
                /* retry with sid=-1 by forcing no stream this iteration */
                ngtcp2_ssize m = ngtcp2_conn_writev_stream(p->conn, &ps.path, &pi, buf, sizeof(buf),
                                                           NULL, NGTCP2_WRITE_STREAM_FLAG_NONE,
                                                           -1, NULL, 0, timestamp_ns());
                if (m <= 0) return 0;
                sendto(p->sock, buf, (size_t)m, 0, (struct sockaddr *)&p->remote_sa, p->remote_salen);
                continue;
            }
            fprintf(stderr, "writev_stream: %s\n", ngtcp2_strerror((int)n));
            return -1;
        }
        if (pdatalen > 0) {
            p->send_remaining -= (uint64_t)pdatalen;
            p->sent_total += (uint64_t)pdatalen;
        }
        if (sid >= 0 && (flags & NGTCP2_WRITE_STREAM_FLAG_FIN) && p->send_remaining == 0) {
            p->send_fin_done = 1;
        }
        if (n == 0) return 0;   /* nothing more to send right now */
        if (sendto(p->sock, buf, (size_t)n, 0, (struct sockaddr *)&p->remote_sa,
                   p->remote_salen) < 0) {
            if (errno == ECONNREFUSED) return 0;   /* peer gone — stop quietly */
            perror("sendto"); return -1;
        }
    }
}

static int feed_packet(struct peer *p, const uint8_t *data, size_t datalen)
{
    ngtcp2_path path;
    mkpath(&path, p);
    ngtcp2_pkt_info pi = {0};
    int rv = ngtcp2_conn_read_pkt(p->conn, &path, &pi, data, datalen, timestamp_ns());
    if (rv != 0) {
        /* DRAINING/CLOSING just mean the peer is tearing down — not an error. */
        if (rv != NGTCP2_ERR_DRAINING && rv != NGTCP2_ERR_CLOSING)
            fprintf(stderr, "read_pkt: %s\n", ngtcp2_strerror(rv));
        return -1;
    }
    return 0;
}

/* milliseconds until the next ngtcp2 timer fires (-1 = infinite) */
static int next_timeout_ms(struct peer *p)
{
    ngtcp2_tstamp exp = ngtcp2_conn_get_expiry(p->conn);
    if (exp == UINT64_MAX) return 1000;
    uint64_t now = timestamp_ns();
    if (exp <= now) return 0;
    uint64_t d = (exp - now) / NGTCP2_MILLISECONDS;
    return d > 1000 ? 1000 : (int)d;
}

/* ------------------------------------------------------------------ server */

static int run_server(const char *ip, int port, const char *cert, const char *key)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &sa.sin_addr);
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) { perror("bind"); return 1; }
    printf("perf server on %s:%d (ALPN \"%s\"), waiting...\n", ip, port, ALPN);
    g_server_ready = 1;

    int conn_count = 0;
    for (;;) {
        /* block for the first datagram of a new connection */
        struct peer p;
        memset(&p, 0, sizeof(p));
        p.sock = sock; p.is_server = 1; p.stream_id = -1;
        p.conn_ref.get_conn = get_conn; p.conn_ref.user_data = &p;

        uint8_t buf[2048];
        struct sockaddr_storage ra; socklen_t ralen = sizeof(ra);
        ssize_t nr = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&ra, &ralen);
        if (nr <= 0) continue;
        /* Only a long-header Initial starts a new connection; drop stray
         * short-header packets (e.g. a previous connection's teardown) so they
         * don't get misparsed into a bogus connection that ignores the next
         * real client. */
        if (!(buf[0] & 0x80)) continue;

        memcpy(&p.remote_sa, &ra, ralen); p.remote_salen = ralen;
        p.local_salen = sizeof(p.local_sa);
        getsockname(sock, (struct sockaddr *)&p.local_sa, &p.local_salen);

        ngtcp2_version_cid vc;
        int rv = ngtcp2_pkt_decode_version_cid(&vc, buf, (size_t)nr, NGTCP2_MAX_CIDLEN);
        if (rv != 0) { fprintf(stderr, "decode_version_cid: %s\n", ngtcp2_strerror(rv)); continue; }

        if (tls_init(&p, cert, key) != 0) { fprintf(stderr, "tls_init failed\n"); continue; }

        ngtcp2_cid dcid, scid, ocid;
        ngtcp2_cid_init(&dcid, vc.scid, vc.scidlen);  /* our DCID = client's SCID */
        scid.datalen = 18; rand_bytes(scid.data, scid.datalen);
        ngtcp2_cid_init(&ocid, vc.dcid, vc.dcidlen);  /* original DCID client chose */

        ngtcp2_settings settings; ngtcp2_transport_params params;
        common_settings(&settings, &params);
        params.original_dcid = ocid;
        params.original_dcid_present = 1;

        ngtcp2_path path; mkpath(&path, &p);
        rv = ngtcp2_conn_server_new(&p.conn, &dcid, &scid, &path, vc.version,
                                    &g_server_cbs, &settings, &params, NULL, &p);
        if (rv != 0) { fprintf(stderr, "server_new: %s\n", ngtcp2_strerror(rv)); wolfSSL_free(p.ssl); wolfSSL_CTX_free(p.ssl_ctx); continue; }
        ngtcp2_conn_set_tls_native_handle(p.conn, p.ssl);

        if (feed_packet(&p, buf, (size_t)nr) != 0) goto cleanup;
        if (flush_send(&p) != 0) goto cleanup;

        /* drive the connection to completion */
        for (;;) {
            struct pollfd pfd = { .fd = sock, .events = POLLIN };
            int to = next_timeout_ms(&p);
            int pr = poll(&pfd, 1, to);
            if (pr < 0) { if (errno == EINTR) continue; break; }
            if (pr == 0) {
                ngtcp2_conn_handle_expiry(p.conn, timestamp_ns());
            } else {
                struct sockaddr_storage sra; socklen_t sl = sizeof(sra);
                ssize_t n2 = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&sra, &sl);
                if (n2 <= 0) continue;
                /* only feed packets from our peer (single-connection server) */
                if (sl == p.remote_salen && memcmp(&sra, &p.remote_sa, sl) == 0) {
                    if (feed_packet(&p, buf, (size_t)n2) != 0) break;
                }
            }
            if (flush_send(&p) != 0) break;

            /* done when we've sent all download bytes (FIN) and the client
             * finished its upload (FIN) */
            if (p.send_fin_done && p.peer_fin && p.stream_opened) {
                uint64_t dur = timestamp_ns() - p.t_start;
                printf("conn done in %.1f ms: served %lu B download, sank %lu B upload\n",
                       (double)dur / 1e6, (unsigned long)p.sent_total, (unsigned long)p.recv_total);
                if (p.sent_total)
                    printf("           download(send) throughput: %.2f Mbps\n", mbps(p.sent_total, dur));
                if (p.recv_total)
                    printf("           upload(recv)   throughput: %.2f Mbps\n", mbps(p.recv_total, dur));
                break;
            }
            if (ngtcp2_conn_in_closing_period(p.conn) || ngtcp2_conn_in_draining_period(p.conn))
                break;
        }
cleanup:
        if (p.conn) ngtcp2_conn_del(p.conn);
        if (p.ssl) wolfSSL_free(p.ssl);
        if (p.ssl_ctx) wolfSSL_CTX_free(p.ssl_ctx);
        if (g_max_conns && ++conn_count >= g_max_conns) { close(sock); return 0; }
        printf("waiting for next connection...\n");
    }
    return 0;
}

/* ---- in-process loopback self-test (one foreground process: no shell
 *      backgrounding, which this sandbox kills) ---- */
struct srv_args { const char *ip; int port; const char *cert, *key; };
static void *server_thread(void *a)
{
    struct srv_args *s = a;
    run_server(s->ip, s->port, s->cert, s->key);
    return NULL;
}
/* single bidirectional connection (one stream does both directions) */
static int run_selftest(uint64_t dl, uint64_t ul, const char *cert, const char *key)
{
    struct srv_args sa = { "127.0.0.1", 4433, cert, key };
    pthread_t th;
    g_max_conns = 1;
    if (pthread_create(&th, NULL, server_thread, &sa) != 0) { perror("pthread"); return 1; }
    while (!g_server_ready) usleep(2000);
    usleep(50000);
    int rc = run_client("127.0.0.1", 4433, dl, ul);
    pthread_join(th, NULL);
    return rc;
}
/* two sequential single-direction connections — mirrors the chip firmware's
 * download-then-upload pattern (server handles one stream per connection). */
static int run_selftest2(uint64_t bytes, const char *cert, const char *key)
{
    struct srv_args sa = { "127.0.0.1", 4433, cert, key };
    pthread_t th;
    g_max_conns = 2;
    if (pthread_create(&th, NULL, server_thread, &sa) != 0) { perror("pthread"); return 1; }
    while (!g_server_ready) usleep(2000);
    usleep(50000);
    printf("=== phase 1: download-only ===\n");
    int rc1 = run_client("127.0.0.1", 4433, bytes, 0);
    usleep(100000);
    printf("=== phase 2: upload-only ===\n");
    int rc2 = run_client("127.0.0.1", 4433, 0, bytes);
    pthread_join(th, NULL);
    return (rc1 || rc2) ? 1 : 0;
}

/* ------------------------------------------------------------------ client */

static int run_client(const char *ip, int port, uint64_t dl, uint64_t ul)
{
    struct peer p; memset(&p, 0, sizeof(p));
    p.is_server = 0; p.stream_id = -1;
    p.want_download = dl; p.want_upload = ul;
    p.conn_ref.get_conn = get_conn; p.conn_ref.user_data = &p;

    p.sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in ra = {0};
    ra.sin_family = AF_INET; ra.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &ra.sin_addr);
    /* connect() so the kernel fills in a concrete local addr for the path */
    if (connect(p.sock, (struct sockaddr *)&ra, sizeof(ra)) < 0) { perror("connect"); return 1; }
    memcpy(&p.remote_sa, &ra, sizeof(ra)); p.remote_salen = sizeof(ra);
    p.local_salen = sizeof(p.local_sa);
    getsockname(p.sock, (struct sockaddr *)&p.local_sa, &p.local_salen);

    if (tls_init(&p, NULL, NULL) != 0) { fprintf(stderr, "tls_init failed\n"); return 1; }

    ngtcp2_cid dcid, scid;
    dcid.datalen = NGTCP2_MIN_INITIAL_DCIDLEN; rand_bytes(dcid.data, dcid.datalen);
    scid.datalen = 8; rand_bytes(scid.data, scid.datalen);

    ngtcp2_settings settings; ngtcp2_transport_params params;
    common_settings(&settings, &params);

    ngtcp2_path path; mkpath(&path, &p);
    int rv = ngtcp2_conn_client_new(&p.conn, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1,
                                    &g_client_cbs, &settings, &params, NULL, &p);
    if (rv != 0) { fprintf(stderr, "client_new: %s\n", ngtcp2_strerror(rv)); return 1; }
    ngtcp2_conn_set_tls_native_handle(p.conn, p.ssl);

    int req_sent = 0;
    p.t_start = timestamp_ns();

    for (;;) {
        /* once the handshake completes, open the perf stream and queue the
         * 8-byte download-length header + upload payload */
        if (!req_sent && ngtcp2_conn_get_handshake_completed(p.conn)) {
            int64_t sid;
            if (ngtcp2_conn_open_bidi_stream(p.conn, &sid, &p) == 0) {
                /* write the 8-byte header immediately as stream data */
                uint8_t hdr[8];
                for (int i = 0; i < 8; i++) hdr[i] = (uint8_t)(p.want_download >> (8 * (7 - i)));
                ngtcp2_vec v = { .base = hdr, .len = 8 };
                ngtcp2_ssize pd = 0;
                uint8_t pkt[MAX_UDP_PAYLOAD];
                ngtcp2_path_storage ps; ngtcp2_pkt_info pi;
                ngtcp2_path_storage_zero(&ps);
                ngtcp2_ssize wn = ngtcp2_conn_writev_stream(p.conn, &ps.path, &pi, pkt, sizeof(pkt),
                                                            &pd, NGTCP2_WRITE_STREAM_FLAG_NONE,
                                                            sid, &v, 1, timestamp_ns());
                if (wn > 0) sendto(p.sock, pkt, (size_t)wn, 0, NULL, 0);
                p.stream_id = sid;
                p.stream_opened = 1;
                p.send_remaining = p.want_upload;   /* now stream the upload */
                if (p.want_upload == 0) p.send_fin_done = 0; /* still need FIN */
                p.t_start = timestamp_ns();
                req_sent = 1;
            }
        }

        if (req_sent) flush_send(&p);

        struct pollfd pfd = { .fd = p.sock, .events = POLLIN };
        int to = next_timeout_ms(&p);
        int pr = poll(&pfd, 1, to);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        if (pr == 0) {
            ngtcp2_conn_handle_expiry(p.conn, timestamp_ns());
        } else {
            uint8_t buf[2048];
            ssize_t n = recv(p.sock, buf, sizeof(buf), 0);
            if (n > 0 && feed_packet(&p, buf, (size_t)n) != 0) break;
        }
        flush_send(&p);

        /* completion: we uploaded everything (FIN) and downloaded all we asked */
        int dl_done = (p.want_download == 0) || (p.recv_total >= p.want_download) || p.peer_fin;
        int ul_done = p.send_fin_done || p.want_upload == 0;
        if (req_sent && dl_done && (p.want_upload == 0 || ul_done)) {
            p.t_done = timestamp_ns();
            uint64_t dur = p.t_done - p.t_start;
            printf("---- perf result ----\n");
            if (p.want_download)
                printf("DOWNLOAD (chip-RX equiv): %lu B in %.1f ms = %.2f Mbps\n",
                       (unsigned long)p.recv_total, (double)dur / 1e6, mbps(p.recv_total, dur));
            if (p.want_upload)
                printf("UPLOAD   (chip-TX equiv): %lu B in %.1f ms = %.2f Mbps\n",
                       (unsigned long)p.want_upload, (double)dur / 1e6, mbps(p.want_upload, dur));
            /* graceful close */
            uint8_t pkt[MAX_UDP_PAYLOAD];
            ngtcp2_path_storage ps; ngtcp2_pkt_info pi; ngtcp2_ccerr err;
            ngtcp2_ccerr_default(&err);
            ngtcp2_path_storage_zero(&ps);
            ngtcp2_ssize wn = ngtcp2_conn_write_connection_close(p.conn, &ps.path, &pi, pkt,
                                                                 sizeof(pkt), &err, timestamp_ns());
            if (wn > 0) sendto(p.sock, pkt, (size_t)wn, 0, NULL, 0);
            break;
        }
        if (ngtcp2_conn_in_closing_period(p.conn) || ngtcp2_conn_in_draining_period(p.conn))
            break;
    }

    ngtcp2_conn_del(p.conn);
    wolfSSL_free(p.ssl);
    wolfSSL_CTX_free(p.ssl_ctx);
    return 0;
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    wolfSSL_Init();
    /* g_zerobuf stays zero — content doesn't matter for a throughput test */

    if (argc >= 6 && strcmp(argv[1], "server") == 0) {
        if (argc >= 7) g_max_conns = atoi(argv[6]);   /* optional: exit after N conns */
        return run_server(argv[2], atoi(argv[3]), argv[4], argv[5]);
    }
    if (argc >= 6 && strcmp(argv[1], "client") == 0) {
        return run_client(argv[2], atoi(argv[3]),
                          strtoull(argv[4], NULL, 10), strtoull(argv[5], NULL, 10));
    }
    if (argc >= 4 && strcmp(argv[1], "selftest") == 0) {
        const char *cert = argc >= 5 ? argv[4] : "cert.pem";
        const char *key  = argc >= 6 ? argv[5] : "key.pem";
        return run_selftest(strtoull(argv[2], NULL, 10), strtoull(argv[3], NULL, 10), cert, key);
    }
    if (argc >= 3 && strcmp(argv[1], "selftest2") == 0) {
        const char *cert = argc >= 4 ? argv[3] : "cert.pem";
        const char *key  = argc >= 5 ? argv[4] : "key.pem";
        return run_selftest2(strtoull(argv[2], NULL, 10), cert, key);
    }
    fprintf(stderr,
        "usage:\n"
        "  %s server <bind-ip> <port> <cert.pem> <key.pem>\n"
        "  %s client <ip> <port> <download_bytes> <upload_bytes>\n"
        "  %s selftest <download_bytes> <upload_bytes> [cert.pem] [key.pem]\n",
        argv[0], argv[0], argv[0]);
    return 2;
}
