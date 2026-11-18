#include <stdio.h>
#include "board.h"
#include "bflb_uart.h"
#include "bl618dg_memorymap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "shell.h"
#include "usb_bench.h"
extern void shell_init_with_task(struct bflb_device_s *uart);
static int benchmark_command(int argc, char **argv) { return usb_bench_command(argc, argv); }
SHELL_CMD_EXPORT_ALIAS(benchmark_command, usbbench, CherryUSB Bulk throughput.);
static void startup(void *arg)
{
    (void)arg;
    shell_init_with_task(bflb_device_get_by_name("uart0"));
    usb_bench_init(USB_BASE);
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    printf("[usbbench] BL618DG built %s %s\r\n", __DATE__, __TIME__);
    configASSERT(xTaskCreate(startup, "usb_start", 1536, NULL, 20, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}
