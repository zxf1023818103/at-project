/* Opt-in iPhone GATT benchmark. All state lives on the NimBLE event queue.
 * GBR1 control/status, raw offset-modulo-251 payload, cumulative RX receipts. */
#include <stdio.h>
#include <string.h>
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "ble_hs_priv.h"
#include "ble_gatt_bench.h"

static struct ble_npl_callout timer;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE, tx_handle;
static uint16_t block=244;
static uint8_t mode;
static bool subscribed, status_enabled, status_pending;
static uint32_t sent, received, bad, errors, blocked, started, duration;
static uint32_t credit_bytes, acknowledged, queue_peak;
static uint8_t payload[512];
static uint32_t now_ms(void) {return ble_npl_time_ticks_to_ms32(ble_npl_time_get());}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++)p[i]=value>>(8*i);}
static void status(uint8_t value[32])
{
    memcpy(value,"GBR1",4);value[4]=mode;value[5]=2;value[6]=block;value[7]=block>>8;
    put32(value+8,sent);put32(value+12,received);put32(value+16,errors);
    put32(value+20,bad);put32(value+24,now_ms()-started);put32(value+28,blocked);
}
static void stop(void)
{
    printf("[GATT-NB] END mode=%u tx=%lu rx=%lu bad=%lu errors=%lu ms=%lu queue_peak=%lu msys=%u\r\n",
           mode,(unsigned long)sent,(unsigned long)received,(unsigned long)bad,
           (unsigned long)errors,(unsigned long)(now_ms()-started),(unsigned long)queue_peak,os_msys_num_free());
    mode=0;status_pending=status_enabled;
}
static int access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    (void)conn;(void)attr;
    unsigned which=(uintptr_t)arg;
    if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        if(which!=3)return 0;
        uint8_t value[32];status(value);
        return os_mbuf_append(ctxt->om,value,sizeof(value))?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    if(ctxt->op!=BLE_GATT_ACCESS_OP_WRITE_CHR)return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    unsigned length=OS_MBUF_PKTLEN(ctxt->om);
    if(which==1) {
        if(mode!=2)return length?BLE_ATT_ERR_UNLIKELY:0; /* empty fence */
        uint8_t temp[256];
        for(unsigned offset=0;offset<length;) {
            unsigned n=length-offset;if(n>sizeof(temp))n=sizeof(temp);
            if(os_mbuf_copydata(ctxt->om,offset,n,temp))return BLE_ATT_ERR_UNLIKELY;
            for(unsigned i=0;i<n;i++)if(temp[i]!=(received+offset+i)%251)bad++;
            offset+=n;
        }
        received+=length;
        if(credit_bytes && received-acknowledged>=credit_bytes)status_pending=true;
        return 0;
    }
    if(which!=3 || length>=80)return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    char command[80]={0}, direction[8]={0};
    if(os_mbuf_copydata(ctxt->om,0,length,command))return BLE_ATT_ERR_UNLIKELY;
    unsigned secs=0, size=244, credits=0;
    if(!strcmp(command,"STATUS_NOTIFY 1")) {status_enabled=true;return 0;}
    if(!strcmp(command,"STOP")) {stop();return 0;}
    if(sscanf(command,"%7s %u %u %u",direction,&secs,&size,&credits)<2 ||
       secs<1 || secs>120 || size<1 || size>sizeof(payload) ||
       (strcmp(direction,"TX") && strcmp(direction,"RX") && strcmp(direction,"RXC")))return BLE_ATT_ERR_UNLIKELY;
    if(!strcmp(direction,"RXC") && (!credits || credits>32 || !subscribed || !status_enabled))return BLE_ATT_ERR_UNLIKELY;
    if(!strcmp(direction,"TX") && !subscribed)return BLE_ATT_ERR_UNLIKELY;
    block=size;mode=!strcmp(direction,"TX")?1:2;
    sent=received=bad=errors=blocked=acknowledged=queue_peak=0;
    started=now_ms();duration=secs*1000;
    credit_bytes=!strcmp(direction,"RXC")?credits*block:0;status_pending=false;
    printf("[GATT-NB] START mode=%u seconds=%u block=%u credits=%u\r\n",mode,secs,block,credits);
    return 0;
}
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(
    0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x00,0x20,0xf0,0xb1);
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=BLE_UUID16_DECLARE(0xfff1),.access_cb=access,.arg=(void *)1,
         .flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP},
        {.uuid=BLE_UUID16_DECLARE(0xfff2),.access_cb=access,.arg=(void *)2,
         .flags=BLE_GATT_CHR_F_NOTIFY,.val_handle=&tx_handle},
        {.uuid=BLE_UUID16_DECLARE(0xfff3),.access_cb=access,.arg=(void *)3,
         .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE},
        {0}}},{0}};
static bool queue_full(void)
{
    unsigned queued=0;
    ble_hs_lock();
    struct ble_hs_conn *conn=ble_hs_conn_find(connection);
    if(conn) {
        struct os_mbuf_pkthdr *packet;
        STAILQ_FOREACH(packet,&conn->bhc_tx_q,omp_next)queued++;
    }
    ble_hs_unlock();
    if(queued>queue_peak)queue_peak=queued;
    return !conn || queued>=8 || os_msys_num_free()<12;
}
static int notify(const uint8_t *value,unsigned length)
{
    struct os_mbuf *om=ble_hs_mbuf_from_flat(value,length);
    if(!om)return BLE_HS_ENOMEM;
    /* ble_gatts_notify_custom consumes om on all return codes. */
    return ble_gatts_notify_custom(connection,tx_handle,om);
}
static void tick(struct ble_npl_event *event)
{
    (void)event;
    if(connection==BLE_HS_CONN_HANDLE_NONE)return;
    if(mode && now_ms()-started>=duration)stop();
    if(subscribed && status_pending && !queue_full()) {
        uint8_t value[32];status(value);
        if(!notify(value,sizeof(value))) {status_pending=false;acknowledged=received;}
        else blocked++;
    }
    if(mode==1 && subscribed && !status_pending) {
        for(unsigned j=0;j<4;j++) {
            if(queue_full()) {blocked++;break;}
            if(block>ble_att_mtu(connection)-3) {errors++;stop();break;}
            for(unsigned i=0;i<block;i++)payload[i]=(sent+i)%251;
            int rc=notify(payload,block);
            if(rc) {blocked++;if(rc!=BLE_HS_ENOMEM && rc!=BLE_HS_EBUSY) {errors++;stop();}break;}
            sent+=block;
        }
    }
    ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(2));
}
int ble_gatt_bench_init(void)
{
    ble_npl_callout_init(&timer,nimble_port_get_dflt_eventq(),tick,NULL);
    int rc=ble_gatts_count_cfg(services);
    if(!rc)rc=ble_gatts_add_svcs(services);
    printf("[GATT-NB] register=%d; GBR1 transport=2, TX queue limit=8\r\n",rc);
    return rc;
}
void ble_gatt_bench_event(struct ble_gap_event *event)
{
    if(event->type==BLE_GAP_EVENT_CONNECT && !event->connect.status) {
        connection=event->connect.conn_handle;mode=0;subscribed=status_enabled=status_pending=false;
        sent=received=bad=errors=blocked=0;block=244;started=now_ms();
        struct ble_gap_upd_params p={.itvl_min=12,.itvl_max=12,.latency=0,.supervision_timeout=400};
        int dle=ble_gap_set_data_len(connection,251,2120);
        int phy=ble_gap_set_prefered_le_phy(connection,2,2,0);
        int interval=ble_gap_update_params(connection,&p);
        printf("[GATT-NB] CONNECT handle=%u DLE=%d PHY=%d interval=%d\r\n",connection,dle,phy,interval);
        ble_npl_callout_reset(&timer,1);
    } else if(event->type==BLE_GAP_EVENT_DISCONNECT) {
        connection=BLE_HS_CONN_HANDLE_NONE;mode=0;subscribed=status_pending=false;ble_npl_callout_stop(&timer);
    } else if(event->type==BLE_GAP_EVENT_SUBSCRIBE && event->subscribe.attr_handle==tx_handle) {
        subscribed=event->subscribe.cur_notify;
        printf("[GATT-NB] notify=%u\r\n",subscribed);
    } else if(event->type==BLE_GAP_EVENT_MTU) {
        printf("[GATT-NB] MTU=%u\r\n",event->mtu.value);
    } else if(event->type==BLE_GAP_EVENT_PHY_UPDATE_COMPLETE) {
        printf("[GATT-NB] PHY status=%d TX=%u RX=%u\r\n",event->phy_updated.status,event->phy_updated.tx_phy,event->phy_updated.rx_phy);
    } else if(event->type==BLE_GAP_EVENT_CONN_UPDATE) {
        struct ble_gap_conn_desc d;
        if(!ble_gap_conn_find(event->conn_update.conn_handle,&d))printf("[GATT-NB] interval=%u latency=%u timeout=%u status=%d\r\n",d.conn_itvl,d.conn_latency,d.supervision_timeout,event->conn_update.status);
    }
}
