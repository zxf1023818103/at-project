/* Opt-in Bluedroid LE CoC benchmark. All channel state and stack calls run
 * on BTU; congestion callbacks never send recursively. Same L2C1 wire format
 * as the existing NimBLE fixture for receiver-confirmed comparisons. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stack/btu.h"
#include "stack/gatt_api.h"
#include "stack/btm_api.h"
#include "stack/btm_ble_api.h"
#include "stack/l2c_api.h"
#include "esp_gap_ble_api.h"
#include "osi/allocator.h"
#include "l2c_int.h" /* BTU-local queue bound; upstream CoC credits do not bound ACL backlog. */

#define COC_PSM 0x81
#define MAX_DATA 4096
#define RX_MTU 2048
#define TEST_NAME "BL618DG-CoC-BD"
static const uint8_t service_uuid[] = {
    0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x01,0x10,0xf0,0xb1
};
static uint16_t channel, peer_mtu, peer_mps, psm_handle;
static tGATT_IF app_if;
static bool closing, advertise_pending;
static TaskHandle_t worker;
static uint8_t rx_frame[16+MAX_DATA], tx_frame[16+MAX_DATA];
static unsigned rx_have, rx_need=16, tx_size, tx_offset, block=1024, phase;
static uint32_t queue_peak, queue_pauses;
static uint32_t rx_bytes, tx_bytes, bad, errors, seconds, started, deadline;
static uint32_t now_ms(void) { return xTaskGetTickCount()*portTICK_PERIOD_MS; }
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
    printf("[CoC-BD] ERROR %s phase=%u rx=%lu tx=%lu bad=%lu\r\n",reason,phase,(unsigned long)rx_bytes,(unsigned long)tx_bytes,(unsigned long)bad);
    phase=0;errors++;closing=true;
    if(channel)L2CA_LECocDisconnect(channel);
}
static void consume(void)
{
    unsigned type=rx_frame[4], length=rx_frame[6]|(unsigned)rx_frame[7]<<8;
    uint32_t a=get32(rx_frame+8), b=get32(rx_frame+12);
    if(type==1 || type==2) {
        if(phase || tx_size || a<1 || a>120 || b<1 || b>MAX_DATA) {failure("invalid START");return;}
        rx_bytes=tx_bytes=bad=errors=0; seconds=a;block=b;
        started=now_ms();deadline=started+seconds*1000;phase=type;
        printf("[CoC-BD] START %s seconds=%lu block=%u\r\n",type==1?"RX":"TX",(unsigned long)seconds,block);
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
        printf("[CoC-BD] RX END bytes=%lu ms=%lu bad=%lu errors=%lu\r\n",(unsigned long)rx_bytes,(unsigned long)elapsed,(unsigned long)bad,(unsigned long)errors);
    } else if(type==5 && phase==3 && length==8) {
        if(a!=tx_bytes || b || get32(rx_frame+20))errors++;
        if(tx_size) {failure("confirmation backlog");return;}
        frame(7,tx_bytes,errors,NULL,0);phase=0;
        printf("[CoC-BD] ACL queue peak=%lu pauses=%lu heap=%u\r\n",(unsigned long)queue_peak,(unsigned long)queue_pauses,(unsigned)xPortGetFreeHeapSize());
        printf("[CoC-BD] TX ACK bytes=%lu peer=%lu bad=%lu errors=%lu\r\n",(unsigned long)tx_bytes,(unsigned long)a,(unsigned long)b,(unsigned long)errors);
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
static void connect_ind(BD_ADDR peer, uint16_t cid, uint16_t psm, uint8_t id)
{
    tL2CAP_LE_CFG_INFO cfg = {.mtu=RX_MTU, .mps=247, .credits=24};
    unsigned result = channel || psm!=COC_PSM ? L2CAP_CONN_NO_RESOURCES : L2CAP_CONN_OK;
    printf("[CoC-BD] ACCEPT cid=%04x psm=%u result=%u\r\n",cid,psm,result);
    if(!L2CA_ConnectLECocRsp(peer,id,cid,result,0,&cfg))
        printf("[CoC-BD] accept response failed\r\n");
}
static void connect_cfm(uint16_t cid, uint16_t result)
{
    if(result) {printf("[CoC-BD] connect failed=%u\r\n",result);return;}
    tL2CAP_LE_CFG_INFO peer;
    if(!L2CA_GetPeerLECocConfig(cid,&peer) || !peer.mtu || peer.mps<23) {
        L2CA_LECocDisconnect(cid);return;
    }
    channel=cid;peer_mtu=peer.mtu;peer_mps=peer.mps;queue_peak=queue_pauses=0;phase=rx_have=tx_size=tx_offset=0;rx_need=16;closing=false;
    printf("[CoC-BD] OPEN PSM=%u CID=%04x MTU=%u/%u MPS=247/%u credits=24/%u transport=LE\r\n",
           COC_PSM,cid,RX_MTU,peer.mtu,peer.mps,peer.credits);
}
static void disconnect_ind(uint16_t cid, BOOLEAN ack)
{
    (void)ack;
    printf("[CoC-BD] CLOSED cid=%04x phase=%u\r\n",cid,phase);
    if(cid==channel) {channel=0;phase=rx_have=tx_size=tx_offset=0;rx_need=16;}
}
static void congestion(uint16_t cid, BOOLEAN full)
{
    /* The stack owns its congestion state. Query it from the next BTU tick;
     * never send inside this synchronous callback. */
    (void)cid;(void)full;
}
static void data_ind(uint16_t cid, BT_HDR *sdu)
{
    if(cid==channel && !closing)receive_bytes((uint8_t *)(sdu+1)+sdu->offset,sdu->len);
    osi_free(sdu); /* callback transfers RX SDU ownership to application */
}
static void advertise(void)
{
    esp_ble_adv_params_t params = {.adv_int_min=0x30,.adv_int_max=0x60,
        .adv_type=ADV_TYPE_IND,.own_addr_type=BLE_ADDR_TYPE_PUBLIC,
        .channel_map=ADV_CHNL_ALL,.adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY};
    printf("[CoC-BD] advertising request=%d\r\n",esp_ble_gap_start_advertising(&params));
    advertise_pending=false;
}
static void le_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    if(event==ESP_GAP_BLE_ADV_START_COMPLETE_EVT)
        printf("[CoC-BD] ADV status=%u\r\n",p->adv_start_cmpl.status);
    else if(event==ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT)
        printf("[CoC-BD] PHY status=%u TX=%u RX=%u\r\n",p->phy_update.status,p->phy_update.tx_phy,p->phy_update.rx_phy);
    else if(event==ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT)
        printf("[CoC-BD] interval=%u latency=%u timeout=%u status=%u\r\n",
               p->update_conn_params.conn_int,p->update_conn_params.latency,p->update_conn_params.timeout,p->update_conn_params.status);
}
static void connected(tGATT_IF id, BD_ADDR peer, uint16_t conn, BOOLEAN up,
                      tGATT_DISCONN_REASON why, tBT_TRANSPORT bearer)
{
    (void)id;(void)conn;
    printf("[CoC-BD] GAP up=%u transport=%u reason=%u\r\n",up,bearer,why);
    if(up && bearer==BT_TRANSPORT_LE) {
        unsigned dle=BTM_SetBleDataLength(peer,251);
        unsigned phy=BTM_BleSetPreferPhy(peer,0,2,2,0);
        unsigned interval=L2CA_UpdateBleConnParams(peer,12,12,0,400);
        printf("[CoC-BD] params request DLE=%u PHY=%u interval=%u\r\n",dle,phy,interval);
    } else if(!up && bearer==BT_TRANSPORT_LE) {
        channel=0;phase=rx_have=tx_size=tx_offset=0;rx_need=16;advertise_pending=true;
    }
}
static void request(uint16_t conn, uint32_t transaction, tGATTS_REQ_TYPE type, tGATTS_DATA *data)
{
    if(type!=GATTS_REQ_TYPE_READ)return;
    tGATTS_RSP reply={0};
    reply.attr_value.handle=data->read_req.handle;
    reply.attr_value.offset=data->read_req.offset;
    tGATT_STATUS status=GATT_SUCCESS;
    if(data->read_req.handle!=psm_handle)status=GATT_INVALID_HANDLE;
    else if(data->read_req.offset>2)status=GATT_INVALID_OFFSET;
    else {
        static const uint8_t value[]={COC_PSM,0};
        reply.attr_value.len=2-data->read_req.offset;
        memcpy(reply.attr_value.value,value+data->read_req.offset,reply.attr_value.len);
    }
    unsigned rc=GATTS_SendRsp(conn,transaction,status,&reply);
    if(rc!=GATT_SUCCESS)printf("[CoC-BD] PSM response failed=%u\r\n",rc);
}
static void serve(void)
{
    static const tL2CAP_APPL_INFO coc_callbacks = {
        .pL2CA_ConnectInd_Cb=connect_ind,.pL2CA_ConnectCfm_Cb=connect_cfm,
        .pL2CA_DisconnectInd_Cb=disconnect_ind,.pL2CA_DataInd_Cb=data_ind,
        .pL2CA_CongestionStatus_Cb=congestion
    };
    unsigned psm=L2CA_RegisterLECoc(COC_PSM,(tL2CAP_APPL_INFO *)&coc_callbacks);
    configASSERT(psm==COC_PSM);
    /* Match the unencrypted NimBLE development fixture. Registration is still
     * required: BTM rejects unknown LE PSMs even when no pairing is requested. */
    BOOLEAN security=BTM_SetSecurityLevel(FALSE,"COC_BENCH",BTM_SEC_SERVICE_GEN_NET,
                                         BTM_SEC_NONE,COC_PSM,BTM_SEC_PROTO_L2CAP,0);
    configASSERT(security);
    static const tGATT_CBACK gatt_callbacks={.p_conn_cb=connected,.p_req_cb=request};
    tBT_UUID uuid={.len=LEN_UUID_128};memcpy(uuid.uu.uuid128,service_uuid,16);
    app_if=GATT_Register(&uuid,&gatt_callbacks);configASSERT(app_if);
    GATT_StartIf(app_if);
    uint16_t handle=GATTS_CreateService(app_if,&uuid,0,4,TRUE);configASSERT(handle);
    tBT_UUID characteristic={.len=LEN_UUID_16,.uu.uuid16=0xfff4};
    tGATTS_ATTR_CONTROL control={.auto_rsp=0};
    uint8_t initial[]={COC_PSM,0};
    tGATT_ATTR_VAL value={.attr_max_len=2,.attr_len=2,.attr_val=initial};
    psm_handle=GATTS_AddCharacteristic(handle,&characteristic,GATT_PERM_READ,
                    GATT_CHAR_PROP_BIT_READ,&value,&control);configASSERT(psm_handle);
    unsigned status=GATTS_StartService(app_if,handle,BT_TRANSPORT_LE);
    BTM_SetLocalDeviceName(TEST_NAME,BT_DEVICE_TYPE_BLE);
    uint8_t adv[21]={2,1,6,17,7};memcpy(adv+5,service_uuid,16);
    uint8_t scan[2+sizeof(TEST_NAME)-1]={sizeof(TEST_NAME),9};
    memcpy(scan+2,TEST_NAME,sizeof(TEST_NAME)-1);
    unsigned ad=BTM_BleWriteAdvDataRaw(adv,sizeof(adv));
    unsigned sr=BTM_BleWriteScanRspRaw(scan,sizeof(scan));
    esp_ble_gap_register_callback(le_gap_event);
    printf("[CoC-BD] SERVER psm=%u mtu=%u GATT=%u adv=%u scan=%u\r\n",psm,RX_MTU,status,ad,sr);
    advertise();
}
static bool tx_pipeline_full(void)
{
    /* CoC credits describe the peer's receive capacity. They do not prevent
     * unlimited SDUs accumulating in this host's link_xmit_data_q. Bound that
     * separate queue before submitting a new SDU, in the same BTU context. */
    tL2C_CCB *ccb=l2cu_find_ccb_by_cid(NULL,channel);
    if(!ccb || !ccb->p_lcb)return true;
    unsigned queued=list_length(ccb->p_lcb->link_xmit_data_q);
    if(queued>queue_peak)queue_peak=queued;
    if(queued>=8) {queue_pauses++;return true;}
    return L2CA_LECocIsCongested(channel);
}
static void process(BT_HDR *message)
{
    osi_free(message);
    if(!app_if)serve();
    if(advertise_pending)advertise();
    if(channel && !closing) {
        if(phase && now_ms()-started>(seconds+15)*1000)failure("test timeout");
        for(unsigned j=0;j<4 && channel && !closing;j++) {
            if(!tx_size && phase==2) {
                if((int32_t)(now_ms()-deadline)>=0) {
                    frame(4,tx_bytes,now_ms()-started,NULL,0);phase=3;
                    printf("[CoC-BD] TX END bytes=%lu ms=%lu\r\n",(unsigned long)tx_bytes,(unsigned long)(now_ms()-started));
                } else {
                    frame(3,tx_bytes,0,NULL,block);
                    for(unsigned i=0;i<block;i++)tx_frame[16+i]=(tx_bytes+i)%251;
                }
            }
            if(!tx_size || tx_pipeline_full() || xPortGetFreeHeapSize()<32768)break;
            unsigned n=tx_size-tx_offset;
            if(n>peer_mtu)n=peer_mtu;
            if(n>peer_mps-2u)n=peer_mps-2u; /* at most one K-frame per submission */
            if(n>1024)n=1024; /* match initial NimBLE SDU cap */
            BT_HDR *sdu=osi_calloc(sizeof(BT_HDR)+n);
            if(!sdu)break;
            sdu->len=n;memcpy(sdu+1,tx_frame+tx_offset,n);
            /* Bluedroid consumes the SDU on ALL return codes, including failure. */
            unsigned rc=L2CA_LECocDataWrite(channel,sdu);
            if(rc==L2CAP_DW_FAILED) {failure("SDU write failed");break;}
            tx_offset+=n;
            if(tx_offset==tx_size) {
                if(tx_frame[4]==3)tx_bytes+=tx_size-16;
                tx_size=tx_offset=0;
            }
        }
    }
    xTaskNotifyGive(worker);
}
static void task(void *unused)
{
    (void)unused;
    for(;;) {
        BT_HDR *message=osi_calloc(sizeof(BT_HDR)+sizeof(post_to_task_hack_t));
        if(message) {
            message->event=BTU_POST_TO_TASK_NO_GOOD_HORRIBLE_HACK;
            ((post_to_task_hack_t *)message->data)->callback=process;
            if(btu_task_post(SIG_BTU_HCI_MSG,message,0))ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
            else osi_free(message);
        }
        vTaskDelay(pdMS_TO_TICKS(2)?pdMS_TO_TICKS(2):1);
    }
}
void bluedroid_coc_bench_init(void)
{
    BaseType_t rc=xTaskCreate(task,"coc_bd",2048,NULL,5,&worker);
    configASSERT(rc==pdPASS);
}
