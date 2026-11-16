#include "ameba_soc.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_CLI.h"
#include "usbd_core.h"
#include "usbh_core.h"
#include "usb_dwc2_param.h"
#include "usb_bench.h"
#include <stdlib.h>

uintptr_t green2_usb_controller_base(void) { return USB_REG_BASE; }

/* The released Green2 PHY library exports this 32-byte table. Only its first
 * two entries are used here; controller/transfer handling belongs to CherryUSB.
 * Layout verified against lib_usb_hal.a relocations at the pinned SDK commit. */
extern const struct {
    uint8_t (*init)(uint8_t mode);
    uint8_t (*deinit)(void);
    uintptr_t reserved[6];
} usb_hal_driver;
extern uint8_t usb_hal_calibrate(uint8_t mode);

static uint32_t usb_interrupt(void *arg)
{
    (void)arg;
#if USB_BENCH_HOST
    USBH_IRQHandler(0);
#else
    USBD_IRQHandler(0);
#endif
    return 0;
}
static void hardware_init(void)
{
    InterruptDis(USB_IRQ);
    usb_hal_driver.deinit();
    configASSERT(usb_hal_driver.init(USB_BENCH_HOST ? 1 : 0) == 0);
    configASSERT(usb_hal_calibrate(USB_BENCH_HOST ? 1 : 0) == 0);
    InterruptRegister(usb_interrupt, USB_IRQ, 0, INT_PRI5);
    InterruptEn(USB_IRQ, INT_PRI5);
}
void usb_dc_low_level_init(uint8_t busid) { (void)busid; hardware_init(); }
void usb_dc_low_level_deinit(uint8_t busid) { (void)busid; InterruptDis(USB_IRQ); usb_hal_driver.deinit(); }
void usb_hc_low_level_init(struct usbh_bus *bus) { (void)bus; hardware_init(); }
void usb_hc_low_level_deinit(struct usbh_bus *bus) { (void)bus; InterruptDis(USB_IRQ); usb_hal_driver.deinit(); }
void usbd_dwc2_delay_ms(uint8_t ms) { DelayMs(ms); }
uint32_t usbd_dwc2_get_system_clock(void) { return 240000000; }

void dwc2_get_user_params(uint32_t base, struct dwc2_user_params *params)
{
    (void)base;
    memset(params, 0, sizeof(*params));
    params->phy_type = DWC2_PHY_TYPE_PARAM_UTMI;
    params->phy_utmi_width = 16;
    params->device_dma_enable = true;
    params->device_rx_fifo_size = 512;
    params->device_tx_fifo_size[0] = 32;
    params->device_tx_fifo_size[4] = 256;
    params->host_rx_fifo_size = 512;
    params->host_nperio_tx_fifo_size = 256;
    params->host_perio_tx_fifo_size = 128;
    params->b_session_valid_override = true;
}

#ifndef AT_NAN_USB_BRIDGE
static BaseType_t command(char *out, size_t len, const char *line)
{
    (void)out; (void)len;
    char copy[128]; char *argv[6]; unsigned argc = 0;
    if (strlen(line) >= sizeof(copy)) return pdFALSE;
    strcpy(copy, line);
    char *save = NULL;
    for (char *p = strtok_r(copy, " \t", &save); p && argc < 6; p = strtok_r(NULL, " \t", &save)) argv[argc++] = p;
    if (usb_bench_command(argc, argv) < 0) printf("[usbbench] command failed\n");
    return pdFALSE;
}
static const CLI_Command_Definition_t command_def = {
    "usbbench", "usbbench [run MiB block verify rounds]: CherryUSB Bulk benchmark\r\n", command, -1
};
static void startup(void *arg)
{
    (void)arg;
    usb_bench_init(USB_REG_BASE);
    vTaskDelete(NULL);
}
void green2_usb_bench_register(void)
{
    FreeRTOS_CLIRegisterCommand(&command_def);
    configASSERT(xTaskCreate(startup, "usb_start", 1536, NULL, 5, NULL) == pdPASS);
}
#endif
