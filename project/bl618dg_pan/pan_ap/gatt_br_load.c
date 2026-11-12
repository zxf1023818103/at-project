/* GATT/ATT on BR/EDR PSM 0x001f. Test service: fff0, RX fff1, TX fff2,
 * CCCD 2902, control/status fff3. All stack operations execute on BTU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"
#include "bluedroid_bouffalo.h"
#include "stack/btu.h"
#include "stack/gatt_api.h"
#include "stack/btm_api.h"
#include "stack/btm_ble_api.h"
#include "stack/l2c_api.h"
#ifdef CONFIG_PAN_GATT_LE
#include "esp_gap_ble_api.h"
static bool le_advertise_pending;
static void le_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT)
        printf("[gatt-le] ADV_START status=%u\r\n",p->adv_start_cmpl.status);
}
static void le_advertise(void)
{
    esp_ble_adv_params_t params = {.adv_int_min=0x30,.adv_int_max=0x60,
        .adv_type=ADV_TYPE_IND,.own_addr_type=BLE_ADDR_TYPE_PUBLIC,
        .channel_map=ADV_CHNL_ALL,.adv_filter_policy=ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY};
    printf("[gatt-le] advertise request=%d\r\n",esp_ble_gap_start_advertising(&params));
    le_advertise_pending=false;
}
#endif
#ifdef CONFIG_PAN_GATT_LE
#define TEST_BEARER BT_TRANSPORT_LE
#define TEST_NAME "BL618DG-GATT-LE"
#else
#define TEST_BEARER BT_TRANSPORT_BR_EDR
#define TEST_NAME "BL618DG-GATT-BR"
#endif
#include "osi/allocator.h"
#include "gatt_br_gzip.h"

static tGATT_IF app_if;
static uint16_t connection = 0xffff, rx_handle, tx_handle, cccd_handle, ctrl_handle;
static uint16_t block_size = 244, cccd;
static uint8_t transport, mode;
static bool gzip_session;
static bool congested;
static bool status_notifications, status_pending;
static uint32_t tx_bytes, rx_bytes, errors, bad_bytes, blocked;
static uint32_t credit_bytes, progress_sent;
static TickType_t started, duration;
static TaskHandle_t worker;
static uint8_t tx_data[512];
static bool discovery_announced;
static TickType_t service_started;
static BD_ADDR connected_peer;
static uint64_t rx_last_us, rx_gap_sum, rx_work_sum, rx_rsp_sum;
static uint32_t rx_packets, rx_responses, rx_gap_max, rx_work_max, rx_rsp_max;

static void timing_report(void)
{
    bluedroid_bouffalo_gatt_timing_report();
    tBTM_PM_MODE power = 0xff;
    if (transport == BT_TRANSPORT_BR_EDR) BTM_ReadPowerMode(connected_peer, &power);
    printf("[gatt-br-time] packets=%lu responses=%lu gap_avg_us=%lu gap_max_us=%lu "
           "work_avg_us=%lu work_max_us=%lu rsp_avg_us=%lu rsp_max_us=%lu power=%u\r\n",
           (unsigned long)rx_packets, (unsigned long)rx_responses,
           (unsigned long)(rx_packets>1 ? rx_gap_sum/(rx_packets-1) : 0), (unsigned long)rx_gap_max,
           (unsigned long)(rx_packets ? rx_work_sum/rx_packets : 0), (unsigned long)rx_work_max,
           (unsigned long)(rx_responses ? rx_rsp_sum/rx_responses : 0), (unsigned long)rx_rsp_max, power);
}

static void active_link(void)
{
#ifdef CONFIG_PAN_GATT_LE
    uint16_t interval=0, latency=0, timeout=0;
    BTM_GetCurrentConnParams(connected_peer, &interval, &latency, &timeout);
    unsigned status=BTM_SetBleDataLength(connected_peer,251);
    unsigned phy=BTM_BleSetPreferPhy(connected_peer,0,0x02,0x02,0);
    unsigned update=L2CA_UpdateBleConnParams(connected_peer,12,12,0,400);
    printf("[gatt-le] request PHY=2M status=%u interval=15ms status=%u\r\n",phy,update);
    printf("[gatt-le] LINK interval=%u latency=%u timeout=%u DLE_request=%u\r\n", interval,latency,timeout,status);
#else
    /* This standalone throughput fixture must not inherit an idle profile's
     * 240 ms sniff interval. Keep the policy local to its single Classic link. */
    uint16_t policy = 0;
    tBTM_PM_PWR_MD power = {.mode = BTM_PM_MD_ACTIVE | BTM_PM_MD_FORCE};
    unsigned policy_status = BTM_SetLinkPolicy(connected_peer, &policy);
    unsigned power_status = BTM_SetPowerMode(BTM_PM_SET_ONLY_ID, connected_peer, &power);
    printf("[gatt-br] ACTIVE policy_status=%u power_status=%u\r\n", policy_status, power_status);
#endif
}

#ifndef CONFIG_PAN_GATT_LE
static void announce_classic_gatt(void)
{
    /* The low-level GATTS API creates SDP records but does not add their UUIDs
     * to BTA's EIR bitmap (which also excludes custom 16-bit UUIDs). Advertise
     * the real GATT/FFF0 services explicitly in this standalone fixture only.
     * Run after BTA's initial name/EIR timers to avoid being overwritten. */
    static const char name[] = "BL618DG-GATT-BR";
    BT_HDR *buffer = osi_calloc(BTM_HCI_EIR_OFFSET + 240);
    if (!buffer) return;
    uint8_t *p = (uint8_t *)buffer + BTM_HCI_EIR_OFFSET;
    *p++ = 2; *p++ = 0x01; *p++ = 0x02; /* general discoverable, BR/EDR supported */
    *p++ = 7; *p++ = 0x03;
    *p++ = 0x00; *p++ = 0x18; /* GAP */
    *p++ = 0x01; *p++ = 0x18; /* Generic Attribute */
    *p++ = 0xf0; *p++ = 0xff; /* actual test service */
    *p++ = sizeof(name); *p++ = 0x09;
    memcpy(p, name, sizeof(name)-1);
    unsigned status = BTM_WriteEIR(buffer, TRUE); /* consumes buffer */
    uint16_t window, interval;
    printf("[gatt-br] EIR status=%u UUIDs=1800,1801,fff0 discoverable=%u connectable=%u\r\n",
           status, BTM_ReadDiscoverability(&window, &interval),
           BTM_ReadConnectability(NULL, NULL));
    discovery_announced = status == BTM_SUCCESS;
}
#endif
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; i++) p[i] = v >> (i * 8); }
static unsigned raw_status(uint8_t *value)
{
    memcpy(value, "GBR1", 4); value[4] = mode; value[5] = transport;
    value[6] = block_size; value[7] = block_size >> 8;
    put32(value+8, tx_bytes); put32(value+12, rx_bytes);
    put32(value+16, errors); put32(value+20, bad_bytes);
    put32(value+24, (xTaskGetTickCount()-started)*portTICK_PERIOD_MS);
    put32(value+28, blocked);
    return 32;
}

static void connected(tGATT_IF id, BD_ADDR peer, uint16_t conn, BOOLEAN up,
                      tGATT_DISCONN_REASON why, tBT_TRANSPORT bearer)
{
    (void)id; (void)peer;
    printf("[gatt-br] CONNECT up=%u conn=%u transport=%u reason=%u\r\n", up, conn, bearer, why);
    if (up && bearer == TEST_BEARER) {
        bluedroid_bouffalo_gatt_control_reset();
        connection = conn; transport = bearer;
        memcpy(connected_peer, peer, sizeof(connected_peer));
        active_link();
    }
    else if (!up && conn == connection) {
        bluedroid_bouffalo_gatt_control_reset();
        gatt_gzip_stop();
#ifdef CONFIG_PAN_GATT_LE
        le_advertise_pending=true;
#endif
        connection = 0xffff; mode = 0; cccd = 0; congested = false;
        status_notifications = status_pending = false;
    }
}
static void complete(uint16_t c, tGATTC_OPTYPE op, tGATT_STATUS st, tGATT_CL_COMPLETE *p)
{ (void)c; (void)op; (void)st; (void)p; }
static void discovery(uint16_t c, tGATT_DISC_TYPE t, tGATT_DISC_RES *p)
{ (void)c; (void)t; (void)p; }
static void discovery_done(uint16_t c, tGATT_DISC_TYPE t, tGATT_STATUS s)
{ (void)c; (void)t; (void)s; }
static void encryption(tGATT_IF id, BD_ADDR peer)
{ (void)id; (void)peer; printf("[gatt-br] encryption complete\r\n"); }
static void congestion(uint16_t c, BOOLEAN full)
{ (void)c; congested = full; bluedroid_bouffalo_gatt_control_congestion(full); }

static void request(uint16_t conn, uint32_t transaction, tGATTS_REQ_TYPE type, tGATTS_DATA *data)
{
    tGATTS_RSP reply = {0};
    tGATT_STATUS status = GATT_SUCCESS;
    bool respond = false;
    uint64_t arrival_us = bflb_mtimer_get_time_us();
    bool time_write = false;
    if (type == GATTS_REQ_TYPE_MTU) {
        printf("[gatt-br] MTU=%u conn=%u\r\n", data->mtu, conn);
        return;
    }
    if (type == GATTS_REQ_TYPE_READ) {
        uint8_t value[80] = {0};
        unsigned size = 0;
        reply.attr_value.handle = data->read_req.handle;
        reply.attr_value.offset = data->read_req.offset;
        respond = true;
        if (data->read_req.handle == ctrl_handle) {
            printf("[gatt-br] STATUS mode=%u tx=%lu rx=%lu\r\n", mode, (unsigned long)tx_bytes, (unsigned long)rx_bytes);
            if(gzip_session) size=gatt_gzip_status(value);
            else size = raw_status(value);
        } else if (data->read_req.handle == cccd_handle) {
            value[0] = cccd; value[1] = cccd >> 8; size = 2;
        } else if (data->read_req.handle == tx_handle || data->read_req.handle == rx_handle) {
            static const char greeting[] = "BL618DG GATT over BR/EDR";
            size = sizeof(greeting)-1;
            memcpy(value, greeting, size);
        } else status = GATT_INVALID_HANDLE;
        if (data->read_req.offset > size) status = GATT_INVALID_OFFSET;
        else {reply.attr_value.len = size-data->read_req.offset;
              memcpy(reply.attr_value.value, value+data->read_req.offset, reply.attr_value.len);}
    } else if (type == GATTS_REQ_TYPE_WRITE) {
        tGATT_WRITE_REQ *w = &data->write_req;
        reply.handle = w->handle; respond = w->need_rsp;
        if (w->is_prep || w->offset) status = GATT_REQ_NOT_SUPPORTED;
        else if (w->handle == cccd_handle && w->len == 2) {
            cccd = w->value[0] | (uint16_t)w->value[1] << 8;
        } else if (w->handle == rx_handle) {
            if (mode == 2) {
                time_write = w->len != 0;
                for (unsigned i = 0; i < w->len; i++) if (w->value[i] != (rx_bytes+i)%251) bad_bytes++;
                rx_bytes += w->len;
                if (credit_bytes && rx_bytes-progress_sent >= credit_bytes) status_pending = true;
            }
        } else if (w->handle == ctrl_handle && w->len < 80) {
            char command[80] = {0}, direction[8] = {0};
            unsigned seconds = 0, block = 244, profile=0, baud=0, level=1, codec=1, credit=0;
            memcpy(command, w->value, w->len);
            if (!strcmp(command, "STATUS_NOTIFY 1")) {status_notifications = true;}
            else if (!strcmp(command, "STOP")) {
                if (mode == 2) timing_report();
                gatt_gzip_stop(); mode = 0; status_pending = status_notifications;
            }
            else if(sscanf(command,"GZ %u %u %u %u %u",&seconds,&profile,&baud,&level,&codec)>=4
                    && transport==BT_TRANSPORT_BR_EDR && (cccd&1)) {
                if(gatt_gzip_start(seconds,profile,baud,level,codec)) {mode=3;gzip_session=true;}
                else status=GATT_ILLEGAL_PARAMETER;
            }
            else if (sscanf(command, "%7s %u %u %u", direction, &seconds, &block, &credit) >= 2
                     && seconds >= 1 && seconds <= 120 && block >= 1 && block <= sizeof(tx_data)
                     && (!strcmp(direction,"TX") || !strcmp(direction,"RX") ||
                         (!strcmp(direction,"RXC") && credit>=1 && credit<=32 && status_notifications && (cccd&1)))
                     && transport == TEST_BEARER && (!strcmp(direction,"RX") || (cccd & 1))) {
                gatt_gzip_stop(); gzip_session=false;
                mode = !strcmp(direction,"TX") ? 1 : 2; block_size = block;
                tx_bytes = rx_bytes = errors = bad_bytes = blocked = 0;
                credit_bytes = !strcmp(direction,"RXC") ? block*credit : 0;
                progress_sent = 0;
                status_pending = false;
                rx_last_us = rx_gap_sum = rx_work_sum = rx_rsp_sum = 0;
                rx_packets = rx_responses = rx_gap_max = rx_work_max = rx_rsp_max = 0;
                bluedroid_bouffalo_gatt_timing_reset(rx_handle);
                started = xTaskGetTickCount(); duration = pdMS_TO_TICKS(seconds*1000);
                active_link();
                printf("[gatt-br] START mode=%u seconds=%u block=%u\r\n", mode, seconds, block);
            } else status = GATT_ILLEGAL_PARAMETER;
        } else status = GATT_INVALID_HANDLE;
    } else if (type == GATTS_REQ_TYPE_WRITE_EXEC) {
        status = GATT_REQ_NOT_SUPPORTED; respond = true;
    }
    uint64_t reply_us = bflb_mtimer_get_time_us();
    if (respond) {
        unsigned result=GATTS_SendRsp(conn, transaction, status, &reply);
        if (!time_write || result != GATT_SUCCESS)
            printf("[gatt-control] RSP ms=%lu transaction=%lu type=%u handle=%u status=%u result=%u mode=%u\r\n",
                   (unsigned long)(bflb_mtimer_get_time_us()/1000),(unsigned long)transaction,type,reply.handle,status,result,mode);
    }
    if (time_write) {
        uint32_t work = bflb_mtimer_get_time_us()-arrival_us;
        rx_packets++; rx_work_sum += work;
        if (work > rx_work_max) rx_work_max = work;
        if (rx_last_us) {
            uint32_t gap = arrival_us-rx_last_us;
            rx_gap_sum += gap;
            if (gap > rx_gap_max) rx_gap_max = gap;
        }
        rx_last_us = arrival_us;
        if (respond) {
            uint32_t response = bflb_mtimer_get_time_us()-reply_us;
            rx_responses++; rx_rsp_sum += response;
            if (response > rx_rsp_max) rx_rsp_max = response;
        }
    }
}

static void serve(void)
{
    /* iOS gives the GAP Device Name priority over the Classic inquiry name. */
    BTM_SetLocalDeviceName(TEST_NAME, BT_DEVICE_TYPE_BLE);
    static const tGATT_CBACK callbacks = {connected, complete, discovery, discovery_done,
                                         request, encryption, congestion};
    tBT_UUID app = {.len=LEN_UUID_128, .uu.uuid128={0x91,0x21,0x78,0x61,0x47,0x41,0x54,0x54}};
    tBT_UUID service = {.len=LEN_UUID_16, .uu.uuid16=0xfff0};
    tBT_UUID characteristic = {.len=LEN_UUID_16};
    tGATTS_ATTR_CONTROL control = {.auto_rsp=0};
    uint8_t initial[32] = {0};
    tGATT_ATTR_VAL value = {.attr_max_len=512,.attr_len=0,.attr_val=initial};
    app_if = GATT_Register(&app, &callbacks); configASSERT(app_if);
    GATT_StartIf(app_if);
    uint16_t handle = GATTS_CreateService(app_if, &service, 0, 10, TRUE); configASSERT(handle);
    characteristic.uu.uuid16=0xfff1;
    rx_handle=GATTS_AddCharacteristic(handle,&characteristic,GATT_PERM_READ|GATT_PERM_WRITE,
                       GATT_CHAR_PROP_BIT_READ|GATT_CHAR_PROP_BIT_WRITE|GATT_CHAR_PROP_BIT_WRITE_NR,&value,&control);
    characteristic.uu.uuid16=0xfff2;
    tx_handle=GATTS_AddCharacteristic(handle,&characteristic,GATT_PERM_READ,
                       GATT_CHAR_PROP_BIT_READ|GATT_CHAR_PROP_BIT_NOTIFY,&value,&control);
    characteristic.uu.uuid16=0x2902; value.attr_max_len=2; value.attr_len=2;
    cccd_handle=GATTS_AddCharDescriptor(handle,GATT_PERM_READ|GATT_PERM_WRITE,&characteristic,&value,&control);
    characteristic.uu.uuid16=0xfff3; value.attr_max_len=80; value.attr_len=0;
    ctrl_handle=GATTS_AddCharacteristic(handle,&characteristic,GATT_PERM_READ|GATT_PERM_WRITE,
                       GATT_CHAR_PROP_BIT_READ|GATT_CHAR_PROP_BIT_WRITE,&value,&control);
    configASSERT(rx_handle && tx_handle && cccd_handle && ctrl_handle);
    unsigned status=GATTS_StartService(app_if,handle,TEST_BEARER);
#ifdef CONFIG_PAN_GATT_LE
    static uint8_t advertising[] = {2,1,6,3,3,0xf0,0xff,16,9,
        'B','L','6','1','8','D','G','-','G','A','T','T','-','L','E'};
    unsigned adv=BTM_BleWriteAdvDataRaw(advertising,sizeof(advertising));
    esp_ble_gap_register_callback(le_gap_event);
    le_advertise();
    printf("[gatt-le] advertising=%u transport=LE ATT_CID=4\r\n",adv);
#endif
    service_started = xTaskGetTickCount();
    printf("[gatt-br] SERVER status=%u service=%u rx=%u tx=%u cccd=%u ctrl=%u bearer=%u\r\n",
            status,handle,rx_handle,tx_handle,cccd_handle,ctrl_handle,TEST_BEARER);
}

static void process(BT_HDR *msg)
{
    osi_free(msg);
    if (!app_if) serve();
#ifdef CONFIG_PAN_GATT_LE
    if (le_advertise_pending) le_advertise();
#endif
#ifndef CONFIG_PAN_GATT_LE
    if (!discovery_announced && xTaskGetTickCount()-service_started >= pdMS_TO_TICKS(5000))
        announce_classic_gatt();
#endif
    if ((mode==1 || mode==2) && xTaskGetTickCount()-started >= duration) {
        if (mode == 2) timing_report();
        printf("[gatt-br] DONE mode=%u tx=%lu rx=%lu errors=%lu bad=%lu\r\n",
                mode,(unsigned long)tx_bytes,(unsigned long)rx_bytes,(unsigned long)errors,(unsigned long)bad_bytes);
        mode=0;
        status_pending = status_notifications;
    }
    bluedroid_bouffalo_gatt_control_flush();
    if (bluedroid_bouffalo_gatt_control_pending()) { xTaskNotifyGive(worker); return; }
    /* Optional terminal status shares the subscribed TX characteristic. The
     * modulo-251 raw payload can never begin with the nonconsecutive "GBR1"
     * signature. Legacy Linux clients do not enable this extension. Queue the
     * status after all payload notifications, with normal congestion handling. */
    if (status_pending && connection != 0xffff && !congested && (cccd & 1)) {
        uint8_t value[32]; raw_status(value);
        unsigned status = GATTS_HandleValueNotification(connection, tx_handle, sizeof(value), value);
        if (status == GATT_SUCCESS || status == GATT_CONGESTED) {
            status_pending = false;
            if (mode == 2) progress_sent = rx_bytes;
            if (status == GATT_CONGESTED) congested = true;
            if (mode == 0)
                printf("[gatt-br] FINAL tx=%lu rx=%lu errors=%lu bad=%lu\r\n",
                       (unsigned long)tx_bytes, (unsigned long)rx_bytes,
                       (unsigned long)errors, (unsigned long)bad_bytes);
        }
    }
    if(mode==3 && connection!=0xffff && !congested && (cccd&1)) {
        for(unsigned j=0;j<4 && !congested;j++) {
            const uint8_t *data;
            unsigned length=gatt_gzip_peek(&data);
            if(!length) break;
            unsigned status=GATTS_HandleValueNotification(connection,tx_handle,length,(uint8_t *)data);
            if(status==GATT_SUCCESS || status==GATT_CONGESTED) {
                gatt_gzip_accept(length);
                if(status==GATT_CONGESTED) congested=true;
            } else {if(status!=GATT_NO_RESOURCES) errors++;break;}
        }
    }
    if (mode == 1 && connection != 0xffff && !congested && (cccd & 1)) {
        for (unsigned j=0;j<4 && !congested;j++) {
            for (unsigned i=0;i<block_size;i++) tx_data[i]=(tx_bytes+i)%251;
            unsigned status=GATTS_HandleValueNotification(connection,tx_handle,block_size,tx_data);
            /* CONGESTED still transfers ownership to L2CAP: count it once. */
            if (status == GATT_SUCCESS || status == GATT_CONGESTED) {
                tx_bytes += block_size;
                if (status == GATT_CONGESTED) {blocked++; congested=true; break;}
            } else {
                if (status == GATT_NO_RESOURCES) blocked++;
                else errors++;
                break;
            }
        }
    }
    xTaskNotifyGive(worker);
}
static void task(void *unused)
{
    (void)unused;
    for (;;) {
        BT_HDR *msg=osi_calloc(sizeof(BT_HDR)+sizeof(post_to_task_hack_t));
        if(msg){msg->event=BTU_POST_TO_TASK_NO_GOOD_HORRIBLE_HACK;
            ((post_to_task_hack_t *)msg->data)->callback=process;
            if(btu_task_post(SIG_BTU_HCI_MSG,msg,0)) ulTaskNotifyTake(pdTRUE,portMAX_DELAY);else osi_free(msg);}
        vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
    }
}
void gatt_br_load_init(void)
{gatt_gzip_init();configASSERT(xTaskCreate(task,"gatt_br",2048,NULL,5,&worker)==pdPASS);}
