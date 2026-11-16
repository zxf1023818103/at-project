/* Standalone NAN control: the IPC callback only copies events; this task owns
 * all state and commands, so it never waits for a reply from its own callback. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "FreeRTOS_CLI.h"
#include "whc_ipc_host_nan.h"
#include "wifi_api_types.h"
#include "nan_pasn.h"
#include "nan_network.h"
#include "nan_wire.h"
#include "nan_event.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtw_nan_struct.h"
#include "rtw_nan_vendor_def.h"
#include "rtw_nan_cmd.h"

#define NAN_SERVICE "_flashstudio._tcp"
#define NAN_HEAP_LOG_INTERVAL_MS 10000u
struct nan_message { uint16_t id, len; uint8_t data[]; };
static QueueHandle_t events;
static volatile unsigned running, received, dropped;
static uint8_t peer_instance, active_ndp;
static uint8_t active_peer[6], active_ndi[6];
static bool datapath_active;
static struct rtw_nan_datapath_request_received_event_data accepted_request;
static bool request_pending;
static uint8_t request_token;
static TickType_t request_started;
void green2_nan_authenticated(const unsigned char *peer, unsigned char type)
{
    printf("[NAN] PASN authenticated type=%u\n", type);
    if (type == 1) {
        struct vendor_tx_followup_info f = {0};
        f.follow_up_type = NAN_TX_FOLLOW_UP_REQ; f.local_inst_id = 1; f.remote_inst_id = peer_instance;
        memcpy(f.remote_mac, peer, 6); f.sec_key_type = 1 << 3;
        printf("[NAN] NIK exchange rc=%d\n", whc_host_nan_cfgvendor_cmd(0x1906, &f, sizeof(f)));
    }
}

static void nan_event(uint16_t id, void *data, uint32_t len)
{
    extern void green2_nan_heap_check(const char *where);
    if (id >= 29 && id <= 34) green2_nan_heap_check("event copy");
    if (len > 2048 || (len && !data)) { dropped++; return; }
    struct nan_message *m = pvPortMalloc(sizeof(*m) + len);
    if (!m) { dropped++; return; }
    m->id = id; m->len = len;
    if (len) memcpy(m->data, data, len);
    if (xQueueSend(events, &m, 0) != pdPASS) {
        mbedtls_platform_zeroize(m->data, len); vPortFree(m); dropped++;
    }
}
static int nandow(uint8_t id, const void *data, size_t len)
{
    struct { uint8_t id, type; uint16_t reserved; uint32_t len; } hdr = {id, 2, 0, len};
    if (len > 1440) return -1;
    uint8_t *cmd = pvPortMalloc(1448);
    if (!cmd) return -1;
    memset(cmd, 0, 1448);
    memcpy(cmd, &hdr, sizeof(hdr)); memcpy(cmd + sizeof(hdr), data, len);
    int rc = whc_host_nan_cfgvendor_cmd(0x1905, cmd, 1448);
    vPortFree(cmd);
    /* SET completes synchronously; the driver only sends a reply for GET. */
    printf("[NAN] command=%u rc=%d\n", id, rc);
    return rc;
}
static void append_info(struct rtw_nan_service_info *s, uint8_t type, const char *value)
{
    size_t n = strlen(value), p = s->length;
    if (n > 255 || p + n + 3 > sizeof(s->info)) return;
    s->info[p++] = type; s->info[p++] = n; s->info[p++] = 0;
    memcpy(s->info + p, value, n); s->length = p + n;
}
static int publish(void)
{
    struct rtw_nan_publish_data *p = pvPortMalloc(sizeof(*p));
    uint8_t hash[32];
    if (!p) return -1;
    memset(p, 0, sizeof(*p));
    mbedtls_sha256((const unsigned char *)NAN_SERVICE, strlen(NAN_SERVICE), hash, 0);
    memcpy(p->service_hash, hash, 6); strcpy((char *)p->name.name, NAN_SERVICE);
    p->publish_type = RTW_NAN_PUBLISH_TYPE_SOLICITED;
    p->solicited_tx_type = 1; p->publish_id = 1;
    p->awake_discovery_window_interval = 1; p->ttl = 0;
    p->flags = RTW_NAN_PUBLISH_FLAG_ADD_DEVICE_CAPABILITY_ATTRIBUTE;
    p->control = RTW_NAN_SDE_CTL_DATAPATH_REQ_REQUIRED | RTW_NAN_SDE_CTL_DATAPATH_TYPE | RTW_NAN_SDE_CTL_SECURITY_REQUIRED;
    p->pairing_enable = 1; p->bstrap_method = 1 << 1;
    p->npk_nik = 1; p->gtk = 1;
    p->cipher_suite_id = (1 << RTW_NAN_CIPHER_ID_NCS_SK_128) | (1 << RTW_NAN_CIPHER_ID_PASN_128);
    memcpy(p->service_specific_info.info, "\x50\x6f\x9a\x02", 4);
    p->service_specific_info.length = 4;
    append_info(&p->service_specific_info, 2, NAN_SERVICE);
    append_info(&p->service_specific_info, 3, "Green2 FreeRTOS");
    /* DNS-SD TXT records used by the reference accessory advertisement. */
    append_info(&p->service_specific_info, 4,
        "\x0e" "vendorName=RTK" "\x15" "pairingName=Green2NAN" "\x12" "modelName=RTL8711F");
    green2_nan_iid(p->interface_identifier);
    memcpy(p->responder_data_address.ether_addr_octet, green2_nan_mac(), 6);
    p->qos.max_service_internal = 1; p->qos.low_latency_required = 1;
    int rc = nandow(RTW_NAN_CMD_PUBLISH, p, sizeof(*p));
    vPortFree(p);
    return rc;
}
static int start(void)
{
    extern int amebagreen2_wifi_ready(void);
    if (!amebagreen2_wifi_ready()) return -1;
    if (running) return 0;
    int rc = whc_host_nan_init();
    printf("[NAN] init rc=%d\n", rc);
    if (rc) return rc;
    rc = whc_host_nan_start(112, RTW_SUPPORT_BAND_2_4G_5G_BOTH);
    printf("[NAN] start rc=%d\n", rc);
    extern int green2_nan_read_mac(void);
    if (!rc) rc = green2_nan_read_mac();
    extern void green2_nan_p256_init(void);
    if (!rc) green2_nan_p256_init();
    if (!rc) rc = green2_pasn_init();
    if (!rc) rc = publish();
    if (!rc) {
        struct vendor_pairing_setpw pw = {0};
        memcpy(pw.pw, "123456", 6); pw.pw_len = 6;
        rc = whc_host_nan_cfgvendor_cmd(0x1907, &pw, sizeof(pw));
        mbedtls_platform_zeroize(&pw, sizeof(pw));
    }
    if (rc) { green2_pasn_stop(); whc_host_nan_stop(); whc_host_nan_deinit(); }
    else { running = 1; printf("[NAN] published %s / Green2 FreeRTOS\n", NAN_SERVICE); }
    return rc;
}
static void datapath_request(const void *data, size_t len)
{
    struct rtw_nan_datapath_request_received_event_data req;
    if (len < sizeof(req)) return;
    memcpy(&req, data, sizeof(req));
    uint8_t token = 0;
    bool authenticated = green2_pasn_peer_authorized(req.initiator_management_address.ether_addr_octet);
    if (req.attribute_list_length > len - sizeof(req) || req.publish_id != 1 ||
        !green2_nan_secure_ndp((const uint8_t *)data + sizeof(req), req.attribute_list_length,
                              req.datapath_id, req.initiator_data_address.ether_addr_octet, &token) ||
        !authenticated) {
        printf("[NAN] rejected NDP request id=%u pub=%u len=%u attrs=%u authenticated=%u\n",
            req.datapath_id, req.publish_id, (unsigned)len, req.attribute_list_length, authenticated); return;
    }
    /* Respond once per transaction. Re-entering the vendor response API for
     * a queued retransmission can replace the security state before Confirm. */
    if (request_pending && token == request_token && req.datapath_id == accepted_request.datapath_id &&
        !memcmp(&req.initiator_management_address, &accepted_request.initiator_management_address, 6) &&
        !memcmp(&req.initiator_data_address, &accepted_request.initiator_data_address, 6) &&
        (datapath_active || (TickType_t)(xTaskGetTickCount() - request_started) < pdMS_TO_TICKS(6000))) {
        printf("[NAN] duplicate NDP request id=%u token=%u\n", req.datapath_id, token);
        return;
    }
    struct rtw_nan_datapath_response rsp = {0};
    rsp.datapath_id = req.datapath_id; rsp.publish_id = req.publish_id;
    rsp.status = RTW_NAN_DATAPATH_ACCEPTED; rsp.cipher_suite_id = RTW_NAN_CIPHER_ID_PASN_128;
    rsp.initiator_data_address = req.initiator_data_address;
    rsp.initiator_management_address = req.initiator_management_address;
    memcpy(rsp.responder_data_address.ether_addr_octet, green2_nan_mac(), 6);
    const uint8_t service[] = {0x50,0x6f,0x9a,2, 0,2,0,0x40,0x1f, 1,1,0,6};
    memcpy(rsp.service_specific_info.info, service, sizeof(service)); rsp.service_specific_info.length = sizeof(service);
    int rc = nandow(RTW_NAN_CMD_DATAPATH_RSP, &rsp, sizeof(rsp));
    if (!rc) {
        accepted_request = req; request_pending = true;
        request_token = token; request_started = xTaskGetTickCount();
    }
    printf("[NAN] NDP response id=%u rc=%d\n", req.datapath_id, rc);
}
static void datapath_confirmed(const void *data, size_t len)
{
    struct vendor_evt_data_confirm_info e;
    if (len != sizeof(e) || !request_pending) return;
    memcpy(&e, data, sizeof(e));
    printf("[NAN] NDP confirmation id=%u status=%u\n", e.ndp_id, e.status);
    if (e.type || e.status || e.ndp_id != accepted_request.datapath_id ||
        memcmp(e.initiator_data_address, accepted_request.initiator_data_address.ether_addr_octet, 6) ||
        memcmp(e.responder_data_address, green2_nan_mac(), 6) ||
        !green2_pasn_peer_authorized(accepted_request.initiator_management_address.ether_addr_octet)) return;
    /* A successful secure-install confirmation carries both authenticated
     * NDIs; event 7 may supply an explicit IPv6 IID afterward. */
    if (datapath_active && active_ndp == e.ndp_id &&
        !memcmp(active_peer, accepted_request.initiator_management_address.ether_addr_octet, 6) &&
        !memcmp(active_ndi, e.initiator_data_address, 6)) return;
    active_ndp = e.ndp_id; datapath_active = true;
    memcpy(active_peer, accepted_request.initiator_management_address.ether_addr_octet, 6);
    memcpy(active_ndi, e.initiator_data_address, 6);
    const uint8_t no_iid[8] = {0};
    green2_nan_net_up(e.responder_data_address, e.initiator_data_address, no_iid);
}
static void datapath_established(const void *data, size_t len)
{
    struct rtw_nan_datapath_established_event_data e = {0};
    size_t prefix = offsetof(struct rtw_nan_datapath_established_event_data, service_specific_info);
    if (len < prefix) return;
    memcpy(&e, data, prefix);
    if (e.pub_id != 1 || !green2_pasn_peer_authorized(e.peer_management_address.ether_addr_octet)) return;
    const uint8_t no_iid[8] = {0};
    if (datapath_active && active_ndp == e.datapath_id &&
        !memcmp(active_peer, e.peer_management_address.ether_addr_octet, 6) &&
        !memcmp(active_ndi, e.initiator_data_address.ether_addr_octet, 6) &&
        !memcmp(e.peer_interface_identifier, no_iid, 8)) return;
    active_ndp = e.datapath_id; datapath_active = true;
    memcpy(active_peer, e.peer_management_address.ether_addr_octet, 6);
    memcpy(active_ndi, e.initiator_data_address.ether_addr_octet, 6);
    green2_nan_net_up(e.responder_data_address.ether_addr_octet, e.initiator_data_address.ether_addr_octet, e.peer_interface_identifier);
}
static void nan_task(void *arg)
{
    (void)arg;
    extern int wifi_port_enable(void);
    int rc = wifi_port_enable();
    if (!rc) rc = start();
    printf("[NAN] standalone startup result=%d\n", rc);
    TickType_t last_heap_log = xTaskGetTickCount();
    for (;;) {
        /* Use elapsed time, rather than queue inactivity, so busy discovery
         * still reports memory. Keep logging out of the RX/IP tasks. */
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - last_heap_log) >= pdMS_TO_TICKS(NAN_HEAP_LOG_INTERVAL_MS)) {
            last_heap_log = now;
            printf("[NAN] AP Available Heap Size: %lu bytes\n",
                (unsigned long)xPortGetFreeHeapSize());
        }
        struct nan_message *m;
        green2_pasn_timers();
        if (xQueueReceive(events, &m, pdMS_TO_TICKS(100)) != pdPASS) continue;
        if (m->id == 0xffff) printf("[NAN] start result=%d\n", start());
        else if (m->id == 0xfffe && running) {
            running = 0; datapath_active = request_pending = false; peer_instance = 0;
            green2_nan_net_down(); green2_pasn_stop(); whc_host_nan_stop(); whc_host_nan_deinit();
        }
        else if (running) {
            received++; printf("[NAN] event=%u len=%u\n", m->id, m->len);
            if (m->id == 29 || m->id == 30) {
                printf("[NAN] PASN rc=%d\n", green2_pasn_event(m->id, m->data, m->len));
                extern void green2_nan_heap_check(const char *where);
                green2_nan_heap_check("PASN done");
            }
            if (m->id == 32 && m->len == sizeof(struct vendor_evt_bstrap_result)) {
                struct vendor_evt_bstrap_result b; memcpy(&b, m->data, sizeof(b)); peer_instance = b.remote_inst_id;
            }
            if (m->id == 4) datapath_request(m->data, m->len);
            if (m->id == 0) datapath_confirmed(m->data, m->len);
            if (m->id == 7) datapath_established(m->data, m->len);
            /* Event 8 carries the NDP ID and peer address. Ignore unrelated
             * teardown events rather than dropping the current interface. */
            if (m->id == 8 && m->len == sizeof(struct rtw_nan_datapath_end_event_data) && datapath_active) {
                struct rtw_nan_datapath_end_event_data e;
                memcpy(&e, m->data, sizeof(e));
                if (e.datapath_id == active_ndp && !memcmp(e.peer_management_address.ether_addr_octet, active_peer, 6)) {
                    datapath_active = false; green2_nan_net_down();
                    if (!memcmp(active_peer, accepted_request.initiator_management_address.ether_addr_octet, 6))
                        request_pending = false;
                }
            }
            if (m->id == 34) printf("[NAN] cache rc=%d\n", green2_pasn_cache_update(m->data, m->len, peer_instance));
        }
        mbedtls_platform_zeroize(m->data, m->len); vPortFree(m);
    }
}
static BaseType_t nan_command(char *out, size_t n, const char *cmd)
{
    if (!strcmp(cmd, "nan start")) { nan_event(0xffff, NULL, 0); snprintf(out, n, "nan: start queued\r\n"); }
    else if (!strcmp(cmd, "nan stop")) { nan_event(0xfffe, NULL, 0); snprintf(out, n, "nan: stop queued\r\n"); }
    else {
        char network[160]; green2_nan_net_status(network, sizeof(network));
        snprintf(out, n, "nan: running=%u events=%u dropped=%u %s\r\n", running, received, dropped, network);
    }
    return pdFALSE;
}
void green2_nan_cli_register(void)
{
    static const CLI_Command_Definition_t def = {"nan", "nan start|stop|status: standalone Wi-Fi Aware\r\n", nan_command, -1};
    events = xQueueCreate(16, sizeof(struct nan_message *));
    configASSERT(events);
    whc_host_nan_register_evt_cb(nan_event);
    configASSERT(xTaskCreate(nan_task, "nan", 4096, NULL, 3, NULL) == pdPASS);
    green2_nan_server_start();
    FreeRTOS_CLIRegisterCommand(&def);
}
