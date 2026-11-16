/* Green2 AP is only a USB-to-radio bridge in this profile. Authentication,
 * pairing decisions, IPv6 and TCP execute on the BL618DG FreeRTOS host. */
#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "wifi_api.h"
#include "whc_ipc_host_nan.h"
#include "nan_platform.h"
#include "nan_network.h"
#include "nan_usb.h"
#include "rtw_nan_vendor_def.h"
#include <stdio.h>
#include <string.h>
static uint8_t radio_mac[6];
static uint8_t response[NAN_USB_MAX_PAYLOAD];
static int initialized;
extern int green2_nan_store(void *, size_t, int);
extern int green2_nan_mgmt(const unsigned char *, size_t);
extern uintptr_t green2_usb_controller_base(void);

uint8_t *green2_nan_mac(void) { return radio_mac; }
void green2_nan_mac_set(const uint8_t mac[6]) { memcpy(radio_mac, mac, 6); }
void green2_nan_net_init(void) {}
void green2_nan_rx(const void *data, size_t length)
{ if (length >= 14 && length <= 1514) nan_usb_send(NAN_USB_ETHERNET, 0, 0, 0, data, length); }
static void event(uint16_t id, void *data, uint32_t size)
{ nan_usb_send(NAN_USB_EVENT, id, 0, 0, data, size); }
static void command_reply(void *data, uint32_t size)
{ nan_usb_send(NAN_USB_COMMAND_REPLY, 0, 0, 0, data, size); }

static void receive(const struct nan_usb_record *r, const void *data)
{
    if (r->type == NAN_USB_ETHERNET) {
        if (r->length >= 14 && r->length <= 1514) green2_nan_send(data, r->length);
        return;
    }
    if (r->type != NAN_USB_RPC) return;
    int rc = -1;
    size_t length = 0;
    uint32_t argument = 0;
    if (r->length >= 4) memcpy(&argument, data, 4);
    switch (r->code) {
    case NAN_USB_HELLO:
        if (!r->length) {
            const uint32_t abi[] = {NAN_USB_VERSION, sizeof(struct rtw_nan_publish_data),
                sizeof(struct rtw_nan_datapath_request_received_event_data),
                sizeof(struct rtw_nan_datapath_response), sizeof(struct rtw_nan_datapath_established_event_data)};
            memcpy(response, abi, sizeof(abi)); length = sizeof(abi);
            rc = green2_nan_radio_ready() ? 0 : -1;
        }
        break;
    case NAN_USB_INIT:
        if (!r->length) {
            if (initialized) { whc_host_nan_stop(); whc_host_nan_deinit(); initialized = 0; }
            rc = whc_host_nan_init(); initialized = rc == 0;
        }
        break;
    case NAN_USB_DEINIT:
        if (!r->length) { rc = whc_host_nan_deinit(); if (!rc) initialized = 0; }
        break;
    case NAN_USB_START:
        if (!r->length) rc = green2_nan_radio_start();
        break;
    case NAN_USB_STOP:
        if (!r->length) rc = whc_host_nan_stop();
        break;
    case NAN_USB_VENDOR:
        if (r->length >= 4 && argument <= UINT16_MAX)
            rc = whc_host_nan_cfgvendor_cmd(argument, (const uint8_t *)data + 4, r->length - 4);
        break;
    case NAN_USB_MGMT:
        rc = green2_nan_mgmt(data, r->length);
        break;
    case NAN_USB_MAC:
        if (!r->length) {
            struct rtw_mac mac;
            rc = wifi_get_mac_address(NAN_WLAN_INDEX, &mac, 0);
            if (!rc) { memcpy(radio_mac, &mac, 6); memcpy(response, &mac, 6); length = 6; }
        }
        break;
    case NAN_USB_STORE_READ:
        if (r->length == 4 && argument && argument <= sizeof(response)) {
            rc = green2_nan_store(response, argument, 0);
            if (!rc) length = argument;
        }
        break;
    case NAN_USB_STORE_WRITE:
        if (r->length) rc = green2_nan_store((void *)data, r->length, 1);
        break;
    case NAN_USB_ECHO:
        memcpy(response, data, r->length); length = r->length; rc = 0;
        break;
    default: break;
    }
    nan_usb_send(NAN_USB_REPLY, r->code, r->id, rc, response, length);
    volatile uint8_t *p = response;
    for (size_t i = 0; i < sizeof(response); ++i) p[i] = 0;
}
static BaseType_t status(char *out, size_t size, const char *line)
{
    (void)line;
    nan_usb_status();
    snprintf(out, size, "nan: USB radio bridge ready=%d initialized=%d (host runs on BL618DG)\r\n",
             green2_nan_radio_ready(), initialized);
    return pdFALSE;
}
static void startup(void *arg)
{
    (void)arg;
    int rc = green2_nan_radio_enable();
    printf("[NAN-USB] Green2 radio startup=%d\n", rc);
    if (!rc) {
        whc_host_nan_register_evt_cb(event);
        whc_host_nan_register_cmd_reply_cb(command_reply);
        nan_usb_init(green2_usb_controller_base(), receive);
    }
    vTaskDelete(NULL);
}
void green2_nan_cli_register(void)
{
    static const CLI_Command_Definition_t def = {"nan", "nan status: USB radio bridge\r\n", status, -1};
    FreeRTOS_CLIRegisterCommand(&def);
    configASSERT(xTaskCreate(startup, "nan_radio", 4096, NULL, 3, NULL) == pdPASS);
}
