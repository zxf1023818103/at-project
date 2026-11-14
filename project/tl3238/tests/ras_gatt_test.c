/* SPDX-License-Identifier: Apache-2.0
 * Execute the production GATT callbacks/FSM against a deterministic ATT/NPL
 * boundary. No replacement of codec, protocol or distance implementations.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble_ras.c"

struct mock_mbuf { struct os_mbuf om; struct os_mbuf_pkthdr hdr; uint8_t bytes[300]; };
static struct mock_mbuf txbuf, rxbuf;
static uint32_t ticks;
static int enc, sent_indicate, send_rc;
static uint16_t sent_handle, write_handle;
static unsigned sent_len, sends, client_writes, att_requests;
static uint8_t sent_bytes[244], written[5];
static struct ble_npl_eventq evq;
static struct os_mbuf *mbuf(struct mock_mbuf *m, const void *p, unsigned n)
{
    memset(m, 0, sizeof *m); m->om.om_data = m->bytes;
    m->om.om_pkthdr_len = sizeof m->hdr; m->om.om_len = m->hdr.omp_len = n;
    if (n) memcpy(m->bytes, p, n);
    return &m->om;
}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p, uint16_t n) { return mbuf(&txbuf, p, n); }
int os_mbuf_copydata(const struct os_mbuf *om, int off, int len, void *dst)
{
    if (off < 0 || len < 0 || off + len > om->om_len) return -1;
    memcpy(dst, om->om_data + off, len); return 0;
}
int os_mbuf_append(struct os_mbuf *om, const void *p, uint16_t n)
{
    assert(om->om_len + n <= 300); memcpy(om->om_data + om->om_len, p, n);
    om->om_len += n; OS_MBUF_PKTLEN(om) += n; return 0;
}
int ble_gap_conn_find(uint16_t h, struct ble_gap_conn_desc *d)
{
    if (h != 1) return BLE_HS_ENOTCONN;
    memset(d, 0, sizeof *d); d->sec_state.encrypted = enc; return 0;
}
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) { return &evq; }
ble_npl_time_t ble_npl_time_get(void) { return ticks; }
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }
void ble_npl_callout_init(struct ble_npl_callout *c, struct ble_npl_eventq *q, ble_npl_event_fn *f, void *a)
{ (void)c; (void)q; (void)f; (void)a; }
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *c, ble_npl_time_t t) { (void)c; (void)t; return 0; }
void ble_npl_callout_stop(struct ble_npl_callout *c) { (void)c; }
uint16_t ble_att_mtu(uint16_t c) { assert(c == 1); return 247; }
uint16_t ble_uuid_u16(const ble_uuid_t *u) { return u->type == BLE_UUID_TYPE_16 ? BLE_UUID16(u)->value : 0; }
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s) { assert(ble_uuid_u16(s->uuid) == 0x185b); return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s)
{
    for (unsigned i = 0; i < NUM_CHARS; i++) {
        assert(s->characteristics[i].flags & BLE_GATT_CHR_F_READ_ENC);
        assert(s->characteristics[i].flags & BLE_GATT_CHR_F_WRITE_ENC);
        *s->characteristics[i].val_handle = 10 + 3*i;
    }
    return 0;
}
static int send(uint16_t c, uint16_t h, struct os_mbuf *om, int ind)
{
    assert(c == 1); if (send_rc) return send_rc;
    sends++; sent_handle = h; sent_indicate = ind; sent_len = OS_MBUF_PKTLEN(om);
    assert(sent_len <= sizeof sent_bytes); memcpy(sent_bytes, om->om_data, sent_len); return 0;
}
int ble_gatts_indicate_custom(uint16_t c, uint16_t h, struct os_mbuf *om) { return send(c,h,om,1); }
int ble_gatts_notify_custom(uint16_t c, uint16_t h, struct os_mbuf *om) { return send(c,h,om,0); }
int ble_gattc_write_no_rsp_flat(uint16_t c, uint16_t h, const void *p, uint16_t n)
{ assert(c == 1 && n <= sizeof written); write_handle = h; memcpy(written,p,n); client_writes++; return 0; }
int ble_gattc_write_flat(uint16_t c, uint16_t h, const void *p, uint16_t n, ble_gatt_attr_fn *cb, void *arg)
{ (void)cb; (void)arg; assert(c==1 && n==2 && ((const uint8_t *)p)[0]==2); write_handle=h; att_requests++; return 0; }
int ble_gattc_exchange_mtu(uint16_t c, ble_gatt_mtu_fn *cb, void *arg)
{ (void)cb; (void)arg; assert(c==1); att_requests++; return 0; }
int ble_gattc_disc_svc_by_uuid(uint16_t c, const ble_uuid_t *u, ble_gatt_disc_svc_fn *cb, void *arg)
{ (void)cb; (void)arg; assert(c==1 && ble_uuid_u16(u)==0x185b); att_requests++; return 0; }
int ble_gattc_disc_all_chrs(uint16_t c, uint16_t a, uint16_t b, ble_gatt_chr_fn *cb, void *arg)
{ (void)cb; (void)arg; assert(c==1 && a<=b); att_requests++; return 0; }
int ble_gattc_disc_all_dscs(uint16_t c, uint16_t a, uint16_t b, ble_gatt_dsc_fn *cb, void *arg)
{ (void)cb; (void)arg; assert(c==1 && a<b); att_requests++; return 0; }
int ble_gattc_read(uint16_t c, uint16_t h, ble_gatt_attr_fn *cb, void *arg)
{ (void)cb; (void)arg; assert(c==1 && h); att_requests++; return 0; }
static void confirm(void)
{
    if (!server.pending_indication) return;
    struct ble_gap_event e = {.type=BLE_GAP_EVENT_NOTIFY_TX};
    e.notify_tx.conn_handle=1; e.notify_tx.attr_handle=server.pending_indication;
    e.notify_tx.indication=1; e.notify_tx.status=BLE_HS_EDONE;
    ble_ras_gap_event(&e);
}
static int control(const uint8_t *p, unsigned n)
{
    struct ble_gatt_access_ctxt c = {.op=BLE_GATT_ACCESS_OP_WRITE_CHR, .om=mbuf(&rxbuf,p,n)};
    return access_cb(1, values[CP], &c, (void *)CP);
}
static void connect(void)
{
    struct ble_gap_event e = {.type=BLE_GAP_EVENT_CONNECT}; e.connect.conn_handle=1;
    ble_ras_gap_event(&e);
}
int main(void)
{
    assert(!ble_ras_init()); connect();
    struct ble_gatt_access_ctxt access = {.op=BLE_GATT_ACCESS_OP_READ_CHR, .om=mbuf(&rxbuf,NULL,0)};
    assert(access_cb(1, values[FEATURES], &access, (void *)FEATURES) == BLE_ATT_ERR_INSUFFICIENT_ENC);
    assert(ble_ras_discover() == BLE_HS_EAUTHEN);
    enc=1;
    assert(!access_cb(1, values[FEATURES], &access, (void *)FEATURES));
    assert(OS_MBUF_PKTLEN(access.om)==4 && access.om->om_data[0]==4);
    uint8_t cp[] = {0,37,0};
    assert(control(cp,3)==BLE_ATT_ERR_CCCD_IMPORER_CONF);
    for (unsigned i=DATA; i<NUM_CHARS; i++) {
        struct ble_gap_event e = {.type=BLE_GAP_EVENT_SUBSCRIBE};
        e.subscribe.conn_handle=1; e.subscribe.attr_handle=values[i]; e.subscribe.cur_indicate=1;
        ble_ras_gap_event(&e);
    }
    assert(!ras_synthetic_pair(&local,&remote,2.5f)); status.available=1;
    assert(!control(cp,3)); assert(server.state==RAS_SERVER_SEND);
    struct ras_rx rebuilt = {0}; unsigned data_packets=0;
    while (server.state==RAS_SERVER_SEND || server.count || server.pending_indication) {
        unsigned before=sends; pump(NULL);
        if (sends!=before && sent_handle==values[DATA]) {
            assert(sent_indicate); assert(!ras_rx_segment(&rebuilt,sent_bytes,sent_len)); data_packets++;
        }
        confirm(); ticks+=20;
    }
    assert(data_packets==4 && rebuilt.complete && !memcmp(rebuilt.body,local.body,local.len));
    assert(rebuilt.body[3]==0); /* Erratum 26610: zero Previously Used / RFU on air. */
    assert(server.state==RAS_SERVER_ACK && server.wait_ack_started);
    assert(sent_handle==values[CP] && sent_len==3 && sent_bytes[0]==0 && sent_bytes[1]==37);
    cp[0]=1; assert(!control(cp,3)); assert(!status.available && server.state==RAS_SERVER_IDLE);
    pump(NULL); assert(sent_bytes[0]==2 && sent_bytes[1]==1); confirm();
    /* Backpressure retains offset; timeout releases operation but retains data. */
    status.available=1; cp[0]=0; assert(!control(cp,3)); send_rc=BLE_HS_ENOMEM;
    pump(NULL); assert(server.offset==0); send_rc=0;
    ticks+=5001; pump(NULL); assert(server.state==RAS_SERVER_IDLE && status.available);
    /* Notify takes precedence when both data CCCD bits are set. */
    server.subscribed[DATA]=3; assert(!control(cp,3)); pump(NULL);
    assert(sent_handle==values[DATA] && !sent_indicate && server.offset>0);
    uint8_t abort[]={3}; assert(!control(abort,1));
    assert(server.state==RAS_SERVER_IDLE && server.count==1);
    pump(NULL); assert(sent_handle==values[CP] && sent_bytes[1]==1); confirm();
    /* Abort after last segment must discard an unsent Complete response. */
    server.state=RAS_SERVER_ACK; queue_reply((uint8_t[]){0,37,0},3);
    assert(!control(abort,1) && server.count==1);
    pump(NULL); assert(sent_len==2 && sent_bytes[0]==2 && sent_bytes[1]==1); confirm();
    /* The actual discovery callbacks work with optional READY read absent. */
    assert(!ble_ras_discover()); assert(status.discovering);
    struct ble_gatt_error ok={0}, done={.status=BLE_HS_EDONE};
    mtu_exchanged(1,&ok,247,NULL);
    struct ble_gatt_svc svc={.start_handle=100,.end_handle=130};
    service(1,&ok,&svc,NULL); service(1,&done,NULL,NULL);
    for (unsigned i=0;i<NUM_CHARS;i++) {
        struct ble_gatt_chr c={.def_handle=101+3*i,.val_handle=102+3*i};
        c.uuid.u16=(ble_uuid16_t)BLE_UUID16_INIT(char_uuids[i]);
        c.properties=i==FEATURES?BLE_GATT_CHR_PROP_READ:BLE_GATT_CHR_PROP_INDICATE;
        if (i==CP) c.properties|=BLE_GATT_CHR_PROP_WRITE_NO_RSP;
        characteristic(1,&ok,&c,NULL);
    }
    characteristic(1,&done,NULL,NULL);
    uint8_t features[]={4,0,0,0};
    struct ble_gatt_attr a={.om=mbuf(&rxbuf,features,4)};
    features_read(1,&ok,&a,NULL);
    for(unsigned i=DATA;i<NUM_CHARS;i++) {
        struct ble_gatt_dsc d={.handle=client.value[i]+1,.uuid.u16=BLE_UUID16_INIT(0x2902)};
        descriptor(1,&ok,client.value[i],&d,NULL); descriptor(1,&done,client.value[i],NULL,NULL);
    }
    for(unsigned i=DATA;i<NUM_CHARS;i++) {
        assert(write_handle==client.cccd[i]); subscribed(1,&ok,NULL,NULL);
    }
    assert(status.client_ready && !status.discovering && !client.ready_readable);
    assert(!ras_synthetic_pair(&local,&remote,2.5f));
    /* Keep the peer response outside the production receive buffer. */
    uint8_t peer[RAS_MAX_BODY]; unsigned peer_len=remote.len; memcpy(peer,remote.body,peer_len);
    uint8_t ready[]={37,0}, complete[]={0,37,0}, ack[]={2,1}, segment[244];
    /* Complete actual client Get / data / ACK with corrected and legacy headers. */
    const uint8_t reserved[]={0x00,0x01,0x80,0xff};
    for (unsigned i=0;i<sizeof reserved;i++) {
        client.consumed=0; peer[3]=reserved[i]; incoming(client.value[READY],ready,2);
        assert(client.phase==CLIENT_DATA && written[0]==0);
        unsigned off=0,seq=61;
        while(off<peer_len) {
            unsigned n=ras_segment(peer,peer_len,off,seq++,23,segment,sizeof segment);
            incoming(client.value[DATA],segment,n); off+=n-1;
        }
        incoming(client.value[CP],complete,3);
        assert(client.phase==CLIENT_ACK && written[0]==1);
        assert(status.distance.status==0 && abs(status.distance.distance_mm-2500)<10);
        incoming(client.value[CP],ack,2);
        assert(client.phase==CLIENT_IDLE);
    }
    /* A new procedure / CS error cannot present the previous distance as valid. */
    struct ble_hci_ev_le_subev_cs_proc_enable_complete enabled={.state=1};
    struct ble_cs_event ce={.type=BLE_CS_EVENT_HCI};
    ce.hci.subevent=0x30; ce.hci.data=(const void *)&enabled; ce.hci.len=sizeof enabled;
    ble_ras_cs_event(&ce); assert(status.distance.status==RAS_INCOMPLETE);
    status.distance.status=0; ce.type=BLE_CS_EVENT_ERROR; ce.error.status=BLE_HS_EAPP;
    ble_ras_cs_event(&ce); assert(status.distance.status==BLE_HS_EAPP);
    /* Incomplete transfer must not retain a previously valid distance. */
    client.consumed=0; incoming(client.value[READY],ready,2);
    unsigned n=ras_segment(peer,peer_len,19,1,23,segment,sizeof segment);
    incoming(client.value[DATA],segment,n);
    assert(client.phase==CLIENT_IDLE && status.distance.status!=0);
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT}; ble_ras_gap_event(&e);
    assert(server.conn==BLE_HS_CONN_HANDLE_NONE && !local.active && !status.client_ready);
    assert(status.distance.status==RAS_INCOMPLETE);
    puts("PASS: production RAS GATT encryption/CCCD, server Get/ACK, pacing/backpressure/timeout, client discovery/subscription, erratum 26610 / legacy header paired distance, missing segment and disconnect cleanup (ASan/UBSan)");
    return 0;
}
