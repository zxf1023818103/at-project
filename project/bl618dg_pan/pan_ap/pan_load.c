/* Direct TCP load generator. Each link uses its own local IPv4 address and
 * an on-subnet peer; the host logs bytes actually received independently. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Routing.h"
#include "FreeRTOS_Sockets.h"
#include "shell.h"
#include "mm.h"

struct load_job {
    uint32_t local, peer;
    uint16_t port;
    unsigned seconds, slot;
    bool receive;
};
static bool busy[2];

static void load_task(void *argument)
{
    struct load_job *job = argument;
    const char *name = job->slot ? "wl1" : "pan0";
    Socket_t sock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
    uint8_t *data = malloc(1460);
    uint32_t total = 0, bad_bytes = 0;
    unsigned stalls = 0;
    BaseType_t error = 0;
    TickType_t started = 0, elapsed = 0;
    if (sock == FREERTOS_INVALID_SOCKET || !data) { error = -1; goto done; }
    TickType_t timeout = pdMS_TO_TICKS(2000);
    FreeRTOS_setsockopt(sock, 0, FREERTOS_SO_RCVTIMEO, &timeout, sizeof(timeout));
    FreeRTOS_setsockopt(sock, 0, FREERTOS_SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct freertos_sockaddr local = {.sin_family = FREERTOS_AF_INET};
    struct freertos_sockaddr peer = {.sin_family = FREERTOS_AF_INET};
    local.sin_address.ulIP_IPv4 = job->local;
    peer.sin_address.ulIP_IPv4 = job->peer;
    peer.sin_port = FreeRTOS_htons(job->port);
    error = FreeRTOS_bind(sock, &local, sizeof(local));
    if (error) goto done;
    error = FreeRTOS_connect(sock, &peer, sizeof(peer));
    if (error) goto done;
    char header[40];
    int length = snprintf(header, sizeof(header), "%s %u\n", job->receive ? "RX" : "TX", job->seconds);
    size_t offset = 0;
    while (offset < (size_t)length) {
        BaseType_t count = FreeRTOS_send(sock, header + offset, length - offset, 0);
        if (count <= 0) { error = count ? count : -1; goto done; }
        offset += count;
    }
    memset(data, 0x5a, 1460);
    char source[16], target[16];
    FreeRTOS_inet_ntoa(job->local, source);
    FreeRTOS_inet_ntoa(job->peer, target);
    printf("[load] START %s %s source=%s peer=%s:%u seconds=%u\r\n",
           name, job->receive ? "RX" : "TX", source, target, job->port, job->seconds);
    started = xTaskGetTickCount();
    while ((elapsed = xTaskGetTickCount() - started) < pdMS_TO_TICKS(job->seconds * 1000)) {
        BaseType_t count = job->receive ? FreeRTOS_recv(sock, data, 1460, 0) :
                                         FreeRTOS_send(sock, data, 1460, 0);
        if (count > 0) {
            if (job->receive) for (BaseType_t i = 0; i < count; ++i) bad_bytes += data[i] != 0x5a;
            total += count;
        }
        else if (count == -pdFREERTOS_ERRNO_EWOULDBLOCK || count == 0 ||
                 (!job->receive && count == -pdFREERTOS_ERRNO_ENOSPC)) {
            /* A bounded idle wait counts toward the requested duration. */
            ++stalls;
            vTaskDelay(1);
        } else { error = count; break; }
    }
    elapsed = xTaskGetTickCount() - started;
    if (job->receive) {
        /* Receiver receipt excludes the phone's queued-but-undelivered bytes. */
        char receipt[80];
        int receipt_size = snprintf(receipt, sizeof(receipt), "RESULT %lu %lu %lu\n",
                                   (unsigned long)total,
                                   (unsigned long)elapsed * 1000 / configTICK_RATE_HZ,
                                   (unsigned long)bad_bytes);
        size_t sent = 0;
        while (sent < (size_t)receipt_size) {
            BaseType_t n = FreeRTOS_send(sock, receipt + sent, receipt_size - sent, 0);
            if (n <= 0) break;
            sent += n;
        }
    }
    FreeRTOS_shutdown(sock, FREERTOS_SHUT_RDWR);
    /* Allow queued TX data/FIN to drain before releasing the socket. */
    TickType_t drain = xTaskGetTickCount();
    while (xTaskGetTickCount() - drain < pdMS_TO_TICKS(2000)) {
        if (FreeRTOS_recv(sock, data, 1460, 0) < 0) break;
    }
done:
    if (sock != FREERTOS_INVALID_SOCKET) FreeRTOS_closesocket(sock);
    unsigned long ms = (unsigned long)elapsed * 1000 / configTICK_RATE_HZ;
    unsigned long kbps = ms ? (uint64_t)total * 8 / ms : 0;
    printf("[load] END %s %s bytes=%lu ms=%lu kbps=%lu stalls=%u error=%ld bad_bytes=%lu heap=%u\r\n",
           name, job->receive ? "RX" : "TX", (unsigned long)total, ms, kbps,
           stalls, (long)error, (unsigned long)bad_bytes, (unsigned)kfree_size(0));
    free(data);
    busy[job->slot] = false;
    free(job);
    vTaskDelete(NULL);
}

static int net_load(int argc, char **argv)
{
    if (argc != 6) return -1;
    unsigned slot;
    if (!strcmp(argv[1], "pan0")) slot = 0;
    else if (!strcmp(argv[1], "wl1")) slot = 1;
    else return -1;
    if (busy[slot] || (strcmp(argv[5], "rx") && strcmp(argv[5], "tx"))) return -1;
    char *end;
    unsigned long port = strtoul(argv[3], &end, 10);
    if (*end || !port || port > 65535) return -1;
    unsigned long seconds = strtoul(argv[4], &end, 10);
    if (*end || !seconds || seconds > 300) return -1;
    struct load_job *job = calloc(1, sizeof(*job));
    if (!job) return -1;
    job->slot = slot;
    job->port = port;
    job->seconds = seconds;
    job->receive = !strcmp(argv[5], "rx");
    job->peer = FreeRTOS_inet_addr(argv[2]);
    for (NetworkEndPoint_t *ep = FreeRTOS_FirstEndPoint(NULL); ep; ep = FreeRTOS_NextEndPoint(NULL, ep)) {
        if (!strcmp(ep->pxNetworkInterface->pcName, argv[1]) && ep->bits.bEndPointUp) {
            job->local = ep->ipv4_settings.ulIPAddress;
            if ((job->peer & ep->ipv4_settings.ulNetMask) !=
                (job->local & ep->ipv4_settings.ulNetMask)) job->local = 0;
            break;
        }
    }
    if (!job->local || !job->peer || job->peer == 0xffffffffUL) {
        printf("[load] interface must be up and peer must be on its subnet\r\n");
        free(job); return -1;
    }
    busy[slot] = true;
    if (xTaskCreate(load_task, "net_load", 1536, job, configMAX_PRIORITIES - 6, NULL) != pdPASS) {
        busy[slot] = false; free(job); return -1;
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(net_load, net_load, TCP load: pan0|wl1 peerIPv4 port seconds rx|tx.);
