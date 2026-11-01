#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DNS.h"
#include "bflb_sec_trng.h"
#include "shell.h"
#if defined(CONFIG_PAN_COEX) || defined(CONFIG_PAN_WIFI_ONLY)
#include "bouffalo_wifi_tcp.h"
#endif

static uint32_t network_random(void)
{
    uint32_t value;
    configASSERT(bflb_trng_readlen((uint8_t *)&value, sizeof(value)) == 0);
    return value;
}
const char *pcApplicationHostnameHook(void) { return "bl618dg"; }
BaseType_t xApplicationGetRandomNumber(uint32_t *value)
{ *value = network_random(); return pdTRUE; }
uint32_t ulApplicationGetNextSequenceNumber(uint32_t source, uint16_t source_port,
                                           uint32_t dest, uint16_t dest_port)
{
    (void)source; (void)source_port; (void)dest; (void)dest_port;
    return network_random();
}

void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t event, NetworkEndPoint_t *endpoint)
{
    #if defined(CONFIG_PAN_COEX) || defined(CONFIG_PAN_WIFI_ONLY)
    bouffalo_wifi_tcp_network_event(event, endpoint);
    #endif
    if (strcmp(endpoint->pxNetworkInterface->pcName, "pan0")) return;
    static bool was_up;
    if (event == eNetworkUp) {
        was_up = true;
        char address[16], gateway[16], dns[16];
        FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulIPAddress, address);
        FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulGatewayAddress, gateway);
        FreeRTOS_inet_ntoa(endpoint->ipv4_settings.ulDNSServerAddresses[0], dns);
        printf("[pan] IP UP address=%s gateway=%s dns=%s\r\n", address, gateway, dns);
    } else if (was_up) { was_up = false; printf("[pan] IP DOWN\r\n"); }
}

void vApplicationPingReplyHook(ePingReplyStatus_t status, uint16_t id)
{ printf("[pan] ping status=%u id=%u\r\n", status, id); }

struct http_job { char host[128]; uint16_t port; uint32_t source; };
static volatile bool http_busy;

static void http_test(void *argument)
{
    struct http_job *job = argument;
    struct freertos_addrinfo hints = {.ai_family = FREERTOS_AF_INET};
    struct freertos_addrinfo *addresses = NULL;
    Socket_t socket = FREERTOS_INVALID_SOCKET;
    if (FreeRTOS_getaddrinfo(job->host, NULL, &hints, &addresses) != 0 || !addresses) {
        printf("[http] DNS FAIL\r\n");
        goto done;
    }
    unsigned tried = 0;
    for (struct freertos_addrinfo *item = addresses; item && tried < 4; item = item->ai_next, ++tried) {
        char ip[16];
        struct freertos_sockaddr peer = *item->ai_addr;
        FreeRTOS_inet_ntoa(peer.sin_address.ulIP_IPv4, ip);
        printf("[http] DNS %s=%s\r\n", job->host, ip);
        socket = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
        if (socket == FREERTOS_INVALID_SOCKET) break;
        TickType_t timeout = pdMS_TO_TICKS(10000);
        FreeRTOS_setsockopt(socket, 0, FREERTOS_SO_RCVTIMEO, &timeout, sizeof(timeout));
        FreeRTOS_setsockopt(socket, 0, FREERTOS_SO_SNDTIMEO, &timeout, sizeof(timeout));
        struct freertos_sockaddr local = { .sin_family = FREERTOS_AF_INET };
        local.sin_address.ulIP_IPv4 = job->source;
        if (FreeRTOS_bind(socket, &local, sizeof(local)) != 0) {
            FreeRTOS_closesocket(socket); socket = FREERTOS_INVALID_SOCKET; break;
        }
        peer.sin_family = FREERTOS_AF_INET;
        peer.sin_port = FreeRTOS_htons(job->port);
        BaseType_t result = FreeRTOS_connect(socket, &peer, sizeof(peer));
        if (!result) break;
        printf("[http] TCP %s:%u FAIL %ld\r\n", ip, job->port, (long)result);
        FreeRTOS_closesocket(socket);
        socket = FREERTOS_INVALID_SOCKET;
    }
    if (socket != FREERTOS_INVALID_SOCKET) {
        char request[256];
        int length = snprintf(request, sizeof(request), "GET / HTTP/1.0\r\nHost: %s:%u\r\nConnection: close\r\n\r\n", job->host, job->port);
        size_t sent = 0;
        while (length > 0 && length < sizeof(request) && sent < (size_t)length) {
            BaseType_t count = FreeRTOS_send(socket, request + sent, length - sent, 0);
            if (count <= 0) break;
            sent += count;
        }
        char response[256] = {0};
        size_t received = 0;
        while (sent == (size_t)length && received < sizeof(response) - 1 && !strstr(response, "\r\n")) {
            BaseType_t count = FreeRTOS_recv(socket, response + received, sizeof(response) - 1 - received, 0);
            if (count <= 0) break;
            received += count;
            response[received] = 0;
        }
        char *end = strstr(response, "\r\n");
        if (end && !strncmp(response, "HTTP/", 5)) {
            *end = 0;
            printf("[http] HTTP response: %s\r\n", response);
        } else printf("[http] HTTP response FAIL\r\n");
        FreeRTOS_closesocket(socket);
    }
done:
    if (addresses) FreeRTOS_freeaddrinfo(addresses);
    free(job);
    http_busy = false;
    vTaskDelete(NULL);
}

static int http_start(int argc, char **argv, const char *interface_name)
{
    if (argc > 3 || http_busy) return -1;
    struct http_job *job = calloc(1, sizeof(*job));
    if (!job) return -1;
    for (NetworkEndPoint_t *endpoint = FreeRTOS_FirstEndPoint(NULL); endpoint;
         endpoint = FreeRTOS_NextEndPoint(NULL, endpoint)) {
        if (!strcmp(endpoint->pxNetworkInterface->pcName, interface_name) && endpoint->bits.bEndPointUp)
            job->source = endpoint->ipv4_settings.ulIPAddress;
    }
    if (!job->source) { printf("[http] %s has no IP\r\n", interface_name); free(job); return -1; }
    const char *host = argc >= 2 ? argv[1] : "example.com";
    if (!*host || strlen(host) >= sizeof(job->host)) { free(job); return -1; }
    strcpy(job->host, host);
    job->port = 80;
    if (argc == 3) {
        char *end;
        unsigned long port = strtoul(argv[2], &end, 10);
        if (*end || !port || port > 65535) { free(job); return -1; }
        job->port = port;
    }
    http_busy = true;
    if (xTaskCreate(http_test, "pan_http", 1536, job, configMAX_PRIORITIES - 4, NULL) != pdPASS) {
        http_busy = false;
        free(job);
        return -1;
    }
    return 0;
}
#ifndef CONFIG_PAN_WIFI_ONLY
static int pan_http(int argc, char **argv) { return http_start(argc, argv, "pan0"); }
SHELL_CMD_EXPORT_ALIAS(pan_http, pan_http, PAN HTTP test: pan_http [hostname [port]].);
#endif
static int wifi_http(int argc, char **argv) { return http_start(argc, argv, "wl1"); }
SHELL_CMD_EXPORT_ALIAS(wifi_http, wifi_http, Wi-Fi HTTP test: wifi_http [hostname [port]].);
