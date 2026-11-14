/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#ifdef TL3238_ENABLE_BLE
#include "controller.h"
#include "driver.h"
void tl3238_ble_start(void);
#else
#include "common.h"
#endif
#include "queue.h"
#include "timers.h"
#include <stdio.h>
/* Optional multi-region heap setup, before the first RTOS allocation. */
void tl3238_app_memory_init(void) __attribute__((weak));

volatile DemoStats demo_stats;
static QueueHandle_t demo_queue;
static TaskHandle_t producer_handle;
static TimerHandle_t demo_timer;
static volatile BaseType_t reset_producer_deadline;

static void producer_task(void *arg)
{
    (void)arg;
    uint32_t sequence = 0;
    float fp = 0.0f;
    TickType_t deadline = xTaskGetTickCount();
    for (;;) {
        if (reset_producer_deadline) {
            deadline = xTaskGetTickCount();
            reset_producer_deadline = pdFALSE;
        }
        xTaskDelayUntil(&deadline, pdMS_TO_TICKS(10));
        /* Keep a live FPU value across context switches; avoid constant folding. */
        __asm volatile("" : "+f"(fp));
        if (fp != (float)(sequence % 1024u) * 0.25f) {
            demo_stats.fpu_errors++;
        }
        sequence++;
        fp = (sequence % 1024u) ? fp + 0.25f : 0.0f;
        configASSERT(xQueueSend(demo_queue, &sequence, portMAX_DELAY) == pdPASS);
        demo_stats.produced = sequence;
    }
}

static void consumer_task(void *arg)
{
    (void)arg;
    uint32_t expected = 1, received;
    float fp = 0.0f;
    for (;;) {
        configASSERT(xQueueReceive(demo_queue, &received, portMAX_DELAY) == pdPASS);
        __asm volatile("" : "+f"(fp));
        if (fp != (float)((expected - 1u) % 1024u) * 0.5f) {
            demo_stats.fpu_errors++;
        }
        if (received != expected) {
            demo_stats.sequence_errors++;
        }
        expected = received + 1u;
        fp = (received % 1024u) ? fp + 0.5f : 0.0f;
        demo_stats.consumed++;
    }
}

static void timer_callback(TimerHandle_t timer)
{
    (void)timer;
    demo_stats.timer_callbacks++;
}

TickType_t demo_idle_window(TickType_t duration, uint32_t *hardware_ms)
{
    /* A bounded diagnostic window removes periodic demo activity. The CLI is
     * delayed, not polling, and incoming UART bytes remain in its RX queue. */
    vTaskSuspend(producer_handle);
    configASSERT(xTimerStop(demo_timer, portMAX_DELAY) == pdPASS);
    TickType_t start = xTaskGetTickCount();
    uint32_t hardware_start = stimer_get_tick();
    vTaskDelay(duration);
    TickType_t elapsed = xTaskGetTickCount() - start;
    *hardware_ms = (stimer_get_tick() - hardware_start) / SYSTEM_TIMER_TICK_1MS;
    reset_producer_deadline = pdTRUE;
    vTaskResume(producer_handle);
    configASSERT(xTimerStart(demo_timer, portMAX_DELAY) == pdPASS);
    return elapsed;
}

void vAssertCalled(const char *file, unsigned long line)
{
    portDISABLE_INTERRUPTS();
    console_write("\r\nFATAL: assertion in ");
    console_write(file);
    char location[24];
    snprintf(location, sizeof(location), ":%lu\r\n", line);
    console_write(location);
    for (;;) { }
}

void vApplicationMallocFailedHook(void)
{
    vAssertCalled("FreeRTOS heap exhausted", 0);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    vAssertCalled(name, 0);
}

int main(void)
{
#ifdef TL3238_ENABLE_BLE
    tl3238_ble_board_init();
#else
    PLATFORM_INIT;
    CLOCK_INIT;
#endif
    if (tl3238_app_memory_init) tl3238_app_memory_init();
    console_init();
    console_write("\r\nTL3238: starting component FreeRTOS + FreeRTOS-Plus-CLI\r\n");
    cli_register_commands();
    demo_queue = xQueueCreate(8, sizeof(uint32_t));
    configASSERT(demo_queue != NULL);
    /* Sizes are words (4 bytes). At -Os the CLI diagnostics left only 464 B
     * with 768 words; reserve another 256 B to retain the 512 B test margin.
     * Keep the radio/host stacks unchanged until RF stress is available. */
    configASSERT(xTaskCreate(console_task, "cli", 832, NULL, 2, NULL) == pdPASS);
    configASSERT(xTaskCreate(producer_task, "producer", 256, NULL, 1, &producer_handle) == pdPASS);
    configASSERT(xTaskCreate(consumer_task, "consumer", 256, NULL, 2, NULL) == pdPASS);
    demo_timer = xTimerCreate("verify", pdMS_TO_TICKS(250), pdTRUE, NULL, timer_callback);
    configASSERT(demo_timer != NULL);
    configASSERT(xTimerStart(demo_timer, 0) == pdPASS);
#ifdef TL3238_ENABLE_BLE
    tl3238_ble_start();
#endif
    vTaskStartScheduler();
    vAssertCalled("scheduler returned", 0);
    return 0;
}
