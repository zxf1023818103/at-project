/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "FreeRTOS_CLI.h"
#include "tl3238_port.h"
#include <stdio.h>
#include <string.h>
#ifndef TL3238_TASK_SNAPSHOT_COUNT
#define TL3238_TASK_SNAPSHOT_COUNT 8
#endif

static BaseType_t version_command(char *out, size_t size, const char *command)
{
    (void)command;
    snprintf(out, size, "TL3238 / FreeRTOS %s / FreeRTOS-Plus-CLI V202212.00\r\n"
             "CPU=48000000 Hz tick=%u Hz UART0=115200 TX=PA1 RX=PA2 tickless=WFI\r\n",
             tskKERNEL_VERSION_NUMBER, (unsigned)configTICK_RATE_HZ);
    return pdFALSE;
}

static BaseType_t hello_command(char *out, size_t size, const char *command)
{
    (void)command;
    snprintf(out, size, "hello world\r\n");
    return pdFALSE;
}

static BaseType_t echo_command(char *out, size_t size, const char *command)
{
    const char *text = strchr(command, ' ');
    snprintf(out, size, "%s\r\n", text ? text + 1 : "");
    return pdFALSE;
}

static BaseType_t uptime_command(char *out, size_t size, const char *command)
{
    (void)command;
    TickType_t ticks = xTaskGetTickCount();
    snprintf(out, size, "ticks=%lu uptime_ms=%lu\r\n", (unsigned long)ticks,
             (unsigned long)(ticks * portTICK_PERIOD_MS));
    return pdFALSE;
}

static BaseType_t heap_command(char *out, size_t size, const char *command)
{
    (void)command;
    HeapStats_t stats;
    vPortGetHeapStats(&stats);
    snprintf(out, size, "heap_total=%u free=%u minimum=%u largest=%u blocks=%u\r\n",
             (unsigned)configTOTAL_HEAP_SIZE, (unsigned)stats.xAvailableHeapSpaceInBytes,
             (unsigned)stats.xMinimumEverFreeBytesRemaining, (unsigned)stats.xSizeOfLargestFreeBlockInBytes,
             (unsigned)stats.xNumberOfFreeBlocks);
    return pdFALSE;
}

static BaseType_t tasks_command(char *out, size_t size, const char *command)
{
    (void)command;
    /* Snapshot once, then emit one bounded row per CLI callback. */
    static TaskStatus_t tasks[TL3238_TASK_SNAPSHOT_COUNT];
    static UBaseType_t count, index;
    static BaseType_t active;
    if (!active) {
        count = uxTaskGetSystemState(tasks, TL3238_TASK_SNAPSHOT_COUNT, NULL);
        index = 0;
        active = pdTRUE;
        snprintf(out, size, "name             state priority stack_free_bytes\r\n");
        if (count) return pdTRUE;
    }
    if (index < count) {
        TaskStatus_t *task = &tasks[index++];
        static const char states[] = "RrBSDI";
        unsigned state = (unsigned)task->eCurrentState;
        snprintf(out, size, "%-16s %c     %u        %u\r\n", task->pcTaskName,
                 state < sizeof(states) - 1 ? states[state] : '?',
                 (unsigned)task->uxCurrentPriority,
                 (unsigned)(task->usStackHighWaterMark * sizeof(StackType_t)));
    }
    if (index < count) return pdTRUE;
    active = pdFALSE;
    return pdFALSE;
}

static BaseType_t stats_command(char *out, size_t size, const char *command)
{
    (void)command;
    taskENTER_CRITICAL();
    DemoStats stats = demo_stats;
    taskEXIT_CRITICAL();
    snprintf(out, size, "produced=%lu consumed=%lu sequence_errors=%lu timer_callbacks=%lu fpu_errors=%lu\r\n"
             "rx_dropped=%lu rx_errors=%lu tx_errors=%lu\r\n",
             (unsigned long)stats.produced, (unsigned long)stats.consumed,
             (unsigned long)stats.sequence_errors, (unsigned long)stats.timer_callbacks,
             (unsigned long)stats.fpu_errors, (unsigned long)console_rx_dropped,
             (unsigned long)console_rx_errors, (unsigned long)console_tx_errors);
    return pdFALSE;
}

static BaseType_t selftest_command(char *out, size_t size, const char *command)
{
    (void)command;
    size_t before = xPortGetFreeHeapSize();
    unsigned char *a = pvPortMalloc(257), *b = pvPortMalloc(513);
    BaseType_t memory_ok = a != NULL && b != NULL;
    if (memory_ok) {
        for (unsigned i = 0; i < 257; i++) a[i] = (unsigned char)i;
        memset(b, 0xa5, 513);
        for (unsigned i = 0; i < 257; i++) memory_ok &= a[i] == (unsigned char)i;
        for (unsigned i = 0; i < 513; i++) memory_ok &= b[i] == 0xa5;
    }
    vPortFree(a);
    vPortFree(b);
    memory_ok &= before == xPortGetFreeHeapSize();
    BaseType_t ok = memory_ok && demo_stats.consumed > 0 && demo_stats.timer_callbacks > 0 &&
        demo_stats.sequence_errors == 0 && demo_stats.fpu_errors == 0 &&
        console_rx_dropped == 0 && console_rx_errors == 0 && console_tx_errors == 0;
    snprintf(out, size, "SELFTEST %s: heap=%s queue=%lu timer=%lu fpu_errors=%lu\r\n",
             ok ? "PASS" : "FAIL", memory_ok ? "PASS" : "FAIL",
             (unsigned long)demo_stats.consumed, (unsigned long)demo_stats.timer_callbacks,
             (unsigned long)demo_stats.fpu_errors);
    return pdFALSE;
}

static BaseType_t tickless_command(char *out, size_t size, const char *command)
{
    BaseType_t length;
    const char *option = FreeRTOS_CLIGetParameter(command, 1, &length);
    BaseType_t unused_length;
    const char *extra = FreeRTOS_CLIGetParameter(command, 2, &unused_length);
    if (extra) {
        snprintf(out, size, "Usage: tickless [on|off|stats]\r\n");
        return pdFALSE;
    }
    if (option) {
        if (length == 2 && !strncmp(option, "on", 2)) {
            tl3238_tickless_enable(1);
        } else if (length == 3 && !strncmp(option, "off", 3)) {
            tl3238_tickless_enable(0);
        } else if (length != 5 || strncmp(option, "stats", 5)) {
            snprintf(out, size, "Usage: tickless [on|off|stats]\r\n");
            return pdFALSE;
        }
    }
    Tl3238TicklessStats stats;
    tl3238_tickless_get_stats(&stats);
    snprintf(out, size, "tickless=%lu mode=WFI tick_irqs=%lu sleeps=%lu skipped_ticks=%lu\r\n"
             "timer_wakes=%lu uart_wakes=%lu other_wakes=%lu aborts=%lu\r\n"
             "last_idle_ticks=%lu max_idle_ticks=%lu max_sleep_ms=%u\r\n",
             (unsigned long)stats.enabled, (unsigned long)stats.tick_irqs,
             (unsigned long)stats.sleep_entries, (unsigned long)stats.skipped_ticks,
             (unsigned long)stats.timer_wakes, (unsigned long)stats.uart_wakes,
             (unsigned long)stats.other_wakes, (unsigned long)stats.aborted_sleeps,
             (unsigned long)stats.last_idle_ticks, (unsigned long)stats.max_idle_ticks,
             TL3238_MAX_IDLE_MS);
    return pdFALSE;
}

static BaseType_t idle_command(char *out, size_t size, const char *command)
{
    BaseType_t length;
    const char *parameter = FreeRTOS_CLIGetParameter(command, 1, &length);
    uint32_t milliseconds = 0;
    if (!parameter || length < 1 || length > 5) goto invalid;
    for (BaseType_t i = 0; i < length; i++) {
        if (parameter[i] < '0' || parameter[i] > '9') goto invalid;
        milliseconds = milliseconds * 10u + (unsigned)(parameter[i] - '0');
    }
    if (milliseconds < 2 || milliseconds > 65000) goto invalid;
    uint32_t hardware_ms;
    TickType_t elapsed = demo_idle_window(pdMS_TO_TICKS(milliseconds), &hardware_ms);
    snprintf(out, size, "IDLE requested_ms=%lu elapsed_ticks=%lu hardware_ms=%lu\r\n",
             (unsigned long)milliseconds, (unsigned long)elapsed, (unsigned long)hardware_ms);
    return pdFALSE;
invalid:
    snprintf(out, size, "Usage: idle <2..65000 ms>\r\n");
    return pdFALSE;
}

void cli_register_commands(void)
{
    static const CLI_Command_Definition_t commands[] = {
        {"version", "version: component versions and board configuration\r\n", version_command, 0},
        {"hello", "hello: print hello world\r\n", hello_command, 0},
        {"echo", "echo <text>: echo text including spaces\r\n", echo_command, -1},
        {"uptime", "uptime: scheduler ticks and uptime\r\n", uptime_command, 0},
        {"tasks", "tasks: task state, priority and unused stack\r\n", tasks_command, 0},
        {"heap", "heap: FreeRTOS heap statistics\r\n", heap_command, 0},
        {"stats", "stats: queue, software timer, FPU and UART counters\r\n", stats_command, 0},
        {"tickless", "tickless [on|off|stats]: WFI tick suppression and wake counters\r\n", tickless_command, -1},
        {"idle", "idle <2..65000 ms>: pause demo activity for a bounded idle test\r\n", idle_command, 1},
        {"selftest", "selftest: heap round-trip and background checks\r\n", selftest_command, 0},
    };
    for (unsigned i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        configASSERT(FreeRTOS_CLIRegisterCommand(&commands[i]) == pdPASS);
    }
}
