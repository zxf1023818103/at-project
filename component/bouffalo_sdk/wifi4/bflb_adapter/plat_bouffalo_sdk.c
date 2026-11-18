#include <assert.h>
#include <stdio.h>
#include "wifi_mgmr_ext.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "platform_al.h"
#include "bflb_irq.h"
#include "bl602_aon.h"
#include "bflb_sec_trng.h"
#include "stdbool.h"

#ifdef CFG_CHIP_BL602 
#include "bl602.h"
#endif
/* Direct synchronous WiFi-event dispatch (no bflb_async_event queue). wifi_mgmr posts events from its
 * own tasks (the wifi_mgmr state-machine task / the FreeRTOS+TCP IP task for GOT_IP / net_boot for
 * INIT_DONE); the registered handlers run INLINE in that posting context. Mirrors AmebaD's
 * wifi_indication -> rtw_indicate_event_handle. Handlers MUST stay non-blocking (set a bit / notify /
 * spawn a task). Two consumers register via platform_register_event(): wifi_mgmr's internal handler and
 * the app's wifi_event_handler (src/main.cpp). */
#define PLAT_MAX_EVENT_CB 3
static struct { pfn_wifi_event cb; void *arg; } s_evt_cbs[PLAT_MAX_EVENT_CB];

volatile bool sys_log_all_enable = 1;

/// Stack size of the WIFI task
#define WIFI_TASK_STACK_SIZE 1024

/// Handle of the WiFi task
static TaskHandle_t wifi_task_handle;

/* Main wifi stack entry point */
extern void wifi_main(void *param);

void wifi_task_create(void) {
  BaseType_t res;

  wifi_hosal_pm_init();
  res = xTaskCreate(wifi_main, "wifi fw", WIFI_TASK_STACK_SIZE, NULL, 30,
                    &wifi_task_handle);

  configASSERT(res == pdTRUE);

  vTaskDelay(100);
  platform_post_event(EV_WIFI, CODE_WIFI_ON_INIT_DONE, 0);
}

int platform_register_event(int catalogue, pfn_wifi_event cb, void *arg)
{
    (void) catalogue;
    for (int i = 0; i < PLAT_MAX_EVENT_CB; i++) {
        if (s_evt_cbs[i].cb == NULL) {
            s_evt_cbs[i].cb = cb;
            s_evt_cbs[i].arg = arg;
            return 0;
        }
    }
    return -1;   /* callback table full - bump PLAT_MAX_EVENT_CB */
}

int platform_unregister_event(int catalogue, pfn_wifi_event cb, void *arg)
{
    (void) catalogue; (void) arg;
    for (int i = 0; i < PLAT_MAX_EVENT_CB; i++) {
        if (s_evt_cbs[i].cb == cb) {
            s_evt_cbs[i].cb = NULL;
            s_evt_cbs[i].arg = NULL;
            return 0;
        }
    }
    return -1;
}

/**
****************************************************************************************
* @brief Post a WiFi event to the upper layer - dispatched SYNCHRONOUSLY to every registered handler,
*        in the posting task's context (no async queue / timer-daemon deferral). See s_evt_cbs.
****************************************************************************************
*/
void platform_post_event(int catalogue, int code1, int code2)
{
    platform_event_t ev = { .code = code1 };
    (void) catalogue;
    for (int i = 0; i < PLAT_MAX_EVENT_CB; i++) {
        if (s_evt_cbs[i].cb != NULL) {
            s_evt_cbs[i].cb(&ev, (void *)(uintptr_t) code2);
        }
    }
}

int platform_wifi_enable_irq(void)
{
extern void mac_irq(void);
extern void bl_irq_handler(void);
    bflb_irq_attach(WIFI_IRQn, (irq_callback)mac_irq, NULL);
    bflb_irq_attach(WIFI_IPC_PUBLIC_IRQn, (irq_callback)bl_irq_handler, NULL);
    bflb_irq_enable(WIFI_IRQn);
    bflb_irq_enable(WIFI_IPC_PUBLIC_IRQn);

    //NVIC_SetPriority((IRQn_Type)5, 0);
    //NVIC_EnableIRQ((IRQn_Type)5);
    puts("Enable BMX IRQ\r\n");
    //NVIC_EnableIRQ((IRQn_Type)0);
    //NVIC_EnableIRQ((IRQn_Type)1);
    //NVIC_EnableIRQ((IRQn_Type)2);
    //NVIC_EnableIRQ((IRQn_Type)3);
    //NVIC_EnableIRQ((IRQn_Type)4);
    //*(uint32_t*)0x40000050 = ((0xF << 28) | (1 << 24));

    return 0;
}

void platform_post_delayed_action(int delay_ms, void (*callback)(void* arg), void *arg)
{

}

void platform_sys_capcode_update(uint8_t capin, uint8_t capout)
{

}

uint8_t platform_sys_capcode_get(void)
{
    return AON_Get_Xtal_CapCode();
}

int platform_get_random(unsigned char *buf, size_t len)
{
    int ret;

    taskENTER_CRITICAL();
    ret = bflb_trng_readlen(buf, len);
    taskEXIT_CRITICAL();
    return ret;
}

long platform_rand(void)
{
    return random();
}

