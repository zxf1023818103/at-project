/* SPDX-License-Identifier: Apache-2.0
 * Production adapter + real NimBLE mbuf/mempool code. Host/GAP/CoC boundary is
 * deterministic; its ownership contract is from the pinned 1.9/1.10 sources.
 * This is not a radio or iOS throughput test.
 */
#include <assert.h>
#include <stdarg.h>
#include "ble_coc_nimble.c"

/* Port initialization entry points are declared in nimble_port.c, not headers. */
extern void os_mempool_module_init(void);
extern void os_msys_init(void);

static struct ble_hs_conn conn;
static struct ble_l2cap_chan chan;
static struct ble_npl_eventq queue;
static uint32_t clock_ms;
static int send_mode, disconnect_calls, send_calls, timer_armed;
static uint8_t output[65536];
static unsigned output_size;
static unsigned depth, max_depth;
static void test_log(const char *format, ...) { (void)format; }
void ble_hs_lock(void) { }
void ble_hs_unlock(void) { }
struct ble_hs_conn *ble_hs_conn_find(uint16_t handle) { return handle==1 ? &conn : NULL; }
int ble_gap_conn_find(uint16_t h, struct ble_gap_conn_desc *d)
{ assert(h==1); memset(d,0,sizeof *d);d->conn_itvl=12;return 0; }
int ble_gap_terminate(uint16_t c, uint8_t reason) { (void)reason;assert(c==1);return 0; }
int ble_gap_update_params(uint16_t c, const struct ble_gap_upd_params *p)
{ assert(c==1 && p->itvl_min==12 && p->itvl_max==12);return 0; }
int ble_gap_set_data_len(uint16_t c, uint16_t octets, uint16_t usec)
{ assert(c==1 && octets==251 && usec==2120);return 0; }
int ble_gap_set_prefered_le_phy(uint16_t c, uint8_t tx, uint8_t rx, uint16_t options)
{ assert(c==1 && tx==2 && rx==2 && !options);return 0; }
int ble_gap_read_le_phy(uint16_t c, uint8_t *tx, uint8_t *rx)
{ assert(c==1);*tx=*rx=2;return 0; }
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) {return &queue;}
ble_npl_time_t ble_npl_time_get(void) {return clock_ms;}
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t t) {return t;}
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t t) {return t;}
uint32_t ble_npl_hw_enter_critical(void) {return 0;}
void ble_npl_hw_exit_critical(uint32_t state) {(void)state;}
void ble_npl_callout_init(struct ble_npl_callout *c, struct ble_npl_eventq *q, ble_npl_event_fn *f, void *arg)
{ (void)c;(void)q;(void)f;(void)arg; }
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *c, ble_npl_time_t t)
{ (void)c; assert(t);timer_armed=1;return 0; }
void ble_npl_callout_stop(struct ble_npl_callout *c) {(void)c;timer_armed=0;}
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s) {assert(s->uuid==&ble_coc_nimble_uuid.u);return 0;}
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s) {(void)s;return 0;}
int ble_l2cap_create_server(uint16_t psm, uint16_t mtu, ble_l2cap_event_fn *f, void *arg)
{ assert(psm==129 && mtu==2048 && f==coc_event && !arg);return 0; }
uint16_t ble_l2cap_get_conn_handle(struct ble_l2cap_chan *c) {return c ? c->conn_handle : BLE_HS_CONN_HANDLE_NONE;}
int ble_l2cap_get_chan_info(struct ble_l2cap_chan *c, struct ble_l2cap_chan_info *info)
{
    assert(c==&chan); memset(info,0,sizeof *info);
    info->psm=129;info->our_coc_mtu=2048;info->peer_coc_mtu=1251;
    info->our_l2cap_mtu=247;info->peer_l2cap_mtu=1251;return 0;
}
int ble_l2cap_recv_ready(struct ble_l2cap_chan *c, struct os_mbuf *om)
{ assert(c==&chan && !c->coc_rx.sdus[0]);c->coc_rx.sdus[0]=om;return 0; }
int ble_l2cap_disconnect(struct ble_l2cap_chan *c) {assert(c==&chan);disconnect_calls++;return 0;}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *data, uint16_t length)
{
    struct os_mbuf *om=os_msys_get_pkthdr(length,0);
    if (om && os_mbuf_append(om,data,length)) {os_mbuf_free_chain(om);return NULL;}
    return om;
}
static void accept_tx(struct os_mbuf *om)
{
    unsigned n=OS_MBUF_PKTLEN(om);assert(output_size+n<=sizeof output);
    assert(!os_mbuf_copydata(om,0,n,output+output_size));output_size+=n;
    os_mbuf_free_chain(om);
}
int ble_l2cap_send(struct ble_l2cap_chan *c, struct os_mbuf *om)
{
    assert(c==&chan && OS_MBUF_PKTLEN(om)<=TX_SDU_MAX);send_calls++;
    depth++;if(depth>max_depth)max_depth=depth;
    int rc=send_mode;
    if (rc==BLE_HS_ESTALLED) {assert(!chan.coc_tx.sdus[0]);chan.coc_tx.sdus[0]=om;}
    else if (!rc) accept_tx(om);
    else if (rc!=BLE_HS_EBUSY && rc!=BLE_HS_EBADDATA) os_mbuf_free_chain(om);
    depth--;return rc;
}
static void open_channel(void)
{
    memset(&chan,0,sizeof chan);chan.conn_handle=1;chan.coc_tx.mtu=1251;
    send_mode=0;clock_ms=0;output_size=0;disconnect_calls=0;
    struct ble_l2cap_event e={.type=BLE_L2CAP_EVENT_COC_ACCEPT,.accept={.conn_handle=1,.chan=&chan,.peer_sdu_size=1251}};
    assert(!coc_event(&e,NULL));assert(chan.initial_credits==24 && chan.coc_rx.credits==24);
    assert(coc->pool.mp_num_free==BLE_COC_RX_BUFFERS-1);
    e.type=BLE_L2CAP_EVENT_COC_CONNECTED;e.connect.status=0;e.connect.conn_handle=1;e.connect.chan=&chan;
    assert(!coc_event(&e,NULL));assert(timer_armed);
}
static void close_channel(void)
{
    struct ble_l2cap_event e={.type=BLE_L2CAP_EVENT_COC_DISCONNECTED,.disconnect={.conn_handle=1,.chan=&chan}};
    coc_event(&e,NULL);
    /* Actual NimBLE cleanup frees its owned SDUs after the disconnect event. */
    os_mbuf_free_chain(chan.coc_rx.sdus[0]);chan.coc_rx.sdus[0]=NULL;
    os_mbuf_free_chain(chan.coc_tx.sdus[0]);chan.coc_tx.sdus[0]=NULL;
    assert(!coc->channel && coc->pool.mp_num_free==BLE_COC_RX_BUFFERS && !timer_armed);
    tick(NULL);assert(!timer_armed); /* Already queued event is harmless. */
}
static void rx(const void *p, unsigned n)
{
    struct os_mbuf *om=chan.coc_rx.sdus[0];assert(om && n<=2048);
    chan.coc_rx.sdus[0]=NULL;assert(!os_mbuf_append(om,p,n));
    struct ble_l2cap_event e={.type=BLE_L2CAP_EVENT_COC_DATA_RECEIVED,.receive={.conn_handle=1,.chan=&chan,.sdu_rx=om}};
    coc_event(&e,NULL);
    if(!coc->closing)assert(chan.coc_rx.sdus[0] && coc->pool.mp_num_free==BLE_COC_RX_BUFFERS-1);
}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static uint32_t get32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void make(uint8_t *p,unsigned type,uint32_t a,uint32_t b,unsigned length)
{
    memcpy(p,"L2C1",4);p[4]=type;p[5]=2;p[6]=length;p[7]=length>>8;
    put32(p+8,a);put32(p+12,b);
    for(unsigned i=0;i<length;i++)p[16+i]=(a+i)%251;
}
int main(void)
{
    os_mempool_module_init();os_msys_init();STAILQ_INIT(&conn.bhc_tx_q);
    assert(!ble_coc_nimble_init(test_log));
    assert(ble_coc_nimble_init(NULL)==BLE_HS_EALREADY);
    char status[512];ble_coc_nimble_status(status,sizeof status);
    assert(!ble_coc_nimble_is_open());
    assert(strstr(status,"CoC ready=1") && strlen(status)<511);
    int free_before=os_msys_num_free();uint8_t p[1040];
    open_channel();make(p,1,1,1024,0);rx(p,7);rx(p+7,9);
    for(unsigned i=0;i<200;i++) {make(p,3,i*1024,0,1024);rx(p,1040);}
    make(p,4,200*1024,1000,0);clock_ms=1000;rx(p,16);tick(NULL);
    assert(output_size==24 && output[4]==5 && get32(output+8)==200*1024);
    assert(coc->stream.upload.complete && !coc->stream.upload.errors);
    assert(os_msys_num_free()==free_before);close_channel();
    /* Credits suspend an accepted SDU. It is sent exactly once on resume. */
    open_channel();make(p,2,1,1024,0);rx(p,16);send_mode=BLE_HS_ESTALLED;tick(NULL);
    assert(coc->stalled && chan.coc_tx.sdus[0] && !output_size);
    unsigned offset=coc->stream.tx_offset;int calls=send_calls;tick(NULL);
    assert(send_calls==calls && coc->stream.tx_offset==offset);
    accept_tx(chan.coc_tx.sdus[0]);chan.coc_tx.sdus[0]=NULL;send_mode=0;
    struct ble_l2cap_event e={.type=BLE_L2CAP_EVENT_COC_TX_UNSTALLED,.tx_unstalled={.chan=&chan,.status=0}};
    coc_event(&e,NULL);assert(send_calls==calls && !coc->stalled);
    clock_ms=1000;
    do {tick(NULL);} while(coc->stream.tx_size || coc->stream.phase==COC_TX);
    unsigned at=0, bytes=0;
    while(at<output_size) {
        unsigned n=output[at+6]|(unsigned)output[at+7]<<8;
        if(output[at+4]==3) {
            assert(get32(output+at+8)==bytes);
            for(unsigned i=0;i<n;i++)assert(output[at+16+i]==(bytes+i)%251);
            bytes+=n;
        } else assert(output[at+4]==4 && get32(output+at+8)==bytes);
        at+=16+n;
    }
    make(p,5,bytes,0,8);put32(p+16,1000);put32(p+20,0);rx(p,24);tick(NULL);
    assert(coc->stream.download.complete && !coc->stream.download.errors && max_depth==1);
    close_channel();assert(os_msys_num_free()==free_before);
    /* Queue and reserve limits pause before allocating or accepting bytes. */
    open_channel();make(p,2,1,1024,0);rx(p,16);
    struct os_mbuf_pkthdr queued[TX_QUEUE_LIMIT]={0};
    for(unsigned i=0;i<TX_QUEUE_LIMIT;i++)STAILQ_INSERT_TAIL(&conn.bhc_tx_q,&queued[i],omp_next);
    calls=send_calls;tick(NULL);
    assert(send_calls==calls && coc->stream.tx_offset==0 && coc->queue_peak==TX_QUEUE_LIMIT);
    STAILQ_INIT(&conn.bhc_tx_q);
    struct os_mbuf *held[MYNEWT_VAL(MSYS_1_BLOCK_COUNT)];unsigned held_count=0;
    while(os_msys_num_free()>=MSYS_RESERVE) {
        assert(held_count<MYNEWT_VAL(MSYS_1_BLOCK_COUNT));held[held_count]=os_msys_get_pkthdr(245,0);
        assert(held[held_count++]);
    }
    tick(NULL);assert(send_calls==calls && coc->stream.tx_offset==0);
    while(held_count)os_mbuf_free_chain(held[--held_count]);
    tick(NULL);assert(send_calls>calls);close_channel();
    /* Disconnect while credits are exhausted releases the retained TX SDU. */
    open_channel();make(p,2,1,1024,0);rx(p,16);send_mode=BLE_HS_ESTALLED;tick(NULL);
    assert(chan.coc_tx.sdus[0]);close_channel();
    assert(os_msys_num_free()==free_before && coc->stream.error==COC_ABORTED);
    const int errors[]={BLE_HS_EBUSY,BLE_HS_EBADDATA,BLE_HS_ENOMEM};
    for(unsigned i=0;i<3;i++) {
        open_channel();make(p,2,1,1024,0);rx(p,16);send_mode=errors[i];tick(NULL);
        assert(os_msys_num_free()==free_before && coc->stream.tx_offset==0);
        if(errors[i]==BLE_HS_EBUSY)assert(!disconnect_calls);
        else assert(disconnect_calls==1 && coc->closing);
        close_channel();
    }
    for(unsigned i=0;i<100;i++) {open_channel();p[0]='!';rx(p,16);assert(disconnect_calls==1);close_channel();}
    open_channel();ble_coc_nimble_reset();close_channel();
    assert(os_msys_num_free()==free_before);
    puts("PASS: production CoC callbacks with real NimBLE mbufs, 200 SDU reuses, credits/stall ownership, queue/pool backpressure, send failures, 100 reconnects, reset cleanup");
}
