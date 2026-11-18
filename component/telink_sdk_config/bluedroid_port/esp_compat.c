#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "driver.h"
#include "tl3238_bluedroid.h"
extern void console_write(const char *text);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t bytes,
                                 void *arg, UBaseType_t priority, TaskHandle_t *out, int core)
{
    if (core != 0 && core != tskNO_AFFINITY) return pdFAIL;
    if (priority >= configMAX_PRIORITIES) return pdFAIL;
    return xTaskCreate(fn, name, (bytes + sizeof(StackType_t) - 1) / sizeof(StackType_t),
                       arg, priority, out);
}

/* FreeRTOS captures the overflow epoch and tick atomically, including tickless
 * stepping. ESP alarms have the same 1 ms resolution as the scheduler. */
int64_t esp_timer_get_time(void)
{
    TimeOut_t now;
    vTaskSetTimeOutState(&now);
    uint64_t ticks = ((uint64_t)(uint32_t)now.xOverflowCount << 32) | now.xTimeOnEntering;
    return ticks * (1000000 / configTICK_RATE_HZ);
}
uint32_t esp_log_timestamp(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

int tl3238_bd_vprintf(const char *format, va_list args)
{
    char text[384];
    int count = vsnprintf(text, sizeof(text), format, args);
    console_write(text);
    return count;
}
int tl3238_bd_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int count = tl3238_bd_vprintf(format, args);
    va_end(args);
    return count;
}

void esp_log_write(esp_log_level_t level, const char *tag, const char *fmt, ...)
{
    (void)tag;
    if (level > CONFIG_LOG_DEFAULT_LEVEL) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

size_t heap_caps_get_free_size(unsigned caps)
{
    (void)caps;
    return xPortGetFreeHeapSize();
}


int ets_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int count = vprintf(fmt, args);
    va_end(args);
    return count;
}

size_t heap_caps_get_largest_free_block(unsigned caps)
{
    (void)caps;
    HeapStats_t stats;
    vPortGetHeapStats(&stats);
    return stats.xSizeOfLargestFreeBlockInBytes;
}

void esp_fill_random(void *buffer, size_t length)
{
    /* Native driver/controller share the TRNG peripheral. Serialize its small
     * transactions against LL preemption, and never substitute a PRNG. */
    unsigned char *out = buffer;
    while (length) {
        unsigned int word;
        taskENTER_CRITICAL();
        unsigned rc = get_rand((unsigned char *)&word, sizeof(word));
        taskEXIT_CRITICAL();
        configASSERT(rc == TRNG_SUCCESS);
        size_t n = length < sizeof(word) ? length : sizeof(word);
        memcpy(out, &word, n); out += n; length -= n;
    }
}

uint32_t esp_random(void)
{
    uint32_t value;
    esp_fill_random(&value, sizeof(value));
    return value;
}

struct pan_timer {
    TimerHandle_t handle;
    esp_timer_cb_t callback;
    void *arg;
};

static void timer_callback(TimerHandle_t handle)
{
    struct pan_timer *timer = pvTimerGetTimerID(handle);
    timer->callback(timer->arg);
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (!args || !args->callback || !out) return ESP_ERR_INVALID_ARG;
    struct pan_timer *timer = calloc(1, sizeof(*timer));
    if (!timer) return ESP_ERR_NO_MEM;
    timer->callback = args->callback;
    timer->arg = args->arg;
    timer->handle = xTimerCreate(args->name ? args->name : "bd_alarm", 1, pdFALSE,
                                 timer, timer_callback);
    if (!timer->handle) { free(timer); return ESP_ERR_NO_MEM; }
    *out = timer;
    return ESP_OK;
}

static esp_err_t bd_timer_start(esp_timer_handle_t timer, uint64_t us, BaseType_t periodic)
{
    if (!timer || !us) return ESP_ERR_INVALID_ARG;
    uint64_t ticks = (us * configTICK_RATE_HZ + 999999) / 1000000;
    if (ticks >= portMAX_DELAY) return ESP_ERR_INVALID_ARG;
    vTimerSetReloadMode(timer->handle, periodic);
    return xTimerChangePeriod(timer->handle, ticks ? ticks : 1, 0) == pdPASS
           ? ESP_OK : ESP_FAIL;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t us)
{ return bd_timer_start(timer, us, pdFALSE); }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t us)
{ return bd_timer_start(timer, us, pdTRUE); }
esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    return xTimerStop(timer->handle, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

static void timer_free(void *timer, uint32_t unused)
{ (void)unused; free(timer); }

esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    if (!timer) return ESP_ERR_INVALID_ARG;
    if (xTimerDelete(timer->handle, 0) != pdPASS) return ESP_FAIL;
    /* Free only after the service task has processed deletion and any callback
     * already in flight. Failure leaves memory allocated instead of a UAF. */
    return xTimerPendFunctionCall(timer_free, timer, 0, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

bool esp_timer_is_active(esp_timer_handle_t timer)
{ return timer && xTimerIsTimerActive(timer->handle) == pdTRUE; }

/* Keep libc assertions on the same UART/fault path as FreeRTOS. */
void __assert_func(const char *file, int line, const char *function, const char *expression)
{
    portDISABLE_INTERRUPTS();
    tl3238_bd_printf("\r\n[BD] assertion %s:%d %s: %s\r\n", file, line, function, expression);
    vAssertCalled(file, line);
    for (;;) { }
}
