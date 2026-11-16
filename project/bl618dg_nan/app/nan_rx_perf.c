/* Link-time diagnostics; ACK policy stays upstream unless nanack enables a cap.
 * Wall times include interrupts/preemption. Scheduler runtime counters below
 * separate task attribution (including ISR time charged to the current task).
 * No payloads, addresses, keys, allocation or printing on the measured path. */
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_Routing.h"
#include "FreeRTOS_TCP_Transmission.h"
#include "NetworkBufferManagement.h"
#include "nan_tcp_protocol.h"
#include "nan_heap.h"
#include "bflb_mtimer.h"
#include "shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum stage { TOTAL, ALLOC, COPY, MATCH, SUBMIT, STAGES };
struct timing { uint64_t us; uint32_t count, maximum; };
static struct timing timings[2][STAGES];
static TaskHandle_t receiver;
static unsigned bucket;
static int ready, measuring, receiving;
static volatile uint32_t ack_limit_ms, ack_shortened;
#define TASK_CAP 32
static TaskStatus_t before_tasks[TASK_CAP], after_tasks[TASK_CAP];

uint32_t bl618dg_nan_runtime_us(void) { return (uint32_t)bflb_mtimer_get_time_us(); }
static int active(void)
{ return measuring && receiving && xTaskGetCurrentTaskHandle() == receiver; }
static void record(enum stage stage, uint32_t started)
{
    uint32_t us = bl618dg_nan_runtime_us() - started;
    struct timing *s = &timings[bucket][stage];
    s->us += us; ++s->count;
    if (us > s->maximum) s->maximum = us;
}
extern void __real_green2_nan_rx(const void *, size_t);
void __wrap_green2_nan_rx(const void *data, size_t size)
{
    if (!measuring) { __real_green2_nan_rx(data, size); return; }
    receiver = xTaskGetCurrentTaskHandle(); bucket = size > 128; receiving = 1;
    uint32_t started = bl618dg_nan_runtime_us();
    __real_green2_nan_rx(data, size);
    record(TOTAL, started); receiving = 0;
}
extern NetworkBufferDescriptor_t *__real_at_netif_rx_alloc(size_t);
NetworkBufferDescriptor_t *__wrap_at_netif_rx_alloc(size_t size)
{
    int sample = active(); uint32_t started = sample ? bl618dg_nan_runtime_us() : 0;
    NetworkBufferDescriptor_t *p = __real_at_netif_rx_alloc(size);
    if (sample) record(ALLOC, started);
    return p;
}
void *bl618dg_nan_profile_memcpy(void *dest, const void *source, size_t size)
{
    int sample = active(); uint32_t started = sample ? bl618dg_nan_runtime_us() : 0;
    void *p = memcpy(dest, source, size);
    if (sample) record(COPY, started);
    return p;
}
extern NetworkEndPoint_t *__real_FreeRTOS_MatchingEndpoint(const NetworkInterface_t *, const uint8_t *);
NetworkEndPoint_t *__wrap_FreeRTOS_MatchingEndpoint(const NetworkInterface_t *interface, const uint8_t *buffer)
{
    int sample = active(); uint32_t started = sample ? bl618dg_nan_runtime_us() : 0;
    NetworkEndPoint_t *p = __real_FreeRTOS_MatchingEndpoint(interface, buffer);
    if (sample) record(MATCH, started);
    return p;
}
extern BaseType_t __real_xSendEventStructToIPTask(const IPStackEvent_t *, TickType_t);
BaseType_t __wrap_xSendEventStructToIPTask(const IPStackEvent_t *event, TickType_t timeout)
{
    int sample = active(); uint32_t started = sample ? bl618dg_nan_runtime_us() : 0;
    BaseType_t rc = __real_xSendEventStructToIPTask(event, timeout);
    if (sample) record(SUBMIT, started);
    return rc;
}
/* Optional, default-off A/B control. The upstream stack still owns ACK
 * generation, buffers and window bookkeeping; only its pending timeout is
 * capped. Change between runs with `nanack 1`; `nanack 0` restores upstream. */
extern BaseType_t __real_prvSendData(FreeRTOS_Socket_t *, NetworkBufferDescriptor_t **, uint32_t, BaseType_t);
BaseType_t __wrap_prvSendData(FreeRTOS_Socket_t *socket, NetworkBufferDescriptor_t **buffer,
                            uint32_t received, BaseType_t bytes)
{
    BaseType_t rc = __real_prvSendData(socket, buffer, received, bytes);
    uint32_t limit = ack_limit_ms;
    if (limit && received && socket->usLocalPort == 8000 && socket->u.xTCP.pxAckMessage) {
        TickType_t ticks = pdMS_TO_TICKS(limit);
        if (!ticks) ticks = 1;
        if (socket->u.xTCP.usTimeout > ticks) {
            socket->u.xTCP.usTimeout = ticks; ++ack_shortened;
        }
    }
    return rc;
}
static int ack_command(int argc, char **argv)
{
    if (argc > 1) {
        char *end; unsigned long value = strtoul(argv[1], &end, 10);
        if (argc != 2 || end == argv[1] || *end || value > 20) {
            puts("usage: nanack [0..20] (0=upstream default)"); return -1;
        }
        ack_limit_ms = value;
    }
    printf("[RX-PERF] ACK cap_ms=%u shortened=%u; change only between tests\n", (unsigned)ack_limit_ms, (unsigned)ack_shortened);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(ack_command, nanack, Diagnostic delayed-ACK cap in ms (0=default).);
extern enum green2_nan_tcp_result __real_green2_nan_tcp_command(
    const struct green2_nan_tcp_io *, struct green2_nan_tcp_stats *);
enum green2_nan_tcp_result __wrap_green2_nan_tcp_command(
    const struct green2_nan_tcp_io *io, struct green2_nan_tcp_stats *stats)
{
    /* Startup's synthetic protocol self-test is not a throughput sample. */
    if (!ready) return __real_green2_nan_tcp_command(io, stats);
    uint32_t before_time, after_time;
    uint32_t before_acks = ack_shortened, before_limit = ack_limit_ms;
    UBaseType_t before_count = uxTaskGetSystemState(before_tasks, TASK_CAP, &before_time);
    taskENTER_CRITICAL();
    memset(timings, 0, sizeof(timings)); receiving = 0; measuring = 1;
    taskEXIT_CRITICAL();
    enum green2_nan_tcp_result rc = __real_green2_nan_tcp_command(io, stats);
    taskENTER_CRITICAL(); measuring = 0; taskEXIT_CRITICAL();
    UBaseType_t after_count = uxTaskGetSystemState(after_tasks, TASK_CAP, &after_time);
    if (rc != GREEN2_NAN_TCP_UPLOAD && rc != GREEN2_NAN_TCP_DOWNLOAD) return rc;
    const char *phase = rc == GREEN2_NAN_TCP_UPLOAD ? "upload" : "download";
    uint32_t interval = after_time - before_time;
    printf("[RX-PERF] phase=%s interval_us=%u tasks=%u/%u ACK_cap_ms=%u/%u shortened=%u\n", phase, interval,
        (unsigned)before_count, (unsigned)after_count, (unsigned)before_limit,
        (unsigned)ack_limit_ms, (unsigned)(ack_shortened - before_acks));
    const char *names[] = {"total", "allocate", "copy", "match", "submit+IP-preemption"};
    for (unsigned b = 0; b < 2; ++b) {
        for (unsigned i = 0; i < STAGES; ++i) {
            const struct timing *s = &timings[b][i];
            printf("[RX-PERF] %s %s n=%u total_us=%llu avg_us=%llu max_us=%u\n",
                b ? "data" : "small", names[i], s->count, (unsigned long long)s->us,
                (unsigned long long)(s->count ? s->us / s->count : 0), s->maximum);
        }
    }
    if (!before_count || !after_count || !interval) { puts("[RX-PERF] task snapshot unavailable"); return rc; }
    uint64_t accounted = 0;
    for (unsigned i = 0; i < after_count; ++i) {
        const TaskStatus_t *a = &after_tasks[i];
        for (unsigned j = 0; j < before_count; ++j) {
            const TaskStatus_t *b = &before_tasks[j];
            if (a->xHandle != b->xHandle || a->xTaskNumber != b->xTaskNumber) continue;
            uint32_t used = a->ulRunTimeCounter - b->ulRunTimeCounter;
            unsigned permille = (uint64_t)used * 1000 / interval;
            accounted += used;
            printf("[RX-PERF] task=%s us=%u share=%u.%u%% priority=%u\n",
                a->pcTaskName, used, permille / 10, permille % 10, (unsigned)a->uxCurrentPriority);
            break;
        }
    }
    printf("[RX-PERF] accounted_us=%llu (ISR time included in task counters)\n", (unsigned long long)accounted);
    return rc;
}

static __attribute__((section(".noncacheable"), aligned(32))) uint8_t copy_source[1568];
void bl618dg_nan_profile_init(void)
{
    uint8_t *a = pvPortMalloc(1568), *b = pvPortMalloc(1568);
    configASSERT(a && b);
    for (unsigned i = 0; i < sizeof(copy_source); ++i) copy_source[i] = (uint8_t)(i * 37);
    memcpy(a, copy_source, sizeof(copy_source));
    const unsigned lengths[] = {84, 1100, 1514};
    for (unsigned k = 0; k < 3; ++k) {
        unsigned size = lengths[k];
        uint32_t started = bl618dg_nan_runtime_us();
        for (unsigned n = 0; n < 256; ++n) memcpy(b + 2, copy_source + 18, size);
        uint32_t rx_us = bl618dg_nan_runtime_us() - started;
        configASSERT(!memcmp(b + 2, copy_source + 18, size));
        started = bl618dg_nan_runtime_us();
        for (unsigned n = 0; n < 256; ++n) memcpy(b + 2, a + 2, size);
        uint32_t ram_us = bl618dg_nan_runtime_us() - started;
        configASSERT(!memcmp(b + 2, a + 2, size));
        printf("[RX-PERF] memcpy heap=" NAN_DATA_HEAP_NAME " bytes=%u 256_rounds uncached_to_heap_us=%u heap_to_heap_us=%u\n",
            size, rx_us, ram_us);
    }
    vPortFree(a); vPortFree(b);
    ready = 1;
}
