#include "board.h"
#include "bflb_uart.h"
#include "bflb_l1c.h"
#include "bl618dg_memorymap.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "shell.h"
#include "mm.h"
#ifdef CONFIG_NAN_NATIVE_WHC_USB
#include "whc_usb.h"
extern void bl618dg_native_init(uintptr_t);
#else
#include "nan_usb.h"
extern void bl618dg_nan_receive(const struct nan_usb_record *, const void *);
#endif
#include "nan_heap.h"
#include <stdio.h>
#include <string.h>
extern void shell_init_with_task(struct bflb_device_s *uart);
extern void green2_nan_cli_register(void);
extern void mbedtls_threading_freertos_init(void);
extern int bl618dg_nan_protocol_selftest(void);

static int nan_command(int argc, char **argv)
{
    char line[40], out[512];
    snprintf(line, sizeof(line), "nan %s", argc > 1 ? argv[1] : "status");
    FreeRTOS_CLIProcessCommand(line, out, sizeof(out));
    printf("%s", out);
#ifdef CONFIG_NAN_NATIVE_WHC_USB
    whc_usb_status();
#else
    nan_usb_status();
#endif
    bl618dg_nan_heap_status();
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(nan_command, nan, NAN status/start/stop.);
static void data_memory_test(void)
{
#ifdef CONFIG_PSRAM
    const size_t size = 2 * 1024 * 1024;
#else
    const size_t size = 256 * 1024;
    configASSERT(kfree_size(MM_HEAP_PSRAM_0) == 0);
#endif
    uint32_t *p = kmalloc(size, NAN_DATA_HEAP_FLAGS | MM_FLAG_ALIGN_32BYTE);
    configASSERT(p);
    configASSERT(mm_find_heap_by_addr(p)->heap_id == NAN_DATA_HEAP);
    for (unsigned round = 0; round < 2; ++round) {
        for (size_t i = 0; i < size / 4; ++i) p[i] = (0x9e3779b9u * i) ^ (round ? 0xaaaaaaaa : 0x55555555);
        bflb_l1c_dcache_clean_invalidate_range(p, size);
        for (size_t i = 0; i < size / 4; ++i)
            configASSERT(p[i] == ((0x9e3779b9u * i) ^ (round ? 0xaaaaaaaa : 0x55555555)));
    }
    printf("[NAN] " NAN_DATA_HEAP_NAME " %lu KiB read/write/cache test PASS address=%p\n",
           (unsigned long)(size / 1024), p);
    kfree(p);
}
static void startup(void *arg)
{
    (void)arg;
    shell_init_with_task(bflb_device_get_by_name("uart0"));
    bl618dg_nan_heap_selftest();
    configASSERT(bl618dg_nan_protocol_selftest() == 0);
    mbedtls_threading_freertos_init();
#ifdef CONFIG_NAN_USB_DIAGNOSTICS
    extern void bl618dg_nan_profile_init(void);
    bl618dg_nan_profile_init();
#endif
#ifdef CONFIG_NAN_NATIVE_WHC_USB
    bl618dg_native_init(USB_BASE);
#else
    nan_usb_init(USB_BASE, bl618dg_nan_receive);
#endif
    green2_nan_cli_register();
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    /* Also keep mbedTLS/libc allocations out of the kernel's small OCRAM heap. */
#ifdef CONFIG_PSRAM
    const uint32_t heaps[] = {MM_HEAP_PSRAM_0, MM_HEAP_OCRAM_0, MM_HEAP_WRAM_0};
#else
    const uint32_t heaps[] = {MM_HEAP_WRAM_0, MM_HEAP_OCRAM_0};
#endif
    configASSERT(mm_heap_set_any_alloc_order(heaps, sizeof(heaps) / sizeof(heaps[0])) == 0);
    printf("[NAN] BL618DG AP FreeRTOS + " NAN_DATA_HEAP_NAME " / Green2 USB radio %s %s\n", __DATE__, __TIME__);
#ifdef CONFIG_NAN_SRAM_HOTCODE
    extern const char __nan_hot_text_start__[], __nan_hot_text_end__[];
    extern const char __nan_hot_rodata_start__[], __nan_hot_rodata_end__[];
    printf("[NAN] SRAM hot text=%p/%lu rodata=%p/%lu bytes\n", __nan_hot_text_start__,
        (unsigned long)(__nan_hot_text_end__ - __nan_hot_text_start__), __nan_hot_rodata_start__,
        (unsigned long)(__nan_hot_rodata_end__ - __nan_hot_rodata_start__));
#endif
    data_memory_test();
    configASSERT(xTaskCreate(startup, "nan_start", 4096, NULL, 20, NULL) == pdPASS);
    vTaskStartScheduler();
    for (;;) {}
}
