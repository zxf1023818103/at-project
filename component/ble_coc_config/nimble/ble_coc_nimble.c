/* SPDX-License-Identifier: Apache-2.0
 * Shared Flash Studio LE CoC adapter for NimBLE 1.9/1.10. All channel
 * mutations run on its event queue; UART reads a short diagnostic snapshot.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_l2cap.h"
#include "host/ble_hs_mbuf.h"
#include "mem/mem.h"
#include "ble_hs_priv.h" /* Pinned adapter: credits and bounded host TX queue. */
#include "ble_coc_nimble.h"
#include "ble_coc_config.h"
#include "coc_stream.h"

#define COC_PSM BLE_COC_PSM
#define RX_MTU BLE_COC_RX_MTU
#define RX_CREDITS BLE_COC_RX_CREDITS
#define TX_SDU_MAX BLE_COC_TX_SDU_MAX
#define TX_QUEUE_LIMIT BLE_COC_TX_QUEUE_LIMIT
#define MSYS_RESERVE BLE_COC_MSYS_RESERVE
#define COC_LOG(...) do { if (coc->log) coc->log(__VA_ARGS__); } while (0)

typedef struct {
    CocStream stream;
    ble_coc_log_fn log;
    struct os_mempool pool;
    struct os_mbuf_pool mbufs;
    struct ble_npl_callout timer;
    struct ble_l2cap_chan *channel;
    struct ble_l2cap_chan_info info;
    uint32_t opens, failures, pauses, stalls, queue_peak, rx_sdus, tx_sdus;
    uint16_t interval, latency, min_msys;
    uint8_t tx_phy, rx_phy, ready, stalled, closing;
} CocContext;
static CocContext *coc;
const ble_uuid128_t ble_coc_nimble_uuid=BLE_UUID128_INIT(BLE_COC_NIMBLE_UUID_BYTES);

static uint32_t now_ms(void)
{ return ble_npl_time_ticks_to_ms32(ble_npl_time_get()); }
static void schedule(unsigned ms)
{
    if (coc->channel && !coc->closing) {
        ble_npl_time_t ticks=ble_npl_time_ms_to_ticks32(ms);
        ble_npl_callout_reset(&coc->timer, ticks ? ticks : 1);
    }
}
static void failure(const char *reason)
{
    if (coc->closing) return;
    coc->failures++; coc->closing=1;
    coc_stream_abort(&coc->stream);
    COC_LOG("[CoC] ERROR %s code=%u phase=%u rx=%lu tx=%lu", reason,
        coc->stream.error, coc->stream.phase, (unsigned long)coc->stream.rx_bytes,
        (unsigned long)coc->stream.tx_bytes);
    ble_npl_callout_stop(&coc->timer);
    if (coc->channel) {
        uint16_t conn=ble_l2cap_get_conn_handle(coc->channel);
        if (ble_l2cap_disconnect(coc->channel))
            (void)ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}
static bool pipeline_full(void)
{
    unsigned queued=0;
    ble_hs_lock();
    struct ble_hs_conn *conn=ble_hs_conn_find(ble_l2cap_get_conn_handle(coc->channel));
    if (conn) {
        struct os_mbuf_pkthdr *p;
        STAILQ_FOREACH(p, &conn->bhc_tx_q, omp_next) queued++;
    }
    ble_hs_unlock();
    if (queued>coc->queue_peak) coc->queue_peak=queued;
    unsigned free_blocks=os_msys_num_free();
    if (free_blocks<coc->min_msys) coc->min_msys=free_blocks;
    if (!conn || queued>=TX_QUEUE_LIMIT || free_blocks<MSYS_RESERVE) {
        coc->pauses++; return true;
    }
    return false;
}
static void report_result(unsigned type)
{
    const CocResult *r=type==5 ? &coc->stream.upload : &coc->stream.download;
    COC_LOG("[CoC] %s bytes=%lu ms=%lu bad=%lu errors=%lu queue_peak=%lu msys_min=%u",
        type==5 ? "RX REPORT" : "TX ACK", (unsigned long)r->bytes,
        (unsigned long)r->milliseconds, (unsigned long)r->bad, (unsigned long)r->errors,
        (unsigned long)coc->queue_peak, coc->min_msys);
}
static void tick(struct ble_npl_event *event)
{
    (void)event;
    if (!coc->channel || coc->closing) return;
    if (coc_stream_prepare(&coc->stream, now_ms())) { failure("protocol timeout"); return; }
    for (unsigned i=0; i<BLE_COC_TX_BURST && coc->channel && !coc->closing && !coc->stalled; i++) {
        if (coc_stream_prepare(&coc->stream, now_ms())) { failure("prepare"); return; }
        if (!coc->stream.tx_size || pipeline_full()) break;
        uint8_t bytes[TX_SDU_MAX];
        unsigned limit=TX_SDU_MAX;
        if (limit>coc->info.peer_coc_mtu) limit=coc->info.peer_coc_mtu;
        if (limit>coc->info.peer_l2cap_mtu-2u) limit=coc->info.peer_l2cap_mtu-2u;
        size_t n=coc_stream_peek(&coc->stream, bytes, limit);
        struct os_mbuf *om=ble_hs_mbuf_from_flat(bytes, n);
        if (!om) { coc->pauses++; break; }
        int rc=ble_l2cap_send(coc->channel, om);
        /* 1.9/1.10 reject ownership before installing the SDU only in these two
         * cases. All later errors, success and ESTALLED consume the mbuf. */
        if (rc==BLE_HS_EBUSY || rc==BLE_HS_EBADDATA) {
            os_mbuf_free_chain(om);
            if (rc==BLE_HS_EBUSY) { coc->pauses++; break; }
            failure("SDU size"); return;
        }
        if (rc && rc!=BLE_HS_ESTALLED) { failure("SDU send"); return; }
        if (rc==BLE_HS_ESTALLED) { coc->stalled=1; coc->stalls++; }
        unsigned type=coc->stream.tx_header[4];
        coc_stream_sent(&coc->stream, n); coc->tx_sdus++;
        if (!coc->stream.tx_size && (type==5 || type==7)) report_result(type);
    }
    schedule(coc->stream.tx_size || coc->stream.phase==COC_TX ? BLE_COC_TX_INTERVAL_MS : BLE_COC_IDLE_INTERVAL_MS);
}
static void link_parameters(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    if (!ble_gap_conn_find(conn, &d)) { coc->interval=d.conn_itvl; coc->latency=d.conn_latency; }
}
static int coc_event(struct ble_l2cap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_L2CAP_EVENT_COC_ACCEPT: {
        if (coc->channel) return BLE_HS_EBUSY;
        struct os_mbuf *om=os_mbuf_get_pkthdr(&coc->mbufs, 0);
        if (!om) return BLE_HS_ENOMEM;
        /* Configured window is installed before the connection response.
         * Callbacks consume and rearm the single receive SDU synchronously. */
        event->accept.chan->initial_credits=RX_CREDITS;
        event->accept.chan->coc_rx.credits=RX_CREDITS;
        int rc=ble_l2cap_recv_ready(event->accept.chan, om);
        if (!rc) coc->channel=event->accept.chan;
        else if (rc==BLE_HS_EINVAL || rc==BLE_HS_EBUSY) os_mbuf_free_chain(om);
        return rc;
    }
    case BLE_L2CAP_EVENT_COC_CONNECTED: {
        if (event->connect.status) {
            coc->failures++; coc->channel=NULL;
            COC_LOG("[CoC] connect error=%d", event->connect.status); break;
        }
        coc->channel=event->connect.chan; coc->closing=coc->stalled=0;
        coc->opens++; coc->pauses=coc->stalls=coc->queue_peak=coc->rx_sdus=coc->tx_sdus=0;
        coc->min_msys=os_msys_num_free(); coc_stream_init(&coc->stream);
        int rc=ble_l2cap_get_chan_info(coc->channel, &coc->info);
        if (rc || coc->info.peer_l2cap_mtu<23 || coc->info.peer_coc_mtu<23) {
            failure("channel parameters"); break;
        }
        uint16_t conn=event->connect.conn_handle;
        struct ble_gap_upd_params p={.itvl_min=BLE_COC_CONN_INTERVAL,.itvl_max=BLE_COC_CONN_INTERVAL,.latency=0,.supervision_timeout=400};
        link_parameters(conn);
        (void)ble_gap_read_le_phy(conn, &coc->tx_phy, &coc->rx_phy);
        int dle=ble_gap_set_data_len(conn, 251, 2120);
        int phy=ble_gap_set_prefered_le_phy(conn, 2, 2, 0);
        int interval=ble_gap_update_params(conn, &p);
        COC_LOG("[CoC] OPEN PSM=%u MTU=%u/%u MPS=%u/%u credits=%u SDU=%u",
            COC_PSM, coc->info.our_coc_mtu, coc->info.peer_coc_mtu,
            coc->info.our_l2cap_mtu, coc->info.peer_l2cap_mtu, RX_CREDITS, TX_SDU_MAX);
        COC_LOG("[CoC] link requests DLE=%d PHY=%d interval=%d", dle, phy, interval);
        schedule(1); break;
    }
    case BLE_L2CAP_EVENT_COC_DISCONNECTED:
        if (event->disconnect.chan!=coc->channel) break;
        coc_stream_abort(&coc->stream);
        COC_LOG("[CoC] CLOSED phase=%u error=%u", coc->stream.phase, coc->stream.error);
        coc->channel=NULL; coc->stalled=0;
        ble_npl_callout_stop(&coc->timer);
        /* NimBLE frees the RX SDU AFTER this callback. Keep its pool alive. */
        break;
    case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
        if (event->tx_unstalled.chan!=coc->channel) break;
        if (event->tx_unstalled.status) { failure("credit resume"); break; }
        coc->stalled=0; schedule(1); break; /* Never send recursively here. */
    case BLE_L2CAP_EVENT_COC_DATA_RECEIVED: {
        struct os_mbuf *om=event->receive.sdu_rx;
        unsigned old_phase=coc->stream.phase;
        if (event->receive.chan!=coc->channel) { os_mbuf_free_chain(om); break; }
        coc->rx_sdus++;
        /* Walk the mbuf chain directly: no 4 KiB application RX frame copy. */
        for (struct os_mbuf *p=om; p && !coc->closing; p=SLIST_NEXT(p, om_next))
            if (coc_stream_feed(&coc->stream, p->om_data, p->om_len, now_ms()))
                failure("stream frame");
        os_mbuf_free_chain(om);
        if (coc->closing || coc->channel!=event->receive.chan) break;
        om=os_mbuf_get_pkthdr(&coc->mbufs, 0);
        if (!om) { failure("RX pool"); break; }
        int rc=ble_l2cap_recv_ready(event->receive.chan, om);
        if (rc) {
            if (rc==BLE_HS_EINVAL || rc==BLE_HS_EBUSY) os_mbuf_free_chain(om);
            failure("RX rearm"); break;
        }
        if (old_phase==COC_IDLE && (coc->stream.phase==COC_RX || coc->stream.phase==COC_TX))
            COC_LOG("[CoC] START %s seconds=%lu block=%lu",
                coc->stream.phase==COC_RX ? "RX" : "TX", (unsigned long)coc->stream.seconds,
                (unsigned long)coc->stream.block);
        if (coc->stream.tx_size || coc->stream.phase==COC_TX) schedule(1);
        break;
    }
    default: break;
    }
    return 0;
}
static int read_psm(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    const uint8_t psm[]={COC_PSM,0};
    return os_mbuf_append(ctxt->om, psm, sizeof psm) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static const struct ble_gatt_chr_def characteristics[]={
    {.uuid=BLE_UUID16_DECLARE(0xfff4),.access_cb=read_psm,.flags=BLE_GATT_CHR_F_READ}, {0}};
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&ble_coc_nimble_uuid.u,.characteristics=characteristics}, {0}};

bool ble_coc_nimble_is_open(void)
{
    uint32_t state=ble_npl_hw_enter_critical();
    bool open=coc && coc->channel;
    ble_npl_hw_exit_critical(state);
    return open;
}
void ble_coc_nimble_status(char *out, size_t size)
{
    if (!coc) { snprintf(out, size, "CoC ready=0\r\n"); return; }
    uint32_t state=ble_npl_hw_enter_critical();
    CocContext c=*coc;
    uint16_t handle=c.channel ? ble_l2cap_get_conn_handle(c.channel) : BLE_HS_CONN_HANDLE_NONE;
    ble_npl_hw_exit_critical(state);
    snprintf(out, size,
        "CoC ready=%u conn=%u open=%u psm=%u phase=%u error=%u\r\n"
        "MTU=%u/%u MPS=%u/%u credits=%u tx_sdu=%u interval=%u latency=%u phy=%u/%u\r\n"
        "RX bytes=%lu ms=%lu bad=%lu errors=%lu complete=%u\r\n"
        "TX bytes=%lu ms=%lu bad=%lu errors=%lu complete=%u\r\n"
        "flow msys=%u min=%u queue_peak=%lu pauses=%lu stalls=%lu rx_sdus=%lu tx_sdus=%lu failures=%lu\r\n",
        c.ready, handle, c.channel!=NULL, COC_PSM, c.stream.phase, c.stream.error,
        RX_MTU, c.info.peer_coc_mtu, MYNEWT_VAL(BLE_L2CAP_COC_MPS), c.info.peer_l2cap_mtu,
        RX_CREDITS, TX_SDU_MAX, c.interval, c.latency, c.tx_phy, c.rx_phy,
        (unsigned long)c.stream.upload.bytes, (unsigned long)c.stream.upload.milliseconds,
        (unsigned long)c.stream.upload.bad, (unsigned long)c.stream.upload.errors, c.stream.upload.complete,
        (unsigned long)c.stream.download.bytes, (unsigned long)c.stream.download.milliseconds,
        (unsigned long)c.stream.download.bad, (unsigned long)c.stream.download.errors, c.stream.download.complete,
        os_msys_num_free(), c.min_msys, (unsigned long)c.queue_peak, (unsigned long)c.pauses,
        (unsigned long)c.stalls, (unsigned long)c.rx_sdus, (unsigned long)c.tx_sdus, (unsigned long)c.failures);
}
int ble_coc_nimble_init(ble_coc_log_fn log)
{
    if (coc) return BLE_HS_EALREADY;
    coc=calloc(1, sizeof *coc);
    if (!coc) return BLE_HS_ENOMEM;
    coc->log=log;
    /* RX SDUs are recycled before DATA_RECEIVED returns. The consumer
     * selects the pool size and supplies its host allocator via the build. */
    int rc=mem_malloc_mbufpkt_pool(&coc->pool, &coc->mbufs, BLE_COC_RX_BUFFERS, RX_MTU, "coc_rx", NULL);
    if (rc) return rc;
    ble_npl_callout_init(&coc->timer, nimble_port_get_dflt_eventq(), tick, NULL);
    if ((rc=ble_l2cap_create_server(COC_PSM, RX_MTU, coc_event, NULL))) return rc;
    if ((rc=ble_gatts_count_cfg(services))) return rc;
    if ((rc=ble_gatts_add_svcs(services))) return rc;
    coc->min_msys=os_msys_num_free(); coc->ready=1;
    return 0;
}
void ble_coc_nimble_gap_event(struct ble_gap_event *event)
{
    if (!coc || !coc->channel) return;
    uint16_t conn=ble_l2cap_get_conn_handle(coc->channel);
    if (event->type==BLE_GAP_EVENT_PHY_UPDATE_COMPLETE && event->phy_updated.conn_handle==conn) {
        coc->tx_phy=event->phy_updated.tx_phy; coc->rx_phy=event->phy_updated.rx_phy;
        COC_LOG("[CoC] PHY status=%d TX=%u RX=%u", event->phy_updated.status, coc->tx_phy, coc->rx_phy);
    } else if (event->type==BLE_GAP_EVENT_CONN_UPDATE && event->conn_update.conn_handle==conn) {
        link_parameters(conn);
        COC_LOG("[CoC] interval=%u latency=%u status=%d", coc->interval, coc->latency, event->conn_update.status);
    }
}
void ble_coc_nimble_reset(void)
{
    if (!coc) return;
    coc_stream_abort(&coc->stream);
    coc->channel=NULL; coc->stalled=0;
    ble_npl_callout_stop(&coc->timer);
}
