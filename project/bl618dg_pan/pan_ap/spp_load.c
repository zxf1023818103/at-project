/* Standard SPP 1.2 service using Bluedroid SDP and RFCOMM directly.
 * All stack operations run on BTU. No PAN, TCP or ESP VFS is involved. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"
#include "stack/btu.h"
#include "stack/port_api.h"
#include "stack/sdp_api.h"
#include "stack/btm_api.h"
#include "osi/allocator.h"

static uint16_t port;
static bool connected;
static unsigned mode; /* 1: board TX, 2: board RX */
static uint32_t bytes, bad_bytes, blocked, errors;
static TickType_t started, duration, first_rx, last_rx;
static TaskHandle_t worker;
static uint8_t payload[990];
static volatile unsigned request;
static unsigned requested_mode, requested_seconds;

static int received(uint16_t handle, void *data, uint16_t len)
{
    (void)handle;
    if (mode == 2) {
        if (!bytes) first_rx = xTaskGetTickCount();
        last_rx = xTaskGetTickCount();
        const uint8_t *p = data;
        for (unsigned i = 0; i < len; i++)
            if (p[i] != (uint8_t)((bytes + i) % 251)) bad_bytes++;
        bytes += len;
    }
    return 1;
}

static void management(uint32_t code, uint16_t handle, void *data)
{
    (void)data;
    connected = code == PORT_SUCCESS;
    printf("[spp] link code=%lu handle=%u connected=%u\r\n",
           (unsigned long)code, handle, connected);
    if (connected) {
        port = handle;
        PORT_SetDataCallback(port, received);
    }
}

static void serve(void)
{
    uint16_t service = UUID_SERVCLASS_SERIAL_PORT, browse = UUID_SERVCLASS_PUBLIC_BROWSE_GROUP;
    uint32_t rec = SDP_CreateRecord();
    tSDP_PROTOCOL_ELEM proto[2] = {0};
    proto[0].protocol_uuid = UUID_PROTOCOL_L2CAP;
    proto[1].protocol_uuid = UUID_PROTOCOL_RFCOMM;
    proto[1].num_params = 1;
    proto[1].params[0] = 1;
    const char name[] = "BL618DG SPP throughput";
    bool ok = rec && SDP_AddServiceClassIdList(rec, 1, &service)
        && SDP_AddProtocolList(rec, 2, proto)
        && SDP_AddProfileDescriptorList(rec, service, 0x0102)
        && SDP_AddUuidSequence(rec, ATTR_ID_BROWSE_GROUP_LIST, 1, &browse)
        && SDP_AddLanguageBaseAttrIDList(rec, LANG_ID_CODE_ENGLISH, LANG_ID_CHAR_ENCODE_UTF8, LANGUAGE_BASE_ID)
        && SDP_AddAttribute(rec, ATTR_ID_SERVICE_NAME, TEXT_STR_DESC_TYPE, sizeof(name)-1, (uint8_t *)name);
    ok = ok && BTM_SetSecurityLevel(FALSE, name, BTM_SEC_SERVICE_SERIAL_PORT,
        BTM_SEC_IN_AUTHENTICATE | BTM_SEC_IN_ENCRYPT, BT_PSM_RFCOMM, BTM_SEC_PROTO_RFCOMM, 1);
    BD_ADDR any = {0};
    int rc = ok ? RFCOMM_CreateConnection(service, 1, TRUE, sizeof(payload), any, &port, management) : -1;
    if (!rc) PORT_SetDataCallback(port, received);
    printf("[spp] SERVER status=%d scn=1 handle=%u sdp=%lu\r\n", rc, port, (unsigned long)rec);
}

static void process(BT_HDR *msg)
{
    osi_free(msg);
    taskENTER_CRITICAL();
    unsigned action = request;
    unsigned next_mode = requested_mode, seconds = requested_seconds;
    request = 0;
    taskEXIT_CRITICAL();
    if (action == 1) serve();
    if (action == 2) {
        if (!connected || mode) {
            printf("[spp] start rejected connected=%u mode=%u\r\n", connected, mode);
        } else {
            mode = next_mode;
            bytes = bad_bytes = blocked = errors = 0;
            first_rx = last_rx = 0;
            started = xTaskGetTickCount();
            duration = pdMS_TO_TICKS(seconds * 1000);
            printf("[spp] START mode=%s seconds=%u\r\n", mode == 1 ? "tx" : "rx", seconds);
        }
    }
    if (mode) {
        TickType_t elapsed = xTaskGetTickCount() - started;
        if (!connected || elapsed >= duration || action == 3) {
            printf("[spp] RESULT mode=%s bytes=%lu elapsed_ms=%lu bad=%lu blocked=%lu errors=%lu connected=%u rx_span_ms=%lu\r\n",
                mode == 1 ? "tx" : "rx", (unsigned long)bytes,
                (unsigned long)(elapsed * portTICK_PERIOD_MS), (unsigned long)bad_bytes,
                (unsigned long)blocked, (unsigned long)errors, connected,
                (unsigned long)((last_rx - first_rx) * portTICK_PERIOD_MS));
            mode = 0;
        } else if (mode == 1) {
            /* Bound each BTU turn so controller RX and credit events can run. */
            for (unsigned j = 0; j < 4; j++) {
                for (unsigned i = 0; i < sizeof(payload); i++) payload[i] = (bytes + i) % 251;
                uint16_t written = 0;
                int rc = PORT_WriteData(port, (char *)payload, sizeof(payload), &written);
                bytes += written;
                if (rc != PORT_SUCCESS) { errors++; break; }
                if (written < sizeof(payload)) { blocked++; break; }
            }
        }
    }
    xTaskNotifyGive(worker);
}

static void task(void *unused)
{
    (void)unused;
    request = 1;
    for (;;) {
        BT_HDR *msg = osi_calloc(sizeof(BT_HDR) + sizeof(post_to_task_hack_t));
        if (msg) {
            msg->event = BTU_POST_TO_TASK_NO_GOOD_HORRIBLE_HACK;
            ((post_to_task_hack_t *)msg->data)->callback = process;
            if (btu_task_post(SIG_BTU_HCI_MSG, msg, 0)) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            else osi_free(msg);
        }
        vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
    }
}

void spp_load_init(void)
{
    configASSERT(xTaskCreate(task, "spp_load", 1536, NULL, 5, &worker) == pdPASS);
}

static int load(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "stop")) {
        taskENTER_CRITICAL(); request = 3; taskEXIT_CRITICAL(); return 0;
    }
    if (argc != 3 || (strcmp(argv[1], "tx") && strcmp(argv[1], "rx"))) {
        printf("usage: spp_load tx|rx seconds (1..300), or spp_load stop\r\n"); return -1;
    }
    unsigned sec = strtoul(argv[2], NULL, 10);
    if (!sec || sec > 300) return -1;
    taskENTER_CRITICAL();
    if (request) { taskEXIT_CRITICAL(); return -1; }
    requested_mode = !strcmp(argv[1], "tx") ? 1 : 2;
    requested_seconds = sec;
    request = 2;
    taskEXIT_CRITICAL();
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(load, spp_load, Run standalone SPP throughput measurement.);
