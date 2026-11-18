/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "FreeRTOS_CLI.h"
#include "controller.h"
#include "tl3238_bluedroid.h"
#include "esp_bt_main.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "ble_coc_bluedroid.h"
#include "nvs_lfs.h"
#include "lfs_blob.h"
#include <stdio.h>
#include <string.h>

/* An independent echo service exercises the public Bluedroid GATTS API, ACL
 * fragmentation, writes, reads and notifications. CoC remains shared code. */
static uint16_t handles[6];
static uint32_t connections, disconnections, writes, notifications, notify_errors;
static bool subscribed;
static uint16_t primary_uuid = ESP_GATT_UUID_PRI_SERVICE;
static uint16_t declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static uint16_t value_uuid = 0xfff5, cccd_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static uint16_t secure_uuid = 0xfff6;
static uint8_t read_property = ESP_GATT_CHAR_PROP_BIT_READ;
static unsigned authentications, auth_errors;
static uint8_t service_uuid[16] = {
    0xf0,0x92,0xcd,0xb3,0xaf,0x91,0xf4,0xba,0x38,0x4e,0xb3,0x97,0x02,0x10,0xf0,0xb1
};
static uint8_t properties = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE |
    ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static uint8_t initial[] = "hello world", cccd[2];
static const esp_gatts_attr_db_t attributes[] = {
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_uuid, ESP_GATT_PERM_READ,
        sizeof(service_uuid), sizeof(service_uuid), service_uuid}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&declaration_uuid, ESP_GATT_PERM_READ,
        1, 1, &properties}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&value_uuid,
        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 244, sizeof(initial)-1, initial}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&cccd_uuid,
        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, cccd}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&declaration_uuid, ESP_GATT_PERM_READ,
        1, 1, &read_property}},
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&secure_uuid, ESP_GATT_PERM_READ_ENCRYPTED,
        sizeof(initial)-1, sizeof(initial)-1, initial}},
};

static void gatt_event(esp_gatts_cb_event_t event, esp_gatt_if_t interface,
                       esp_ble_gatts_cb_param_t *data)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        configASSERT(data->reg.status == ESP_GATT_OK);
        configASSERT(esp_ble_gatts_create_attr_tab(attributes, interface, 6, 0) == ESP_OK);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        configASSERT(data->add_attr_tab.status == ESP_GATT_OK && data->add_attr_tab.num_handle == 6);
        memcpy(handles, data->add_attr_tab.handles, sizeof(handles));
        configASSERT(esp_ble_gatts_start_service(handles[0]) == ESP_OK);
        break;
    case ESP_GATTS_START_EVT:
        configASSERT(data->start.status == ESP_GATT_OK);
        tl3238_bd_printf("[BD-GATT] echo ready value=%u cccd=%u\r\n", handles[2], handles[3]);
        break;
    case ESP_GATTS_CONNECT_EVT:
        connections++;
        subscribed = false;
        /* Echo subscriptions are session-scoped; Host bonding keys use LFS. */
        configASSERT(esp_ble_gatts_set_attr_value(handles[3], 2, cccd) == ESP_OK);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        disconnections++;
        subscribed = false;
        break;
    case ESP_GATTS_WRITE_EVT:
        if (data->write.is_prep) break;
        if (data->write.handle == handles[3] && data->write.len == 2)
            subscribed = data->write.value[0] == 1 && data->write.value[1] == 0;
        else if (data->write.handle == handles[2]) {
            writes++;
            if (subscribed) {
                esp_err_t rc = esp_ble_gatts_send_indicate(interface, data->write.conn_id,
                    handles[2], data->write.len, data->write.value, false);
                if (rc == ESP_OK) notifications++;
                else notify_errors++;
            }
        }
        break;
    default: break;
    }
}

static BaseType_t status_command(char *out, size_t size, const char *command)
{
    (void)command;
    Tl3238BleControllerStats stats = tl3238_controller_stats;
    snprintf(out, size, "host=Bluedroid status=%u NVS=LittleFS CS=off\r\n"
        "ready=%lu init_error=%lu commands=%lu tx_acl=%lu events=%lu rx_acl=%lu errors=%lu\r\n"
        "connections=%lu disconnections=%lu writes=%lu notifications=%lu notify_errors=%lu\r\n"
        "bonds=%d auth=%u auth_errors=%u commits=%u storage_error=%d\r\n",
        (unsigned)esp_bluedroid_get_status(), (unsigned long)stats.ready,
        (unsigned long)stats.init_error, (unsigned long)stats.commands,
        (unsigned long)stats.tx_acl, (unsigned long)stats.events,
        (unsigned long)stats.rx_acl, (unsigned long)stats.errors,
        (unsigned long)connections, (unsigned long)disconnections, (unsigned long)writes,
        (unsigned long)notifications, (unsigned long)notify_errors,
        esp_ble_get_bond_device_num(),authentications,auth_errors,nvs_lfs_commits(),nvs_lfs_error());
    return pdFALSE;
}
void ble_coc_bluedroid_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p)
{
    if (event==ESP_GAP_BLE_SEC_REQ_EVT)
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,nvs_lfs_error()==0);
    else if (event==ESP_GAP_BLE_AUTH_CMPL_EVT) {
        if (p->ble_security.auth_cmpl.success) authentications++; else auth_errors++;
        tl3238_bd_printf("[BD] auth success=%u reason=%u bonds=%d commits=%u storage_error=%d\r\n",
            p->ble_security.auth_cmpl.success,p->ble_security.auth_cmpl.fail_reason,
            esp_ble_get_bond_device_num(),nvs_lfs_commits(),nvs_lfs_error());
        if (nvs_lfs_error()) esp_ble_gap_disconnect(p->ble_security.auth_cmpl.bd_addr);
    }
}
static BaseType_t storage_command(char *out,size_t n,const char *command)
{
    BaseType_t len=0;
    const char *arg=FreeRTOS_CLIGetParameter(command,1,&len);
    int write=arg && len==4 && !strncmp(arg,"test",4);
    if (arg && !write && !(len==5 && !strncmp(arg,"check",5))) {
        snprintf(out,n,"Usage: storage [test|check]\r\n"); return pdFALSE;
    }
    nvs_handle_t h=0; int rc=0;
    if (arg) {
        static const char expected[]="shared LFS NVS persistence probe v1";
        char actual[sizeof expected]; size_t size=sizeof actual;
        rc=nvs_open("probe",NVS_READWRITE,&h);
        if (!rc && write) rc=nvs_set_blob(h,"payload",expected,sizeof expected);
        if (!rc && write) rc=nvs_commit(h);
        if (!rc) rc=nvs_get_blob(h,"payload",actual,&size);
        if (!rc && (size!=sizeof expected || memcmp(expected,actual,size))) rc=ESP_FAIL;
        nvs_close(h);
    }
    snprintf(out,n,"STORAGE LittleFS ready=%u error=%d commits=%u bonds=%d probe_rc=%d\r\n",
        vfs_lfs_mount()!=NULL,nvs_lfs_error(),nvs_lfs_commits(),esp_ble_get_bond_device_num(),rc);
    return pdFALSE;
}
static void start(void *unused)
{
    (void)unused;
    int rc = vfs_lfs_mount() ? ESP_OK : ESP_FAIL;
    if (!rc) rc = tl3238_bluedroid_hci_attach();
    if (!rc) rc = esp_bluedroid_init();
    if (!rc) rc = esp_bluedroid_enable();
    tl3238_bd_printf("[BD] startup=%d heap=%u NVS=LittleFS\r\n", rc, (unsigned)xPortGetFreeHeapSize());
    configASSERT(rc == ESP_OK);
    configASSERT(esp_ble_gatt_set_local_mtu(247) == ESP_OK);
    configASSERT(esp_ble_gatts_register_callback(gatt_event) == ESP_OK);
    configASSERT(esp_ble_gatts_app_register(1) == ESP_OK);
    uint8_t auth=ESP_LE_AUTH_REQ_SC_BOND, io=ESP_IO_CAP_NONE, key_size=16;
    uint8_t keys=ESP_BLE_ENC_KEY_MASK|ESP_BLE_ID_KEY_MASK;
    configASSERT(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE,&auth,1)==ESP_OK);
    configASSERT(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE,&io,1)==ESP_OK);
    configASSERT(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE,&key_size,1)==ESP_OK);
    configASSERT(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY,&keys,1)==ESP_OK);
    configASSERT(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY,&keys,1)==ESP_OK);
    ble_coc_bluedroid_init();
    vTaskDelete(NULL);
}
void tl3238_ble_start(void)
{
    static const CLI_Command_Definition_t command = {
        "ble", "ble: Bluedroid and Controller status\r\n", status_command, 0
    };
    configASSERT(FreeRTOS_CLIRegisterCommand(&command) == pdPASS);
    static const CLI_Command_Definition_t storage = {
        "storage","storage [test|check]: LFS persistence probe and status\r\n",storage_command,-1
    };
    configASSERT(FreeRTOS_CLIRegisterCommand(&storage)==pdPASS);
    configASSERT(xTaskCreate(start, "bd_start", 1024, NULL, 2, NULL) == pdPASS);
}
