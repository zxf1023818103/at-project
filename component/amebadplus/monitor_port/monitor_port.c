#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include "monitor.h"
static volatile int claimed;
static int sleep_poll;
static StreamBufferHandle_t rx;
static StaticStreamBuffer_t rx_control;
static uint8_t rx_storage[2049];
static u32 console_irq(void *unused)
{
    (void)unused;
    BaseType_t wake=pdFALSE;
    for (unsigned n=0;n<128 && LOGUART_Readable();++n) {
        uint8_t c=DiagGetChar(0);
        (void)xStreamBufferSendFromISR(rx,&c,1,&wake);
    }
    /* RX timeout/line-status latches can survive FIFO drain after clock gating.
     * Acknowledge them explicitly to prevent a level-triggered IRQ storm. */
    LOGUART_INTClear(LOGUART_DEV,LOGUART_BIT_TOICF|LOGUART_BIT_RLSICF);
    portYIELD_FROM_ISR(wake);
    return 0;
}
void amebadplus_console_claim(void)
{
    claimed = 1;
    LOGUART_INTConfig(LOGUART_DEV,0xffffffff,DISABLE);
    InterruptDis(UART_LOG_IRQ);
}
void monitor_port_init(void)
{
    LOGUART_INTConfig(LOGUART_DEV, 0xffffffff, DISABLE);
    rx=xStreamBufferCreateStatic(sizeof(rx_storage),1,rx_storage,&rx_control);
    configASSERT(rx);
    InterruptRegister(console_irq,UART_LOG_IRQ,0,INT_PRI5);
    InterruptEn(UART_LOG_IRQ,INT_PRI5);
    LOGUART_INTCoreConfig(LOGUART_DEV,LOGUART_BIT_INTR_MASK_KM0,DISABLE);
    LOGUART_INTCoreConfig(LOGUART_DEV,LOGUART_BIT_INTR_MASK_KM4,ENABLE);
    LOGUART_INTConfig(LOGUART_DEV,LOGUART_BIT_ERBI|LOGUART_BIT_ETOI,ENABLE);
}
const char *monitor_core_name(void) { return "Dplus-KM4"; }
int monitor_rx_get(uint8_t *c)
{
    if (claimed) { vTaskDelay(pdMS_TO_TICKS(20)); return 0; }
    static int first = 1;
    if (first) { first = 0; return -1; }
    if (sleep_poll) {
        if (xStreamBufferReceive(rx,c,1,0) == 1) return 1;
        if (LOGUART_Readable()) { *c=DiagGetChar(0); return 1; }
        vTaskDelay(pdMS_TO_TICKS(100));
        return 0;
    }
    return xStreamBufferReceive(rx,c,1,pdMS_TO_TICKS(100)) == 1;
}
/* During explicit SleepCG, retain RX in the FIFO and sample on the bounded
 * PMC timer. Clear latched errors before restoring the interrupt console. */
void amebadplus_console_sleep(int enter)
{
    taskENTER_CRITICAL();
    LOGUART_INTClear(LOGUART_DEV,LOGUART_BIT_TOICF|LOGUART_BIT_RLSICF);
    LOGUART_INTConfig(LOGUART_DEV,LOGUART_BIT_ERBI|LOGUART_BIT_ETOI,
                     enter ? DISABLE : ENABLE);
    sleep_poll=enter;
    taskEXIT_CRITICAL();
}
int monitor_after_command(void) { return claimed; }
