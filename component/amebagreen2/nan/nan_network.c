#include "nan_network.h"
#include "nan_tcp_protocol.h"
#include "at_netif.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_ND.h"
#include "FreeRTOS_Sockets.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
struct eth_drv_sg;
extern int whc_ipc_host_send(int, struct eth_drv_sg *, int, int, void *, uint8_t);
static NetworkInterface_t iface;
static NetworkEndPoint_t endpoint;
static uint8_t own_mac[6], data_mac[6];
static MACAddress_t peer_mac;
static IPv6_Address_t peer_ip;
static volatile BaseType_t link_up;
static volatile uint32_t link_generation;
static unsigned rx_count, tx_count, requests, uploads, downloads, protocol_errors;
static unsigned rx_dropped, tx_dropped;
static TickType_t link_started;
static uint32_t syn_rx_ms, syn_tx_ms;

#ifdef AT_NAN_NET_DIAG
/* Bounded metadata only: no payloads, allocation or printing on the RX/IP tasks.
 * Timestamps are taken at the WHC boundary, not at the socket API. */
static struct {
    BaseType_t active;
    TickType_t started;
    unsigned used, tx_packets, tx_data, rx_packets, send_calls;
    uint32_t whc_ms, whc_max_ms, send_ms, send_max_ms;
    struct {
        uint32_t ms, seq, ack, cost_ms;
        uint16_t bytes;
        uint8_t tx;
    } packet[256];
} net_diag;
static uint32_t read_be32(const uint8_t *p)
{ return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void diag_packet(const uint8_t *p, size_t size, BaseType_t tx,
                        TickType_t tick, TickType_t cost)
{
    if (!net_diag.active || size < 74 || p[12] != 0x86 || p[13] != 0xdd || p[20] != 6) return;
    if (!((p[54] == 0x1f && p[55] == 0x40) || (p[56] == 0x1f && p[57] == 0x40))) return;
    size_t payload = (size_t)p[18] << 8 | p[19];
    size_t header = (size_t)(p[66] >> 4) * 4;
    if (header < 20 || payload < header || payload > size - 54) return;
    taskENTER_CRITICAL();
    if (net_diag.active) {
        if (tx) {
            ++net_diag.tx_packets;
            net_diag.tx_data += payload > header;
            net_diag.whc_ms += cost * portTICK_PERIOD_MS;
            if (cost * portTICK_PERIOD_MS > net_diag.whc_max_ms)
                net_diag.whc_max_ms = cost * portTICK_PERIOD_MS;
        } else ++net_diag.rx_packets;
        if (net_diag.used < sizeof(net_diag.packet) / sizeof(net_diag.packet[0])) {
            unsigned n = net_diag.used++;
            net_diag.packet[n].ms = (tick - net_diag.started) * portTICK_PERIOD_MS;
            net_diag.packet[n].seq = read_be32(p + 58);
            net_diag.packet[n].ack = read_be32(p + 62);
            net_diag.packet[n].cost_ms = cost * portTICK_PERIOD_MS;
            net_diag.packet[n].bytes = payload - header;
            net_diag.packet[n].tx = tx != pdFALSE;
        }
    }
    taskEXIT_CRITICAL();
}
static void diag_begin(void)
{
    taskENTER_CRITICAL();
    memset(&net_diag, 0, sizeof(net_diag));
    net_diag.started = xTaskGetTickCount();
    net_diag.active = pdTRUE;
    taskEXIT_CRITICAL();
}
static void diag_end(enum green2_nan_tcp_result result)
{
    taskENTER_CRITICAL();
    net_diag.active = pdFALSE;
    taskEXIT_CRITICAL();
    if (result != GREEN2_NAN_TCP_UPLOAD && result != GREEN2_NAN_TCP_DOWNLOAD) return;
    printf("[NAN-PERF] phase=%s tx=%u data=%u rx=%u whc_ms=%lu max=%lu send_calls=%u send_ms=%lu max=%lu\n",
        result == GREEN2_NAN_TCP_UPLOAD ? "upload" : "download",
        net_diag.tx_packets, net_diag.tx_data, net_diag.rx_packets,
        (unsigned long)net_diag.whc_ms, (unsigned long)net_diag.whc_max_ms,
        net_diag.send_calls, (unsigned long)net_diag.send_ms, (unsigned long)net_diag.send_max_ms);
    for (unsigned i = 0; i < net_diag.used; ++i)
        printf("[NAN-PERF] %c t=%lu seq=%lu ack=%lu len=%u cost=%lu\n",
            net_diag.packet[i].tx ? 'T' : 'R', (unsigned long)net_diag.packet[i].ms,
            (unsigned long)net_diag.packet[i].seq, (unsigned long)net_diag.packet[i].ack,
            net_diag.packet[i].bytes, (unsigned long)net_diag.packet[i].cost_ms);
}
#endif

void green2_nan_net_status(char *out, size_t len)
{
    snprintf(out, len, "link=%ld rx=%u tx=%u pong=%u up=%u down=%u err=%u rxdrop=%u txdrop=%u",
        (long)link_up, rx_count, tx_count, requests, uploads, downloads, protocol_errors,
        rx_dropped, tx_dropped);
}

static BaseType_t tcp_syn(const uint8_t *p, size_t size)
{
    return size >= 74 && p[12] == 0x86 && p[13] == 0xdd && p[20] == 6 && (p[67] & 2);
}

void green2_nan_mac_set(const uint8_t mac[6]) { memcpy(own_mac, mac, 6); }
uint8_t *green2_nan_mac(void) { return own_mac; }
void green2_nan_iid(uint8_t iid[8])
{
    iid[0] = own_mac[0] ^ 2; iid[1] = own_mac[1]; iid[2] = own_mac[2];
    iid[3] = 0xff; iid[4] = 0xfe; iid[5] = own_mac[3]; iid[6] = own_mac[4]; iid[7] = own_mac[5];
}
static BaseType_t phy(NetworkInterface_t *i) { (void)i; return link_up; }
static BaseType_t init(NetworkInterface_t *i)
{
    (void)i;
    if (!link_up) return pdFALSE;
    /* Runs in the IP task, which owns endpoint and neighbor-cache updates. */
    memcpy(endpoint.xMACAddress.ucBytes, data_mac, 6);
    memset(&endpoint.ipv6_defaults.xIPAddress, 0, sizeof(IPv6_Address_t));
    endpoint.ipv6_defaults.xIPAddress.ucBytes[0] = 0xfe;
    endpoint.ipv6_defaults.xIPAddress.ucBytes[1] = 0x80;
    uint8_t *iid = endpoint.ipv6_defaults.xIPAddress.ucBytes + 8;
    iid[0] = data_mac[0] ^ 2; iid[1] = data_mac[1]; iid[2] = data_mac[2];
    iid[3] = 0xff; iid[4] = 0xfe; memcpy(iid + 5, data_mac + 3, 3);
    endpoint.ipv6_settings.xIPAddress = endpoint.ipv6_defaults.xIPAddress;
    vNDRefreshCacheEntry(&peer_mac, &peer_ip, &endpoint);
    return pdTRUE;
}
static BaseType_t output(NetworkInterface_t *i, NetworkBufferDescriptor_t *b, BaseType_t release)
{
    (void)i; BaseType_t rc = pdFALSE;
    if (link_up && b && b->xDataLength <= 1514) {
        struct { unsigned int buf, len; } sg = {(unsigned int)b->pucEthernetBuffer, b->xDataLength};
#ifdef AT_NAN_NET_DIAG
        TickType_t before = xTaskGetTickCount();
#endif
        rc = whc_ipc_host_send(2, (struct eth_drv_sg *)&sg, 1, b->xDataLength, NULL, 0) == 0 ? pdTRUE : pdFALSE;
#ifdef AT_NAN_NET_DIAG
        if (rc) diag_packet(b->pucEthernetBuffer, b->xDataLength, pdTRUE,
                            before, xTaskGetTickCount() - before);
#endif
        if (rc) {
            tx_count++;
            if (syn_tx_ms == UINT32_MAX && tcp_syn(b->pucEthernetBuffer, b->xDataLength))
                syn_tx_ms = (xTaskGetTickCount() - link_started) * portTICK_PERIOD_MS;
        } else tx_dropped++;
    }
    if (release) vReleaseNetworkBufferAndDescriptor(b);
    return rc;
}
void green2_nan_net_init(void)
{
    IPv6_Address_t ll = {{0xfe, 0x80}}, prefix = {{0xfe, 0x80}};
    iface.pcName = "nan0"; iface.pfInitialise = init; iface.pfOutput = output; iface.pfGetPhyLinkStatus = phy;
    FreeRTOS_AddNetworkInterface(&iface);
    green2_nan_iid(ll.ucBytes + 8);
    FreeRTOS_FillEndPoint_IPv6(&iface, &endpoint, &ll, &prefix, 64, NULL, NULL, own_mac);
}
void green2_nan_net_up(const uint8_t own[6], const uint8_t peer[6], const uint8_t iid[8])
{
    taskENTER_CRITICAL();
    memcpy(data_mac, own, 6); memcpy(peer_mac.ucBytes, peer, 6);
    memset(&peer_ip, 0, sizeof(peer_ip)); peer_ip.ucBytes[0] = 0xfe; peer_ip.ucBytes[1] = 0x80;
    memcpy(peer_ip.ucBytes + 8, iid, 8);
    static const uint8_t absent_iid[8];
    if (!memcmp(iid, absent_iid, 8)) {
        /* Without an explicit NDPE IID, the peer uses its NDI's EUI-64. */
        uint8_t *p = peer_ip.ucBytes + 8;
        p[0] = peer[0] ^ 2; p[1] = peer[1]; p[2] = peer[2];
        p[3] = 0xff; p[4] = 0xfe; memcpy(p + 5, peer + 3, 3);
    }
    ++link_generation;
    link_started = xTaskGetTickCount();
    syn_rx_ms = syn_tx_ms = UINT32_MAX;
    link_up = pdTRUE;
    taskEXIT_CRITICAL();
    FreeRTOS_NetworkDown(&iface);
    printf("[NAN] NDP up local=%02x:%02x:%02x:%02x:%02x:%02x peer=%02x:%02x:%02x:%02x:%02x:%02x\n",
        own[0],own[1],own[2],own[3],own[4],own[5],peer[0],peer[1],peer[2],peer[3],peer[4],peer[5]);
    printf("[NAN] peer IID=%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
        iid[0],iid[1],iid[2],iid[3],iid[4],iid[5],iid[6],iid[7]);
}
void green2_nan_net_down(void)
{
    taskENTER_CRITICAL();
    link_up = pdFALSE;
    ++link_generation;
    taskEXIT_CRITICAL();
    FreeRTOS_NetworkDown(&iface);
}
void green2_nan_rx(const void *data, size_t len)
{
    if (!link_up || len < 14 || len > 1514) return;
#ifdef AT_NAN_NET_DIAG
    diag_packet(data, len, pdFALSE, xTaskGetTickCount(), 0);
#endif
    if (syn_rx_ms == UINT32_MAX && tcp_syn(data, len))
        syn_rx_ms = (xTaskGetTickCount() - link_started) * portTICK_PERIOD_MS;
    NetworkBufferDescriptor_t *b = at_netif_rx_alloc(len);
    if (!b) { rx_dropped++; return; }
    memcpy(b->pucEthernetBuffer, data, len); b->xDataLength = len;
    b->pxInterface = &iface; b->pxEndPoint = FreeRTOS_MatchingEndpoint(&iface, b->pucEthernetBuffer);
    IPStackEvent_t e = {eNetworkRxEvent, b};
    if (xSendEventStructToIPTask(&e, 0) != pdPASS) {
        rx_dropped++; vReleaseNetworkBufferAndDescriptor(b);
    }
    else rx_count++;
}
struct tcp_client {
    Socket_t socket;
    uint32_t generation;
};
static uint32_t tcp_now_ms(void *context)
{
    (void)context;
    return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}
static int tcp_io(struct tcp_client *client, void *data, size_t size,
                  uint32_t timeout_ms, BaseType_t sending)
{
    uint32_t started = tcp_now_ms(NULL);
    while (link_up && client->generation == link_generation) {
        uint32_t elapsed = tcp_now_ms(NULL) - started;
        if (elapsed >= timeout_ms) break;
        uint32_t left = timeout_ms - elapsed;
        /* Recheck NDP identity at least once a second during stalled I/O. */
        TickType_t ticks = pdMS_TO_TICKS(left < 1000u ? left : 1000u);
        if (!ticks) ticks = 1;
        FreeRTOS_setsockopt(client->socket, 0,
            sending ? FREERTOS_SO_SNDTIMEO : FREERTOS_SO_RCVTIMEO, &ticks, sizeof(ticks));
#ifdef AT_NAN_NET_DIAG
        uint32_t before = tcp_now_ms(NULL);
#endif
        BaseType_t n = sending ? FreeRTOS_send(client->socket, data, size, 0) :
                                 FreeRTOS_recv(client->socket, data, size, 0);
#ifdef AT_NAN_NET_DIAG
        if (sending && net_diag.active) {
            uint32_t cost = tcp_now_ms(NULL) - before;
            ++net_diag.send_calls;
            net_diag.send_ms += cost;
            if (cost > net_diag.send_max_ms) net_diag.send_max_ms = cost;
        }
#endif
        /* This FreeRTOS+TCP release reports a send timeout as ENOSPC,
         * while recv returns zero. Both consume only this one-second slice. */
        if (n != 0 && !(sending && n == -pdFREERTOS_ERRNO_ENOSPC)) return (int)n;
        vTaskDelay(1);
    }
    return -1;
}
static int tcp_read(void *context, void *data, size_t size, uint32_t timeout_ms)
{ return tcp_io(context, data, size, timeout_ms, pdFALSE); }
static int tcp_write(void *context, const void *data, size_t size, uint32_t timeout_ms)
{ return tcp_io(context, (void *)data, size, timeout_ms, pdTRUE); }
static void server(void *arg)
{
    (void)arg;
    while (!at_netif_is_ready()) vTaskDelay(pdMS_TO_TICKS(50));
    Socket_t listener = FreeRTOS_socket(FREERTOS_AF_INET6, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
    struct freertos_sockaddr address = {0};
    address.sin_family = FREERTOS_AF_INET6; address.sin_port = FreeRTOS_htons(8000);
    /* Only this service needs bulk buffers. Accepted sockets inherit these
     * properties; the shared low-memory defaults and other profiles stay intact. */
    const WinProperties_t window = {
        .lTxBufSize = 40 * ipconfigTCP_MSS, .lTxWinSize = 32,
        .lRxBufSize = 24 * ipconfigTCP_MSS, .lRxWinSize = 16
    };
    if (listener == FREERTOS_INVALID_SOCKET ||
        FreeRTOS_setsockopt(listener, 0, FREERTOS_SO_WIN_PROPERTIES, &window, sizeof(window)) ||
        FreeRTOS_bind(listener, &address, sizeof(address)) || FreeRTOS_listen(listener, 1)) {
        printf("[NAN] TCP listen failed\n");
        if (listener != FREERTOS_INVALID_SOCKET) FreeRTOS_closesocket(listener);
        vTaskDelete(NULL); return;
    }
    printf("[NAN] FreeRTOS TCP port 8000 ready (PING + NAN-THROUGHPUT/1)\n");
    for (;;) {
        struct freertos_sockaddr peer = {0}; socklen_t peerlen = sizeof(peer);
        Socket_t client = FreeRTOS_accept(listener, &peer, &peerlen);
        if (client == FREERTOS_INVALID_SOCKET) continue;
        printf("[NAN] TCP accepted peer_match=%u link=%ld\n", !memcmp(peer.sin_address.xIP_IPv6.ucBytes, peer_ip.ucBytes, 16), (long)link_up);
        if (!link_up || memcmp(peer.sin_address.xIP_IPv6.ucBytes, peer_ip.ucBytes, 16)) {
            FreeRTOS_closesocket(client); continue;
        }
        printf("[NAN] link age=%lu ms first SYN rx=%lu tx=%lu ms\n",
            (unsigned long)((xTaskGetTickCount() - link_started) * portTICK_PERIOD_MS),
            (unsigned long)syn_rx_ms, (unsigned long)syn_tx_ms);
        struct tcp_client context = {client, link_generation};
        const struct green2_nan_tcp_io io = {&context, tcp_read, tcp_write, tcp_now_ms};
        for (;;) {
            struct green2_nan_tcp_stats stats;
#ifdef AT_NAN_NET_DIAG
            diag_begin();
#endif
            enum green2_nan_tcp_result result = green2_nan_tcp_command(&io, &stats);
#ifdef AT_NAN_NET_DIAG
            diag_end(result);
#endif
            if (result == GREEN2_NAN_TCP_PING) {
                printf("[NAN] TCP PONG #%u rx=%u tx=%u\n", ++requests, rx_count, tx_count);
                break;
            }
            if (result <= GREEN2_NAN_TCP_CLOSED) {
                if (result == GREEN2_NAN_TCP_ERROR) {
                    ++protocol_errors;
                    printf("[NAN] TCP test aborted (protocol, timeout or link loss)\n");
                }
                break;
            }
            if (result == GREEN2_NAN_TCP_UPLOAD) ++uploads;
            else ++downloads;
            uint64_t kbps = stats.bytes * 8u / stats.elapsed_ms;
            printf("[NAN] throughput %s bytes=%" PRIu64 " ms=%lu Mbps=%" PRIu64 ".%03" PRIu64 "\n",
                result == GREEN2_NAN_TCP_UPLOAD ? "upload" : "download", stats.bytes,
                (unsigned long)stats.elapsed_ms, kbps / 1000u, kbps % 1000u);
            const FreeRTOS_Socket_t *socket_state = (const FreeRTOS_Socket_t *)client;
            printf("[NAN] TCP MSS=%u txbuf=%lu txwin=%lu rxdrop=%u txdrop=%u\n",
                socket_state->u.xTCP.usMSS, (unsigned long)socket_state->u.xTCP.uxTxStreamSize,
                (unsigned long)socket_state->u.xTCP.xTCPWindow.xSize.ulTxWindowLength,
                rx_dropped, tx_dropped);
        }
        FreeRTOS_shutdown(client, FREERTOS_SHUT_RDWR);
        /* Allow queued data and FIN to leave, with a three-second total bound
         * even if a peer disappears or keeps sending after its request. */
        TickType_t timeout = pdMS_TO_TICKS(100);
        FreeRTOS_setsockopt(client, 0, FREERTOS_SO_RCVTIMEO, &timeout, sizeof(timeout));
        TickType_t closing = xTaskGetTickCount();
        char discard[32];
        while (link_up && context.generation == link_generation &&
               (TickType_t)(xTaskGetTickCount() - closing) < pdMS_TO_TICKS(3000)) {
            if (FreeRTOS_recv(client, discard, sizeof(discard), 0) < 0) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        FreeRTOS_closesocket(client);
    }
}
void green2_nan_server_start(void)
{ configASSERT(xTaskCreate(server, "nan_tcp", 2048, NULL, 2, NULL) == pdPASS); }
