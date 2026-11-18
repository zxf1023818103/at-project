/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "cs_app.h"
#include "controller.h"
#include "semphr.h"
#include "FreeRTOS_CLI.h"
#include "host/ble_hs.h"
#include "host/ble_cs.h"
#include "ble_ras.h"
#include "mbedtls/aes.h"
#include "mbedtls/ecdh.h"
#include "host/ble_hs_hci.h"
#include "nimble/nimble_port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ble_hci_le_cs_rd_loc_supp_cap_rp caps;
static struct ble_npl_event request;
static SemaphoreHandle_t response;
static volatile unsigned busy;
static char operation[16], reply[512];
static uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
static ble_addr_t peer;
extern int tl3238_ble_connect(const ble_addr_t *address);
static unsigned ready, hci_events, results, procedures, steps, errors;
static int last_rc, host_rc = BLE_HS_ENOTSYNCED;
static int32_t requested_offset;
static unsigned data_len, data_steps, data_complete, data_overflow, data_counter, data_generation;
static int data_frequency, data_power;
static unsigned data_abort, data_status;
static uint8_t last_data[TL3238_CS_RAW_SUBEVENT_CAPACITY]
    __attribute__((section(".iram_bss"), aligned(16)));

static int cs_event(struct ble_cs_event *event, void *arg)
{
    (void)arg;
    ble_ras_cs_event(event);
    if (event->type == BLE_CS_EVENT_HCI) hci_events++;
    else if (event->type == BLE_CS_EVENT_ERROR) { last_rc = event->error.status; errors++; }
    else if (event->type == BLE_CS_EVENT_CS_PROCEDURE_COMPLETE) {
        procedures++; last_rc = event->procedure_complete.status;
    } else if (event->type == BLE_CS_EVENT_SUBEVENT_RESULT) {
        results++; steps += event->result.num_steps;
        if (!event->result.continuation) {
            data_generation++;
            data_len = data_steps = data_complete = data_overflow = 0;
            data_counter = event->result.procedure_counter;
            data_frequency = event->result.frequency_compensation;
            data_power = event->result.reference_power;
        }
        if (event->result.steps_len <= sizeof last_data - data_len && !data_overflow) {
            memcpy(last_data + data_len, event->result.steps, event->result.steps_len);
            data_len += event->result.steps_len;
            data_steps += event->result.num_steps;
        } else data_overflow = 1;
        if (event->result.subevent_status != 1) data_complete = 1;
        data_abort = event->result.abort_reason;
        data_status = event->result.subevent_status;
    }
    return 0;
}
static void read_caps(void)
{
    last_rc = host_rc ? host_rc : ble_cs_read_local_capabilities(&caps);
    ready = !last_rc && caps.num_config_supported && (caps.roles_supported & 3);
}
static int selftest_rng(void *arg, unsigned char *p, size_t n)
{
    (void)arg;
    return ble_hs_hci_rand(p, (int)n);
}
static int ecdh_selftest(void)
{
    /* Public P-256 test vector: private scalar 2, peer generator G -> X(2G).
     * Controller RNG is used for blinding, as in the NimBLE pairing backend. */
    static const uint8_t expected[32] = {
        0x7c,0xf2,0x7b,0x18,0x8d,0x03,0x4f,0x7e,0x8a,0x52,0x38,0x03,0x04,0xb5,0x1a,0xc3,
        0xc0,0x89,0x69,0xe2,0x77,0xf2,0x1b,0x35,0xa6,0x0b,0x48,0xfc,0x47,0x66,0x99,0x78};
    uint8_t shared[32];
    mbedtls_ecp_group group; mbedtls_mpi private, secret;
    mbedtls_ecp_group_init(&group); mbedtls_mpi_init(&private); mbedtls_mpi_init(&secret);
    int rc = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (!rc) rc = mbedtls_mpi_lset(&private, 2);
    if (!rc) rc = mbedtls_ecdh_compute_shared(&group, &secret, &group.G, &private, selftest_rng, NULL);
    if (!rc) rc = mbedtls_mpi_write_binary(&secret, shared, sizeof shared);
    if (!rc) rc = memcmp(shared, expected, sizeof shared);
    mbedtls_mpi_free(&secret); mbedtls_mpi_free(&private); mbedtls_ecp_group_free(&group);
    return rc;
}
static void request_cb(struct ble_npl_event *ev)
{
    (void)ev;
    int rc = 0;
    if (!strcmp(operation, "ras_status")) {
        struct ble_ras_status s; ble_ras_status(&s);
        snprintf(reply, sizeof reply,
            "RAS service=%u uuid=0x185b features=0x00000004 client=%u discovering=%u conn=%u client_state=%u\r\n"
            "server_state=%u available=%u bytes=%u counter=%u tx=%u rx=%u overwritten=%u errors=%u rc=%d\r\n"
            "DIST valid=%u status=%d counter=%u mm=%ld raw_mm=%ld offset_mm=%ld pairs=%u channels=%u span_mhz=%u residual_mrad=%u\r\n",
            s.service, s.client_ready, s.discovering, s.conn, s.client_state, s.server_state, s.available, s.bytes,
            s.local_counter, s.tx_segments, s.rx_segments, s.overwritten, s.errors, s.last_rc,
            s.distance.status == 0, s.distance.status, s.distance.counter,
            (long)s.distance.distance_mm, (long)s.distance.raw_mm, (long)s.offset_mm,
            s.distance.pairs, s.distance.channels, s.distance.span_mhz, s.distance.residual_mrad);
    } else if (!strcmp(operation, "ras_selftest")) {
        struct cs_distance_result result = {0};
        rc = ble_ras_run_selftest(&result);
        /* NIST AES-128 KAT also checks the new flash-resident AES tables. */
        static const uint8_t key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        static const uint8_t input[16] = {0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        static const uint8_t expected[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
        uint8_t output[16]; mbedtls_aes_context aes; mbedtls_aes_init(&aes);
        int crypto = mbedtls_aes_setkey_enc(&aes, key, 128);
        if (!crypto) crypto = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, input, output);
        if (!crypto) crypto = memcmp(output, expected, sizeof output);
        mbedtls_aes_free(&aes);
        int ecc = rc ? rc : ecdh_selftest();
        snprintf(reply, sizeof reply, "RAS SELFTEST %s synthetic=1 expected_mm=2500 measured_mm=%ld channels=%u residual_mrad=%u AES=%s ECDH=%s rc=%d ecc_rc=%d\r\n",
            !rc && !crypto && !ecc ? "PASS" : "FAIL", (long)result.distance_mm, result.channels,
            result.residual_mrad, crypto ? "FAIL" : "PASS", ecc ? "FAIL" : "PASS", rc, ecc);
    } else if (!strncmp(operation, "ras_", 4)) {
        if (!strcmp(operation, "ras_discover")) rc = ble_ras_discover();
        else if (!strcmp(operation, "ras_get")) rc = ble_ras_get();
        else if (!strcmp(operation, "ras_offset")) rc = ble_ras_set_offset(requested_offset);
        else rc = BLE_HS_EINVAL;
        snprintf(reply, sizeof reply, "%s rc=%d\r\n", operation, rc);
    } else if (!strcmp(operation, "caps")) {
        read_caps(); rc = last_rc;
        snprintf(reply, sizeof reply,
            "CS caps rc=%d ready=%u configs=%u roles=0x%02x antennas=%u paths=%u modes=0x%02x "
            "rtt=0x%02x sync_phy=0x%02x subfeatures=0x%04x tx_snr=0x%02x host_cs=%u\r\n",
            rc, ready, caps.num_config_supported, caps.roles_supported,
            caps.num_antennas_supported, caps.max_antenna_paths_supported,
            caps.optional_modes_supported, caps.rtt_capability,
            caps.optional_cs_sync_phys_supported, caps.optional_subfeatures_supported,
            caps.optional_tx_snr_capability, host_rc == 0);
    } else {
        if (!strcmp(operation, "connect") || !strcmp(operation, "connect-random")) rc = tl3238_ble_connect(&peer);
        else if (handle == BLE_HS_CONN_HANDLE_NONE) rc = BLE_HS_ENOTCONN;
        else if (!strcmp(operation, "pair")) rc = ble_gap_security_initiate(handle);
        else if (!strcmp(operation, "remote")) rc = ble_cs_read_remote_capabilities(handle);
        else if (!strcmp(operation, "reflector")) rc = ble_cs_set_default_settings(handle, 2);
        else if (!strcmp(operation, "start")) {
            struct ble_cs_initiator_procedure_start_params p = {handle, cs_event, NULL};
            rc = ble_cs_initiator_procedure_start(&p);
        } else if (!strcmp(operation, "stop")) rc = ble_cs_initiator_procedure_terminate(handle);
        else if (!strcmp(operation, "remove")) rc = ble_cs_remove_config(handle, 0);
        else if (!strcmp(operation, "security")) rc = ble_cs_security_enable(handle);
        else rc = BLE_HS_EINVAL;
        snprintf(reply, sizeof reply, "CS %s rc=%d (0=accepted; async result in cs status)\r\n", operation, rc);
    }
    last_rc = rc;
    xSemaphoreGive(response);
    taskENTER_CRITICAL(); busy = 0; taskEXIT_CRITICAL();
}
static BaseType_t send_request(char *out, size_t n, const char *op, const ble_addr_t *address, int32_t offset)
{
    taskENTER_CRITICAL();
    if (busy) { taskEXIT_CRITICAL(); snprintf(out, n, "CS command busy\r\n"); return pdFALSE; }
    busy = 1;
    taskEXIT_CRITICAL();
    xSemaphoreTake(response, 0);
    strcpy(operation, op);
    if (address) peer = *address;
    requested_offset = offset;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &request);
    if (xSemaphoreTake(response, pdMS_TO_TICKS(5000)) == pdPASS) snprintf(out, n, "%s", reply);
    else snprintf(out, n, "CS command timed out\r\n");
    return pdFALSE;
}
static BaseType_t ras_command(char *out, size_t n, const char *command)
{
    char op[16] = "status", arg[16] = {0}, extra[2];
    int args = sscanf(command, "%*s %15s %15s %1s", op, arg, extra);
    int32_t offset = 0;
    if (!strcmp(op, "offset")) {
        char *end; long value = strtol(arg, &end, 10);
        if (args != 2 || end == arg || *end || value < -10000 || value > 10000) goto usage;
        offset = value;
    } else if (args > 1 || (strcmp(op, "status") && strcmp(op, "discover") && strcmp(op, "get") && strcmp(op, "selftest"))) goto usage;
    char action[16]; snprintf(action, sizeof action, "ras_%.11s", op);
    return send_request(out, n, action, NULL, offset);
usage:
    snprintf(out, n, "Usage: ras [status|discover|get|selftest|offset <-10000..10000 mm>]\r\n");
    return pdFALSE;
}
static BaseType_t cs_command(char *out, size_t n, const char *command)
{
    char op[16] = "status", extra[24] = {0}, trailing[2];
    int args = sscanf(command, "%*s %15s %23s %1s", op, extra, trailing);
    int connect = !strcmp(op, "connect") || !strcmp(op, "connect-random");
    ble_addr_t address = {.type = !strcmp(op, "connect-random") ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC};
    if (connect && args == 2 && strlen(extra) == 12 && strspn(extra, "0123456789abcdefABCDEF") == 12) {
        for (unsigned i = 0; i < 6; i++) {
            char byte[] = {extra[2*i], extra[2*i+1], 0};
            address.val[5-i] = strtoul(byte, NULL, 16);
        }
    } else if (args > 1 || connect) {
        snprintf(out, n, "Usage: cs [status|caps|pair|remote|reflector|start|stop|remove|security|data]\r\n");
        return pdFALSE;
    }
    if (!strcmp(op, "status")) {
        struct ble_gap_conn_desc desc;
        int encrypted = !ble_gap_conn_find(handle, &desc) && desc.sec_state.encrypted;
        snprintf(out, n,
            "CS ready=%u conn=%u encrypted=%d rc=%d mode=PBR antenna=1 host_cs=%u host_rc=%d\r\n"
            "hci_events=%u result_fragments=%u procedures=%u steps=%u errors=%u\r\n"
            "controller_cs=%lu calibration=%lu irq=%lu events=%lu drops=%lu\r\n"
            "data counter=%u bytes=%u steps=%u complete=%u overflow=%u status=%u abort=%u freq=%d power=%d\r\n"
            "RTOS=TIMER1 CS=TIMER0 BLE=STIMER; RAS=on-demand distance=PBR (ras for status)\r\n",
            ready, handle, encrypted, last_rc, host_rc == 0, host_rc, hci_events, results, procedures, steps, errors,
            (unsigned long)tl3238_controller_stats.cs_ready,
            (unsigned long)tl3238_controller_stats.cs_calibration,
            (unsigned long)tl3238_controller_stats.cs_irqs,
            (unsigned long)tl3238_controller_stats.cs_events,
            (unsigned long)tl3238_controller_stats.cs_drops,
            data_counter, data_len, data_steps, data_complete, data_overflow,
            data_status, data_abort, data_frequency, data_power);
        return pdFALSE;
    }
    if (!strcmp(op, "data")) {
        /* Chunked CLI response; complete subevent remains stable until next one. */
        static unsigned offset, generation;
        taskENTER_CRITICAL();
        if (!data_complete || data_overflow) {
            offset = 0;
            snprintf(out, n, "CS data unavailable: complete=%u overflow=%u\r\n", data_complete, data_overflow);
            taskEXIT_CRITICAL();
            return pdFALSE;
        }
        if (!offset) generation = data_generation;
        if (generation != data_generation) {
            offset = 0;
            snprintf(out, n, "CS data changed; retry after cs stop\r\n");
            taskEXIT_CRITICAL();
            return pdFALSE;
        }
        unsigned count = data_len - offset;
        if (count > 80) count = 80;
        uint8_t bytes[80];
        unsigned counter = data_counter, total = data_len;
        memcpy(bytes, last_data + offset, count);
        taskEXIT_CRITICAL();
        int at = snprintf(out, n, "CS data counter=%u offset=%u hex=", counter, offset);
        for (unsigned i = 0; i < count && (size_t)at + 4 < n; i++) at += snprintf(out + at, n - at, "%02x", bytes[i]);
        snprintf(out + at, n - at, "\r\n");
        offset += count;
        if (offset < total) return pdTRUE;
        offset = 0;
        return pdFALSE;
    }
    const char *ops[] = {"caps", "pair", "remote", "reflector", "start", "stop", "remove", "security", "connect", "connect-random"};
    unsigned i;
    for (i = 0; i < sizeof ops / sizeof ops[0]; i++) if (!strcmp(op, ops[i])) break;
    if (i == sizeof ops / sizeof ops[0]) {
        snprintf(out, n, "Unknown cs command\r\n"); return pdFALSE;
    }
    return send_request(out, n, op, &address, 0);
}
void cs_app_init(void)
{
    memset(last_data, 0, sizeof last_data);
    response = xSemaphoreCreateBinary();
    configASSERT(response);
    ble_npl_event_init(&request, request_cb, NULL);
    struct ble_cs_reflector_setup_params p = {cs_event, NULL};
    configASSERT(ble_cs_reflector_setup(&p) == 0);
    static const CLI_Command_Definition_t cmd = {"cs", "cs [status|caps|pair|remote|reflector|start|stop|remove|security|data]\r\ncs connect[-random] <12-hex-address>: connect CS peer\r\n", cs_command, -1};
    configASSERT(FreeRTOS_CLIRegisterCommand(&cmd) == pdPASS);
    static const CLI_Command_Definition_t ras_cmd = {"ras", "ras [status|discover|get|selftest|offset <mm>]: RAS and distance\r\n", ras_command, -1};
    configASSERT(FreeRTOS_CLIRegisterCommand(&ras_cmd) == pdPASS);
}
void cs_app_sync(void)
{
    ble_cs_reset(BLE_HS_CONN_HANDLE_NONE);
    host_rc = ble_cs_enable_host_support();
    read_caps();
}
void cs_app_connect(uint16_t connection)
{
    handle = connection;
    last_rc = ble_cs_set_default_settings(handle, 2);
}
void cs_app_disconnect(uint16_t connection)
{
    ble_cs_reset(connection);
    handle = BLE_HS_CONN_HANDLE_NONE;
    data_len = data_steps = data_complete = data_overflow = 0;
}
