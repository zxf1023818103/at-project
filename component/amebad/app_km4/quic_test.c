/*
 * AmebaD KM4 QUIC client self-test — ngtcp2 (v1.23.0) + in-tree wolfSSL TLS 1.3.
 *
 * Proves the ngtcp2 transport + wolfSSL QUIC crypto stack completes a real QUIC
 * v1 handshake over the FreeRTOS-Plus-TCP UDP socket layer that the bring-up
 * already stood up (DHCP IP). It connects to an ngtcp2 example server running on
 * the WSL host and reports whether the TLS 1.3 handshake completed.
 *
 *   Host side (in component/thirdparty/ngtcp2 after a host build):
 *     examples/server <host-ip> 4433 server.key server.crt
 *   (ALPN "hq-interop"; a self-signed cert is fine — the client runs
 *    SSL_VERIFY_NONE for this smoke test.)
 *
 * Structure mirrors ngtcp2's examples/simpleclient.c, with three substitutions:
 *   - crypto backend: quictls  -> wolfSSL  (ngtcp2_crypto_wolfssl_*)
 *   - socket layer:   POSIX    -> FreeRTOS-Plus-TCP (FreeRTOS_socket/sendto/recvfrom)
 *   - event loop:     libev    -> one task with a recvfrom timeout == ngtcp2 expiry
 *
 * Randomness: ngtcp2 callbacks use a wolfCrypt WC_RNG; wolfSSL's internal RNG
 * uses CUSTOM_RAND_GENERATE_BLOCK (the bench xorshift). Neither is crypto-grade
 * yet — a real TRNG is a follow-up. Functionally sufficient for the handshake.
 */
#include <string.h>
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

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/quic.h>
#include <wolfssl/wolfcrypt/random.h>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_wolfssl.h>

extern void DiagPrintf(const char *, ...);
extern void amebad_bench_rng_reseed(unsigned int seed);

/* ---- test target (the WSL host's ngtcp2 example server) ---- */
#define QUIC_SERVER_A0      192
#define QUIC_SERVER_A1      168
#define QUIC_SERVER_A2      4
#define QUIC_SERVER_A3      250
#define QUIC_SERVER_PORT    4433
#define QUIC_ALPN           "hq-interop"
#define QUIC_HANDSHAKE_TIMEOUT_MS  10000U   /* give up after 10 s */

struct quic_client {
    ngtcp2_crypto_conn_ref conn_ref;
    Socket_t sock;
    WOLFSSL_CTX *ssl_ctx;
    WOLFSSL *ssl;
    ngtcp2_conn *conn;
    ngtcp2_ccerr last_error;
    ngtcp2_sockaddr_in local_addr;
    ngtcp2_sockaddr_in remote_addr;
    struct freertos_sockaddr fr_remote;
};

static struct quic_client s_client;
static WC_RNG s_rng_ctx;
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
    if (wc_RNG_GenerateBlock(&s_rng_ctx, dest, (word32)destlen) != 0) {
        /* xorshift fallback so a transient RNG error can't wedge the test */
        size_t i;
        for (i = 0; i < destlen; i++) {
            dest[i] = (uint8_t)(xTaskGetTickCount() + i);
        }
    }
}

static int get_new_connection_id_cb(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                    ngtcp2_stateless_reset_token *token,
                                    size_t cidlen, void *user_data)
{
    (void)conn;
    (void)user_data;

    if (wc_RNG_GenerateBlock(&s_rng_ctx, cid->data, (word32)cidlen) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    cid->datalen = cidlen;

    if (wc_RNG_GenerateBlock(&s_rng_ctx, token->data, sizeof(token->data)) != 0) {
        return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    return 0;
}

static ngtcp2_conn *get_conn(ngtcp2_crypto_conn_ref *conn_ref)
{
    struct quic_client *c = conn_ref->user_data;
    return c->conn;
}

/* ---- wolfSSL client TLS context ---- */

/* NO_ASN_TIME (no on-target clock) makes wolfSSL force HAVE_SESSION_TICKET off,
 * so wolfSSL_CTX_UseSessionTicket — which ngtcp2_crypto_wolfssl_configure_client_
 * context() calls — isn't compiled. Resumption isn't needed for the handshake
 * test; provide a no-op so the helper succeeds. (Strong def: wolfSSL didn't emit
 * one.) */
int wolfSSL_CTX_UseSessionTicket(WOLFSSL_CTX *ctx)
{
    (void)ctx;
    return WOLFSSL_SUCCESS;
}

static int client_ssl_init(struct quic_client *c)
{
    c->ssl_ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());
    if (c->ssl_ctx == NULL) {
        DiagPrintf("[QUIC] wolfSSL_CTX_new failed\n\r");
        return -1;
    }

    if (ngtcp2_crypto_wolfssl_configure_client_context(c->ssl_ctx) != 0) {
        DiagPrintf("[QUIC] configure_client_context failed\n\r");
        return -1;
    }

    /* Smoke test: no CA store / no clock -> accept the server cert unverified. */
    wolfSSL_CTX_set_verify(c->ssl_ctx, WOLFSSL_VERIFY_NONE, NULL);

    c->ssl = wolfSSL_new(c->ssl_ctx);
    if (c->ssl == NULL) {
        DiagPrintf("[QUIC] wolfSSL_new failed\n\r");
        return -1;
    }

    wolfSSL_set_app_data(c->ssl, &c->conn_ref);
    wolfSSL_set_connect_state(c->ssl);

    if (wolfSSL_UseALPN(c->ssl, (char *)QUIC_ALPN, (unsigned int)strlen(QUIC_ALPN),
                        WOLFSSL_ALPN_MATCH) != WOLFSSL_SUCCESS) {
        DiagPrintf("[QUIC] UseALPN failed\n\r");
        return -1;
    }

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
    if (wc_RNG_GenerateBlock(&s_rng_ctx, dcid.data, (word32)dcid.datalen) != 0) {
        return -1;
    }
    scid.datalen = 8;
    if (wc_RNG_GenerateBlock(&s_rng_ctx, scid.data, (word32)scid.datalen) != 0) {
        return -1;
    }

    ngtcp2_settings_default(&settings);
    settings.initial_ts = quic_timestamp();
    /* log_printf left NULL: the ROM DiagPrintf can't handle ngtcp2's width/flag
     * format specifiers safely. */

    ngtcp2_transport_params_default(&params);
    params.initial_max_streams_uni            = 3;
    params.initial_max_stream_data_bidi_local = 128 * 1024;
    params.initial_max_data                   = 1024 * 1024;

    rv = ngtcp2_conn_client_new(&c->conn, &dcid, &scid, &path, NGTCP2_PROTO_VER_V1,
                                &callbacks, &settings, &params, NULL, c);
    if (rv != 0) {
        DiagPrintf("[QUIC] conn_client_new: %s\n\r", ngtcp2_strerror(rv));
        return -1;
    }

    ngtcp2_conn_set_tls_native_handle(c->conn, c->ssl);
    return 0;
}

/* ---- UDP I/O over FreeRTOS-Plus-TCP ---- */

static int client_send_packet(struct quic_client *c, const uint8_t *data, size_t datalen)
{
    int32_t sent = FreeRTOS_sendto(c->sock, data, datalen, 0,
                                   &c->fr_remote, sizeof(c->fr_remote));
    if (sent != (int32_t)datalen) {
        DiagPrintf("[QUIC] sendto short/err (%d/%d)\n\r", (int)sent, (int)datalen);
        return -1;
    }
    return 0;
}

/* Drain all locally queued packets ngtcp2 wants to send (no stream data — the
 * handshake test sends only crypto/ACK frames). */
static int client_write(struct quic_client *c)
{
    uint8_t buf[1280];
    ngtcp2_path_storage ps;
    ngtcp2_pkt_info pi;
    ngtcp2_ssize nwrite;

    ngtcp2_path_storage_zero(&ps);

    for (;;) {
        nwrite = ngtcp2_conn_writev_stream(c->conn, &ps.path, &pi, buf, sizeof(buf),
                                           NULL, NGTCP2_WRITE_STREAM_FLAG_NONE,
                                           -1, NULL, 0, quic_timestamp());
        if (nwrite < 0) {
            DiagPrintf("[QUIC] writev_stream: %s\n\r", ngtcp2_strerror((int)nwrite));
            ngtcp2_ccerr_set_liberr(&c->last_error, (int)nwrite, NULL, 0);
            return -1;
        }
        if (nwrite == 0) {
            return 0;   /* nothing more to send right now */
        }
        if (client_send_packet(c, buf, (size_t)nwrite) != 0) {
            return -1;
        }
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
        DiagPrintf("[QUIC] read_pkt: %s\n\r", ngtcp2_strerror(rv));
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
        DiagPrintf("[QUIC] socket() failed\n\r");
        return -1;
    }

    /* Bind to an ephemeral local port so recvfrom works and we can report the
     * local address into the ngtcp2 path. */
    (void)memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = FREERTOS_AF_INET;
    bind_addr.sin_port = 0;
    if (FreeRTOS_bind(c->sock, &bind_addr, sizeof(bind_addr)) != 0) {
        DiagPrintf("[QUIC] bind() failed\n\r");
        return -1;
    }

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

static void quic_client_task(void *arg)
{
    struct quic_client *c = &s_client;
    TickType_t deadline;
    int done = 0;
    int failed = 0;
    (void)arg;

    /* Wait until DHCP has handed us an IP. */
    while (FreeRTOS_GetIPAddress() == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    DiagPrintf("[QUIC] ngtcp2 %s + wolfSSL TLS1.3 client -> %d.%d.%d.%d:%d (ALPN %s)\n\r",
               ngtcp2_version(0)->version_str,
               QUIC_SERVER_A0, QUIC_SERVER_A1, QUIC_SERVER_A2, QUIC_SERVER_A3,
               QUIC_SERVER_PORT, QUIC_ALPN);

    amebad_bench_rng_reseed((unsigned int)xTaskGetTickCount() ^ (unsigned int)(uintptr_t)&c);

    if (wc_InitRng(&s_rng_ctx) != 0) {
        DiagPrintf("[QUIC] wc_InitRng failed\n\r");
        vTaskDelete(NULL);
        return;
    }

    (void)memset(c, 0, sizeof(*c));
    ngtcp2_ccerr_default(&c->last_error);
    c->conn_ref.get_conn = get_conn;
    c->conn_ref.user_data = c;

    if (client_socket_init(c) != 0 || client_ssl_init(c) != 0 ||
        client_quic_init(c) != 0) {
        DiagPrintf("[QUIC] init failed\n\r");
        goto cleanup;
    }

    /* Kick off the handshake (sends the Initial packet). */
    if (client_write(c) != 0) {
        failed = 1;
        goto report;
    }

    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(QUIC_HANDSHAKE_TIMEOUT_MS);

    while (!done && !failed) {
        TickType_t now = xTaskGetTickCount();
        ngtcp2_tstamp expiry, ts_now;
        TickType_t wait_ticks;
        int32_t nread;
        struct freertos_sockaddr from;
        socklen_t fromlen = sizeof(from);

        if ((int32_t)(now - deadline) >= 0) {
            DiagPrintf("[QUIC] handshake TIMEOUT after %d ms\n\r", (int)QUIC_HANDSHAKE_TIMEOUT_MS);
            failed = 1;
            break;
        }

        /* recvfrom timeout = time until ngtcp2's next timer (capped at 200 ms). */
        expiry = ngtcp2_conn_get_expiry2(c->conn);
        ts_now = quic_timestamp();
        if (expiry <= ts_now) {
            wait_ticks = 0;
        } else {
            uint64_t ms = (expiry - ts_now) / 1000000ULL;
            if (ms > 200) ms = 200;
            wait_ticks = pdMS_TO_TICKS((uint32_t)ms);
        }
        (void)FreeRTOS_setsockopt(c->sock, 0, FREERTOS_SO_RCVTIMEO,
                                  &wait_ticks, sizeof(wait_ticks));

        nread = FreeRTOS_recvfrom(c->sock, s_rxbuf, sizeof(s_rxbuf), 0, &from, &fromlen);
        if (nread > 0) {
            if (client_read(c, s_rxbuf, (size_t)nread) != 0) {
                failed = 1;
                break;
            }
        } else {
            /* timeout (0) or error (<0): drive ngtcp2's expiry/retransmit. */
            int rv = ngtcp2_conn_handle_expiry(c->conn, quic_timestamp());
            if (rv != 0) {
                DiagPrintf("[QUIC] handle_expiry: %s\n\r", ngtcp2_strerror(rv));
                failed = 1;
                break;
            }
        }

        if (client_write(c) != 0) {
            failed = 1;
            break;
        }

        if (ngtcp2_conn_get_handshake_completed(c->conn)) {
            done = 1;
        }
    }

report:
    if (done) {
        DiagPrintf("[QUIC] ===> HANDSHAKE COMPLETE: QUIC v1 + TLS1.3 over wolfSSL OK\n\r");
        /* Tidy close so the server logs a clean teardown. */
        {
            uint8_t buf[1280];
            ngtcp2_path_storage ps;
            ngtcp2_pkt_info pi;
            ngtcp2_ssize n;
            ngtcp2_path_storage_zero(&ps);
            if (!ngtcp2_conn_in_closing_period2(c->conn) &&
                !ngtcp2_conn_in_draining_period2(c->conn)) {
                n = ngtcp2_conn_write_connection_close(c->conn, &ps.path, &pi, buf,
                                                       sizeof(buf), &c->last_error,
                                                       quic_timestamp());
                if (n > 0) {
                    (void)client_send_packet(c, buf, (size_t)n);
                }
            }
        }
    } else {
        DiagPrintf("[QUIC] ===> HANDSHAKE FAILED (err_code=%d)\n\r",
                   (int)c->last_error.error_code);
    }

cleanup:
    if (c->conn) ngtcp2_conn_del(c->conn);
    if (c->ssl) wolfSSL_free(c->ssl);
    if (c->ssl_ctx) wolfSSL_CTX_free(c->ssl_ctx);
    if (c->sock && c->sock != FREERTOS_INVALID_SOCKET) FreeRTOS_closesocket(c->sock);
    wc_FreeRng(&s_rng_ctx);
    vTaskDelete(NULL);
}

void quic_test_start(void)
{
    static int started = 0;
    if (!started) {
        started = 1;
        /* Big stack: TLS 1.3 handshake + ngtcp2 conn state. */
        xTaskCreate(quic_client_task, "quic_cli", 8192, NULL, tskIDLE_PRIORITY + 2, NULL);
    }
}
