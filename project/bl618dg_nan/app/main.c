#include "board.h"
#include "bflb_uart.h"
#include "bflb_l1c.h"
#include "bl618dg_memorymap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "shell.h"
#include "mm.h"
#include "nan_usb.h"
#include "nan_heap.h"
#include <stdio.h>
#include <string.h>
extern void shell_init_with_task(struct bflb_device_s *uart);
extern void green2_nan_cli_register(void);
extern void bl618dg_nan_receive(const struct nan_usb_record *, const void *);
extern void mbedtls_threading_freertos_init(void);
extern int bl618dg_nan_protocol_selftest(void);

static int nan_command(int argc, char **argv)
{
    char line[40], out[512];
    snprintf(line, sizeof(line), "nan %s", argc > 1 ? argv[1] : "status");
    FreeRTOS_CLIProcessCommand(line, out, sizeof(out));
    printf("%s", out); nan_usb_status(); bl618dg_nan_heap_status();
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(nan_command, nan, NAN status/start/stop.);
static void psram_test(void)
{
    const size_t size = 2 * 1024 * 1024;
    uint32_t *p = kmalloc(size, MM_FLAG_HEAP_PSRAM_0 | MM_FLAG_ALIGN_32BYTE);
    configASSERT(p);
    for (unsigned round = 0; round < 2; ++round) {
        for (size_t i = 0; i < size / 4; ++i) p[i] = (0x9e3779b9u * i) ^ (round ? 0xaaaaaaaa : 0x55555555);
        bflb_l1c_dcache_clean_invalidate_range(p, size);
        for (size_t i = 0; i < size / 4; ++i)
            configASSERT(p[i] == ((0x9e3779b9u * i) ^ (round ? 0xaaaaaaaa : 0x55555555)));
    }
    printf("[NAN] PSRAM 2 MiB read/write/cache test PASS address=%p\n", p);
    kfree(p);
}
static void startup(void *arg)
{
    (void)arg;
    shell_init_with_task(bflb_device_get_by_name("uart0"));
    bl618dg_nan_heap_selftest();
    configASSERT(bl618dg_nan_protocol_selftest() == 0);
    mbedtls_threading_freertos_init();
    nan_usb_init(USB_BASE, bl618dg_nan_receive);
    green2_nan_cli_register();
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    /* Also keep mbedTLS/libc allocations out of the kernel's small OCRAM heap. */
    const uint32_t heaps[] = {MM_HEAP_PSRAM_0, MM_HEAP_OCRAM_0, MM_HEAP_WRAM_0};
    configASSERT(mm_heap_set_any_alloc_order(heaps, sizeof(heaps) / sizeof(heaps[0])) == 0);
    printf("[NAN] BL618DG AP FreeRTOS + PSRAM / Green2 USB radio %s %s\n", __DATE__, __TIME__);
    psram_test();
    configASSERT(xTaskCreate(startup, "nan_start", 4096, NULL, 20, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}
