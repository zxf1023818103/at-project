/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "FreeRTOS_CLI.h"
#include "stream_buffer.h"
#include "semphr.h"
#include "driver.h"
#include <string.h>

#define LINE_CAPACITY 128
static StreamBufferHandle_t rx_stream;
static SemaphoreHandle_t tx_mutex;
#define RX_DMA_CHANNEL DMA2
#define RX_DMA_BLOCK_SIZE 128u
#define RX_DMA_BLOCK_COUNT 4u
static struct {
    volatile uint32_t length; /* Written by DMA on timeout or a full block. */
    uint8_t bytes[RX_DMA_BLOCK_SIZE];
} rx_dma[RX_DMA_BLOCK_COUNT] __attribute__((aligned(4)));
static dma_chain_config_t rx_descriptors[RX_DMA_BLOCK_COUNT];
static unsigned rx_dma_read;
volatile uint32_t console_rx_dropped, console_rx_errors, console_tx_errors;

void console_write(const char *text)
{
    /* CLI and BLE diagnostics share UART0. Fault handlers have MIE masked and
     * must remain usable even if the interrupted task owns this mutex. */
    BaseType_t locked = tx_mutex && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        !xPortIsInsideInterrupt() && (read_csr(NDS_MSTATUS) & 8u);
    if (locked) configASSERT(xSemaphoreTake(tx_mutex, portMAX_DELAY) == pdPASS);
    while (*text) {
        if (uart_send_byte(UART0, (unsigned char)*text++) != DRV_API_SUCCESS) {
            console_tx_errors++;
        }
    }
    if (locked) configASSERT(xSemaphoreGive(tx_mutex) == pdPASS);
}

void console_init(void)
{
    unsigned short divider;
    unsigned char width;
    uart_clk_en(UART0);
    uart_hw_fsm_reset(UART0);
    uart_set_pin(UART0, GPIO_FC_PA1, GPIO_FC_PA2);
    uart_cal_div_and_bwpc(115200, sys_clk.pclk * 1000000u, &divider, &width);
    uart_init(UART0, divider, width, UART_PARITY_NONE, UART_STOP_BIT_ONE);
    uart_set_cts_dis(UART0);
    uart_set_rts_dis(UART0);
    uart_clr_irq_mask(UART0, UART_RX_IRQ_MASK | UART_TX_IRQ_MASK | UART_RXDONE_MASK | UART_ERR_IRQ_MASK | UART_TXDONE_MASK);
    uart_clr_irq_status(UART0, UART_RXBUF_IRQ_STATUS);
    rx_stream = xStreamBufferCreate(512, 1);
    configASSERT(rx_stream != NULL);
    tx_mutex = xSemaphoreCreateMutex();
    configASSERT(tx_mutex != NULL);
    uart_set_rx_timeout_with_exp(UART0, width, 12, UART_BW_MUL2, 0);
    /* The eight-byte hardware FIFO cannot cover BLE interrupt latency. A
     * cyclic DMA list keeps receiving while the CPU services the radio. */
    dma_chn_dis(RX_DMA_CHANNEL);
    dma_set_llp_irq_mode(RX_DMA_CHANNEL, DMA_INTERRUPT_MODE);
    dma_set_irq_mask(RX_DMA_CHANNEL, TC_MASK);
    uart_set_dma_chain_llp(UART0, RX_DMA_CHANNEL, rx_dma[0].bytes,
                          RX_DMA_BLOCK_SIZE, &rx_descriptors[1]);
    for (unsigned i = 0; i < RX_DMA_BLOCK_COUNT; i++) {
        uart_rx_dma_add_list_element(UART0, RX_DMA_CHANNEL, &rx_descriptors[i],
            &rx_descriptors[(i + 1u) % RX_DMA_BLOCK_COUNT], rx_dma[i].bytes, RX_DMA_BLOCK_SIZE);
    }
    dma_clr_tc_irq_status(BIT(RX_DMA_CHANNEL));
    dma_chn_en(RX_DMA_CHANNEL);
    plic_interrupt_enable(IRQ_DMA);
}

BaseType_t console_rx_pending(void)
{
    return dma_get_tc_irq_status(BIT(RX_DMA_CHANNEL)) != 0;
}

/* Called with full task context saved by entry_irq5 in the RTOS port. */
BaseType_t console_rx_isr(void)
{
    BaseType_t woken = pdFALSE;
    if (!console_rx_pending()) return woken;
    dma_clr_tc_irq_status(BIT(RX_DMA_CHANNEL));
    __asm volatile("fence iorw, iorw" ::: "memory");
    if (uart_get_irq_status(UART0, UART_RX_ERR)) {
        console_rx_errors++;
        uart_clr_irq_status(UART0, UART_RXBUF_IRQ_STATUS);
    }
    for (unsigned i = 0; i < RX_DMA_BLOCK_COUNT; i++) {
        uint32_t length = rx_dma[rx_dma_read].length;
        if (!length) break;
        if (length > RX_DMA_BLOCK_SIZE) {
            console_rx_errors++;
            length = RX_DMA_BLOCK_SIZE;
        }
        size_t sent = xStreamBufferSendFromISR(rx_stream, rx_dma[rx_dma_read].bytes, length, &woken);
        console_rx_dropped += length - sent;
        rx_dma[rx_dma_read].length = 0;
        rx_dma_read = (rx_dma_read + 1u) % RX_DMA_BLOCK_COUNT;
    }
    return woken;
}

void console_task(void *arg)
{
    (void)arg;
    char line[LINE_CAPACITY];
    char *output = FreeRTOS_CLIGetOutputBuffer();
    size_t length = 0;
    BaseType_t overflow = pdFALSE, last_was_cr = pdFALSE;
    console_write("FreeRTOS " tskKERNEL_VERSION_NUMBER " ready. PA1=TX PA2=RX, 115200 8N1\r\nType help for commands.\r\ntl3238> ");
    for (;;) {
        uint8_t byte;
        configASSERT(xStreamBufferReceive(rx_stream, &byte, 1, portMAX_DELAY) == 1);
        if (byte == '\n' && last_was_cr) {
            last_was_cr = pdFALSE;
            continue;
        }
        last_was_cr = (byte == '\r');
        if (byte == '\r' || byte == '\n') {
            console_write("\r\n");
            line[length] = '\0';
            if (overflow) {
                console_write("ERROR: command too long (max 127 characters)\r\n");
            } else if (length) {
                BaseType_t more;
                do {
                    output[0] = '\0';
                    more = FreeRTOS_CLIProcessCommand(line, output, configCOMMAND_INT_MAX_OUTPUT_SIZE);
                    console_write(output);
                } while (more != pdFALSE);
            }
            length = 0;
            overflow = pdFALSE;
            console_write("tl3238> ");
        } else if (byte == 3) {
            length = 0;
            overflow = pdFALSE;
            console_write("^C\r\ntl3238> ");
        } else if (byte == 8 || byte == 127) {
            if (!overflow && length) {
                length--;
                console_write("\b \b");
            }
        } else if (byte >= 32 && byte <= 126 && !overflow) {
            if (length < sizeof(line) - 1u) {
                line[length++] = (char)byte;
                char echo[2] = {(char)byte, '\0'};
                console_write(echo);
            } else {
                overflow = pdTRUE; /* Discard the entire line, never execute a prefix. */
            }
        }
    }
}
