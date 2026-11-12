/* Opt-in NimBLE LE CoC fixture. Bulk traffic uses dynamic L2CAP channels;
 * GATT only exposes the PSM. All state runs on the NimBLE host event queue. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_l2cap.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include "os/os_mempool.h"
#include "ble_coc.h"
#include "ble_hs_priv.h" /* fixture-local window and host TX-queue adapter */

#define COC_PSM 0x81
#define MAX_DATA 4096
#define RX_MTU 2048
#define RX_CREDITS 24
#define TX_QUEUE_LIMIT 8
#define POOL_COUNT 8
#define POOL_BLOCK 2112
static struct os_mempool pool;
static struct os_mbuf_pool mbuf_pool;
static os_membuf_t pool_memory[OS_MEMPOOL_SIZE(POOL_COUNT,POOL_BLOCK)];
static struct ble_l2cap_chan *channel;
static struct ble_npl_callout timer;
static uint8_t rx_frame[16+MAX_DATA], tx_frame[16+MAX_DATA];
static unsigned rx_have, rx_need=16, tx_size, tx_offset, block=1024, phase;
static uint32_t rx_bytes, tx_bytes, bad, errors, seconds;
static uint32_t started, deadline;
static bool stalled, closing;
static uint16_t peer_mtu, peer_mps;
static uint32_t queue_peak, queue_pauses;
static uint32_t now_ms(void) {return ble_npl_time_ticks_to_ms32(ble_npl_time_get());}
static uint32_t get32(const uint8_t *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=n>>(i*8);}
static void frame(unsigned type,uint32_t a,uint32_t b,const uint8_t *data,unsigned length)
{
    assert(!tx_size && length<=MAX_DATA);
    memcpy(tx_frame,"L2C1",4);tx_frame[4]=type;tx_frame[5]=2;
    tx_frame[6]=length;tx_frame[7]=length>>8;
    put32(tx_frame+8,a);put32(tx_frame+12,b);
    if(length && data)memcpy(tx_frame+16,data,length);
    tx_size=16+length;tx_offset=0;
}
static void failure(const char *reason)
{
    printf("[CoC] ERROR %s phase=%u rx=%lu tx=%lu bad=%lu\r\n",reason,phase,(unsigned long)rx_bytes,(unsigned long)tx_bytes,(unsigned long)bad);
    phase=0;errors++;closing=true;ble_npl_callout_stop(&timer);
    if(channel)ble_l2cap_disconnect(channel);
}
static void consume(void)
{
    unsigned type=rx_frame[4], length=rx_frame[6]|(unsigned)rx_frame[7]<<8;
    uint32_t a=get32(rx_frame+8), b=get32(rx_frame+12);
    if(type==1 || type==2) {
        if(phase || tx_size || a<1 || a>120 || b<1 || b>MAX_DATA) {failure("invalid START");return;}
        rx_bytes=tx_bytes=bad=errors=0; seconds=a;block=b;
        started=now_ms();deadline=started+seconds*1000;phase=type;
        printf("[CoC] START %s seconds=%lu block=%u\r\n",type==1?"RX":"TX",(unsigned long)seconds,block);
    } else if(type==3 && phase==1) {
        if(a!=rx_bytes)errors++;
        for(unsigned i=0;i<length;i++)if(rx_frame[16+i]!=(rx_bytes+i)%251)bad++;
        rx_bytes+=length;
    } else if(type==4 && phase==1) {
        if(a!=rx_bytes)errors++;
        uint32_t elapsed=now_ms()-started;
        uint8_t report[8];put32(report,elapsed);put32(report+4,errors);
        if(tx_size) {failure("response backlog");return;}
        frame(5,rx_bytes,bad,report,sizeof(report));phase=0;
        printf("[CoC] RX END bytes=%lu ms=%lu bad=%lu errors=%lu\r\n",(unsigned long)rx_bytes,(unsigned long)elapsed,(unsigned long)bad,(unsigned long)errors);
    } else if(type==5 && phase==3 && length==8) {
        if(a!=tx_bytes || b || get32(rx_frame+20))errors++;
        if(tx_size) {failure("confirmation backlog");return;}
        frame(7,tx_bytes,errors,NULL,0);phase=0;
        printf("[CoC] TX queue peak=%lu pauses=%lu msys_free=%u\r\n",(unsigned long)queue_peak,(unsigned long)queue_pauses,os_msys_num_free());
        printf("[CoC] TX ACK bytes=%lu peer=%lu bad=%lu errors=%lu\r\n",(unsigned long)tx_bytes,(unsigned long)a,(unsigned long)b,(unsigned long)errors);
    } else {failure("unexpected frame");}
}
static void receive_bytes(const uint8_t *data,unsigned length)
{
    while(length && channel && !closing) {
        unsigned n=rx_need-rx_have;if(n>length)n=length;
        memcpy(rx_frame+rx_have,data,n);rx_have+=n;data+=n;length-=n;
        if(rx_have!=rx_need)continue;
        if(rx_need==16) {
            unsigned payload=rx_frame[6]|(unsigned)rx_frame[7]<<8;
            if(memcmp(rx_frame,"L2C1",4)||rx_frame[5]!=2||payload>MAX_DATA) {failure("invalid header");return;}
            rx_need=16+payload;
            if(payload)continue;
        }
        consume();rx_have=0;rx_need=16;
    }
}
static bool tx_pipeline_full(void)
{
    unsigned queued=0;
    ble_hs_lock();
    struct ble_hs_conn *conn=ble_hs_conn_find(channel->conn_handle);
    if(conn) {
        struct os_mbuf_pkthdr *packet;
        STAILQ_FOREACH(packet,&conn->bhc_tx_q,omp_next)queued++;
    }
    ble_hs_unlock();
    if(queued>queue_peak)queue_peak=queued;
    if(queued>=TX_QUEUE_LIMIT) {queue_pauses++;return true;}
    return conn==NULL;
}
static void tick(struct ble_npl_event *event)
{
    (void)event;
    if(!channel || closing)return;
    if(phase && now_ms()-started>(seconds+15)*1000) {failure("test timeout");return;}
    for(unsigned j=0;j<4 && channel && !closing && !stalled;j++) {
        if(!tx_size && phase==2) {
            if((int32_t)(now_ms()-deadline)>=0) {
                frame(4,tx_bytes,now_ms()-started,NULL,0);phase=3;
                printf("[CoC] TX END bytes=%lu ms=%lu\r\n",(unsigned long)tx_bytes,(unsigned long)(now_ms()-started));
            } else {
                frame(3,tx_bytes,0,NULL,block);
                for(unsigned i=0;i<block;i++)tx_frame[16+i]=(tx_bytes+i)%251;
            }
        }
        if(!tx_size)break;
        unsigned n=tx_size-tx_offset;
        if(n>peer_mtu)n=peer_mtu;
        if(n>peer_mps-2u)n=peer_mps-2u; /* same one-K-frame cap as Bluedroid */
        if(n>1024)n=1024;
        /* Reserve RX/control and fragmentation headroom rather than filling
         * the host ACL queue while the controller is credit-limited. */
        if(tx_pipeline_full() || os_msys_num_free()<12)break;
        struct os_mbuf *om=ble_hs_mbuf_from_flat(tx_frame+tx_offset,n);
        if(!om)break;
        int rc=ble_l2cap_send(channel,om);
        if(rc==BLE_HS_EBUSY) {os_mbuf_free_chain(om);break;}
        if(rc && rc!=BLE_HS_ESTALLED) {failure("SDU send failed (ownership consumed)");break;}
        /* NimBLE owns accepted/stalled SDUs; never resend or free them. */
        stalled=rc==BLE_HS_ESTALLED;tx_offset+=n;
        if(tx_offset==tx_size) {
            if(tx_frame[4]==3)tx_bytes+=tx_size-16;
            tx_size=tx_offset=0;
        }
    }
    if(channel && !closing)ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(2));
}
static int coc_event(struct ble_l2cap_event *event,void *arg)
{
    (void)arg;
    struct os_mbuf *om;
    switch(event->type) {
    case BLE_L2CAP_EVENT_COC_ACCEPT:
        if(channel)return BLE_HS_EBUSY;
        om=os_mbuf_get_pkthdr(&mbuf_pool,0);
        if(!om)return BLE_HS_ENOMEM;
        /* NimBLE 1.9.0 has no public initial-credit setting. The ACCEPT
         * callback precedes the wire response. Change both granted and refill
         * windows here, only for this fixture, without patching upstream. */
        event->accept.chan->initial_credits=RX_CREDITS;
        event->accept.chan->coc_rx.credits=RX_CREDITS;
        return ble_l2cap_recv_ready(event->accept.chan,om);
    case BLE_L2CAP_EVENT_COC_CONNECTED: {
        if(event->connect.status) {printf("[CoC] connect error=%d\r\n",event->connect.status);break;}
        channel=event->connect.chan;phase=0;tx_size=tx_offset=rx_have=0;rx_need=16;stalled=false;closing=false;
        struct ble_l2cap_chan_info info;
        int rc=ble_l2cap_get_chan_info(channel,&info);assert(!rc);
        peer_mtu=info.peer_coc_mtu;peer_mps=info.peer_l2cap_mtu;
        queue_peak=queue_pauses=0;
        printf("[CoC] OPEN PSM=%u SCID=%04x DCID=%04x MTU=%u/%u MPS=%u/%u transport=LE\r\n",info.psm,info.scid,info.dcid,info.our_coc_mtu,info.peer_coc_mtu,info.our_l2cap_mtu,info.peer_l2cap_mtu);
        printf("[CoC] credits=%u/%u TX queue limit=%u SDU cap=1024\r\n",
               channel->initial_credits,channel->coc_tx.credits,TX_QUEUE_LIMIT);
        ble_npl_callout_reset(&timer,1);break;
    }
    case BLE_L2CAP_EVENT_COC_DISCONNECTED:
        printf("[CoC] CLOSED phase=%u\r\n",phase);
        channel=NULL;phase=tx_size=rx_have=0;rx_need=16;ble_npl_callout_stop(&timer);break;
    case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
        if(event->tx_unstalled.status) {failure("unstall error");break;}
        stalled=false;break;
    case BLE_L2CAP_EVENT_COC_DATA_RECEIVED: {
        om=event->receive.sdu_rx;
        unsigned total=OS_MBUF_PKTLEN(om);uint8_t temp[256];
        for(unsigned offset=0;offset<total && !closing;) {
            unsigned n=total-offset;if(n>sizeof(temp))n=sizeof(temp);
            if(os_mbuf_copydata(om,offset,n,temp)) {failure("SDU copy");break;}
            receive_bytes(temp,n);offset+=n;
        }
        os_mbuf_free_chain(om);
        if(closing || channel!=event->receive.chan)break;
        om=os_mbuf_get_pkthdr(&mbuf_pool,0);
        if(!om) {failure("RX buffer exhausted");break;}
        int rc=ble_l2cap_recv_ready(event->receive.chan,om);
        /* 1.9.0 installs the mbuf before checking connection state. Only
         * EINVAL/EBUSY reject ownership; other errors leave it on the channel. */
        if(rc) {
            if(rc==BLE_HS_EINVAL || rc==BLE_HS_EBUSY)os_mbuf_free_chain(om);
            failure("RX credit refill");
        }
        break;
    }
    default:break;
    }
    return 0;
}
static int read_psm(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    (void)conn;(void)attr;(void)arg;uint8_t psm[]={COC_PSM,0};
    return os_mbuf_append(ctxt->om,psm,sizeof(psm))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
}
static const ble_uuid128_t psm_service=BLE_UUID128_INIT(0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x00,0x10,0xf0,0xb1);
static const struct ble_gatt_svc_def service[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&psm_service.u,
     .characteristics=(struct ble_gatt_chr_def[]){{.uuid=BLE_UUID16_DECLARE(0xfff4),.access_cb=read_psm,.flags=BLE_GATT_CHR_F_READ},{0}}},{0}};
int ble_coc_init(void)
{
    int rc=os_mempool_init(&pool,POOL_COUNT,POOL_BLOCK,pool_memory,"coc_sdu");if(rc)return rc;
    rc=os_mbuf_pool_init(&mbuf_pool,&pool,POOL_BLOCK,POOL_COUNT);if(rc)return rc;
    ble_npl_callout_init(&timer,nimble_port_get_dflt_eventq(),tick,NULL);
    rc=ble_l2cap_create_server(COC_PSM,RX_MTU,coc_event,NULL);if(rc)return rc;
    rc=ble_gatts_count_cfg(service);if(rc)return rc;
    rc=ble_gatts_add_svcs(service);
    printf("[CoC] NimBLE PSM=%u MTU=%u register=%d\r\n",COC_PSM,RX_MTU,rc);return rc;
}
void ble_coc_gap_event(struct ble_gap_event *event)
{
    if(event->type==BLE_GAP_EVENT_CONNECT && !event->connect.status) {
        uint16_t c=event->connect.conn_handle;
        struct ble_gap_upd_params params={.itvl_min=12,.itvl_max=12,.latency=0,.supervision_timeout=400,.min_ce_len=0,.max_ce_len=0};
        printf("[CoC] LE params request DLE=%d PHY=%d interval=%d\r\n",ble_gap_set_data_len(c,251,2120),ble_gap_set_prefered_le_phy(c,2,2,0),ble_gap_update_params(c,&params));
    } else if(event->type==BLE_GAP_EVENT_PHY_UPDATE_COMPLETE) {
        printf("[CoC] PHY status=%d TX=%u RX=%u\r\n",event->phy_updated.status,event->phy_updated.tx_phy,event->phy_updated.rx_phy);
    } else if(event->type==BLE_GAP_EVENT_CONN_UPDATE) {
        struct ble_gap_conn_desc d;
        if(!ble_gap_conn_find(event->conn_update.conn_handle,&d))printf("[CoC] interval=%u latency=%u timeout=%u status=%d\r\n",d.conn_itvl,d.conn_latency,d.supervision_timeout,event->conn_update.status);
    }
}
