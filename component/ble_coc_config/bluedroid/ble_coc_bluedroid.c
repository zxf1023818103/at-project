/* Opt-in Bluedroid LE CoC benchmark. All channel state and stack calls run
 * on BTU; congestion callbacks never send recursively. Same L2C1 wire format
 * as the existing NimBLE fixture for receiver-confirmed comparisons. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_coc_config.h"
#include "ble_coc_bluedroid.h"
#include "coc_stream.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stack/btu.h"
#include "stack/gatt_api.h"
#include "stack/btm_api.h"
#include "stack/btm_ble_api.h"
#include "stack/l2c_api.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_device.h"
#include "osi/allocator.h"
#include "l2c_int.h" /* BTU-local queue bound; upstream CoC credits do not bound ACL backlog. */

#define COC_PSM BLE_COC_PSM
#define RX_MTU BLE_COC_RX_MTU
#define TEST_NAME BLE_COC_DEVICE_NAME
static const uint8_t service_uuid[] = {BLE_COC_BLUEDROID_UUID_BYTES};
static uint16_t channel, peer_mtu, peer_mps, psm_handle;
static tGATT_IF app_if;
static bool closing, advertise_pending;
static TaskHandle_t worker;
static CocStream stream;
static uint32_t queue_peak, queue_pauses;
static uint32_t now_ms(void) { return xTaskGetTickCount()*portTICK_PERIOD_MS; }
static void failure(const char *reason)
{
    if(closing)return;
    closing=true;coc_stream_abort(&stream);
    printf("[CoC-BD] ERROR %s phase=%u rx=%lu tx=%lu code=%u\r\n",reason,
           stream.phase,(unsigned long)stream.rx_bytes,(unsigned long)stream.tx_bytes,stream.error);
    if(channel)L2CA_LECocDisconnect(channel);
}
static void receive_bytes(const uint8_t *data,unsigned length)
{
    unsigned old=stream.phase;
    if(coc_stream_feed(&stream,data,length,now_ms())) {failure("stream frame");return;}
    if(old==COC_IDLE && (stream.phase==COC_RX || stream.phase==COC_TX))
        printf("[CoC-BD] START %s seconds=%lu block=%lu\r\n",stream.phase==COC_RX?"RX":"TX",
               (unsigned long)stream.seconds,(unsigned long)stream.block);
}
static void report_result(unsigned type)
{
    const CocResult *r=type==5 ? &stream.upload : &stream.download;
    printf("[CoC-BD] %s bytes=%lu ms=%lu bad=%lu errors=%lu queue_peak=%lu pauses=%lu\r\n",
           type==5?"RX REPORT":"TX ACK",(unsigned long)r->bytes,(unsigned long)r->milliseconds,
           (unsigned long)r->bad,(unsigned long)r->errors,(unsigned long)queue_peak,(unsigned long)queue_pauses);
}
static void connect_ind(BD_ADDR peer, uint16_t cid, uint16_t psm, uint8_t id)
{
    tL2CAP_LE_CFG_INFO cfg = {.mtu=RX_MTU, .mps=L2CAP_LE_COC_MPS, .credits=BLE_COC_RX_CREDITS};
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
    channel=cid;peer_mtu=peer.mtu;peer_mps=peer.mps;queue_peak=queue_pauses=0;coc_stream_init(&stream);closing=false;
    printf("[CoC-BD] OPEN PSM=%u CID=%04x MTU=%u/%u MPS=%u/%u credits=%u/%u transport=LE\r\n",
           COC_PSM,cid,RX_MTU,peer.mtu,L2CAP_LE_COC_MPS,peer.mps,BLE_COC_RX_CREDITS,peer.credits);
}
static void disconnect_ind(uint16_t cid, BOOLEAN ack)
{
    (void)ack;
    printf("[CoC-BD] CLOSED cid=%04x phase=%u\r\n",cid,stream.phase);
    if(cid==channel) {coc_stream_abort(&stream);channel=0;}
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
        .adv_type=ADV_TYPE_IND,.own_addr_type=BLE_COC_USE_STATIC_RANDOM ? BLE_ADDR_TYPE_RANDOM : BLE_ADDR_TYPE_PUBLIC,
        .channel_map=ADV_CHNL_ALL,.adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY};
    printf("[CoC-BD] advertising request=%d\r\n",esp_ble_gap_start_advertising(&params));
    advertise_pending=false;
}
__attribute__((weak)) void ble_coc_bluedroid_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{ (void)event; (void)p; }
static void identity_ready(BT_HDR *message)
{ osi_free(message); advertise(); }
static void le_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    if(event==ESP_GAP_BLE_ADV_START_COMPLETE_EVT)
        printf("[CoC-BD] ADV status=%u\r\n",p->adv_start_cmpl.status);
    else if(event==ESP_GAP_BLE_PHY_UPDATE_COMPLETE_EVT)
        printf("[CoC-BD] PHY status=%u TX=%u RX=%u\r\n",p->phy_update.status,p->phy_update.tx_phy,p->phy_update.rx_phy);
    else if(event==ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT)
        printf("[CoC-BD] interval=%u latency=%u timeout=%u status=%u\r\n",
               p->update_conn_params.conn_int,p->update_conn_params.latency,p->update_conn_params.timeout,p->update_conn_params.status);
    else if(event==ESP_GAP_BLE_SET_STATIC_RAND_ADDR_EVT && BLE_COC_USE_STATIC_RANDOM) {
        configASSERT(p->set_rand_addr_cmpl.status==ESP_BT_STATUS_SUCCESS);
        /* GAP callbacks run on BTC; retain BTU ownership of channel state. */
        BT_HDR *message=osi_calloc(sizeof(BT_HDR)+sizeof(post_to_task_hack_t));
        configASSERT(message);
        message->event=BTU_POST_TO_TASK_NO_GOOD_HORRIBLE_HACK;
        ((post_to_task_hack_t *)message->data)->callback=identity_ready;
        configASSERT(btu_task_post(SIG_BTU_HCI_MSG,message,0));
    }
    ble_coc_bluedroid_gap_event(event,p);
}
static void connected(tGATT_IF id, BD_ADDR peer, uint16_t conn, BOOLEAN up,
                      tGATT_DISCONN_REASON why, tBT_TRANSPORT bearer)
{
    (void)id;(void)conn;
    printf("[CoC-BD] GAP up=%u transport=%u reason=%u\r\n",up,bearer,why);
    if(up && bearer==BT_TRANSPORT_LE) {
        unsigned dle=BTM_SetBleDataLength(peer,251);
        unsigned phy=BTM_BleSetPreferPhy(peer,0,2,2,0);
        unsigned interval=L2CA_UpdateBleConnParams(peer,BLE_COC_CONN_INTERVAL,BLE_COC_CONN_INTERVAL,0,400);
        printf("[CoC-BD] params request DLE=%u PHY=%u interval=%u\r\n",dle,phy,interval);
    } else if(!up && bearer==BT_TRANSPORT_LE) {
        coc_stream_abort(&stream);channel=0;advertise_pending=true;
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
    if (BLE_COC_USE_STATIC_RANDOM) {
        esp_bd_addr_t address;
        const uint8_t *public_address=esp_bt_dev_get_address();
        configASSERT(public_address);
        memcpy(address,public_address,sizeof address); address[0]|=0xc0;
        configASSERT(esp_ble_gap_set_rand_addr(address)==ESP_OK);
        printf("[CoC-BD] static random identity %02x:%02x:%02x:%02x:%02x:%02x\r\n",
            address[0],address[1],address[2],address[3],address[4],address[5]);
    } else advertise();
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
    if(queued>=BLE_COC_TX_QUEUE_LIMIT) {queue_pauses++;return true;}
    return L2CA_LECocIsCongested(channel);
}
static void process(BT_HDR *message)
{
    osi_free(message);
    if(!app_if)serve();
    if(advertise_pending)advertise();
    if(channel && !closing) {
        if(coc_stream_prepare(&stream,now_ms()))failure("protocol timeout");
        for(unsigned j=0;j<BLE_COC_TX_BURST && channel && !closing;j++) {
            if(coc_stream_prepare(&stream,now_ms())) {failure("prepare");break;}
            if(!stream.tx_size || tx_pipeline_full() || xPortGetFreeHeapSize()<BLE_COC_HEAP_RESERVE)break;
            unsigned n=stream.tx_size-stream.tx_offset;
            if(n>peer_mtu)n=peer_mtu;
            if(n>peer_mps-2u)n=peer_mps-2u;
            if(n>BLE_COC_TX_SDU_MAX)n=BLE_COC_TX_SDU_MAX;
            BT_HDR *sdu=osi_calloc(sizeof(BT_HDR)+n);
            if(!sdu)break;
            sdu->len=coc_stream_peek(&stream,sdu+1,n);
            /* Bluedroid consumes the SDU on ALL return codes, including failure. */
            unsigned rc=L2CA_LECocDataWrite(channel,sdu);
            if(rc==L2CAP_DW_FAILED) {failure("SDU write failed");break;}
            unsigned type=stream.tx_header[4];
            coc_stream_sent(&stream,n);
            if(!stream.tx_size && (type==5 || type==7))report_result(type);
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
        vTaskDelay(pdMS_TO_TICKS(BLE_COC_TX_INTERVAL_MS)?pdMS_TO_TICKS(BLE_COC_TX_INTERVAL_MS):1);
    }
}
void ble_coc_bluedroid_init(void)
{
    BaseType_t rc=xTaskCreate(task,"coc_bd",BLE_COC_BLUEDROID_STACK_WORDS,NULL,
                             BLE_COC_BLUEDROID_PRIORITY,&worker);
    configASSERT(rc==pdPASS);
}
