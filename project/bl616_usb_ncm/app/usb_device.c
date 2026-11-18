/* Descriptor template: CherryUSB 1cf7193, Apache-2.0, Copyright 2026 54fire. */
#include "usbd_core.h"
#include "usbd_cdc_ncm.h"
#include "ncm_tcp.h"
#include "ncm_identity.h"
#ifdef BL618DG
#include "bl618dg_memorymap.h"
#else
#include "bl616_memorymap.h"
#endif
/*!< endpoint address */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0xFFFF
#define USBD_PID           NCM_PRODUCT_ID
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_NCM_DESCRIPTOR_LEN)

/* str idx = 4 is for mac address: aa:bb:cc:dd:ee:ff*/
#define CDC_NCM_MAC_STRING_INDEX 4

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_NCM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, CDC_NCM_MAC_STRING_INDEX)
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_NCM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, CDC_NCM_MAC_STRING_INDEX)
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_NCM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, CDC_NCM_MAC_STRING_INDEX)
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_NCM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, CDC_NCM_MAC_STRING_INDEX)
};

static const char *string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    "CherryUSB",                  /* Manufacturer */
    NCM_CHIP_NAME " FreeRTOS TCP NCM", /* Product */
    NCM_SERIAL,
    NCM_HOST_MAC_STRING,
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return config_descriptor_fs;
    } else {
        return NULL;
    }
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return other_speed_config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return other_speed_config_descriptor_fs;
    } else {
        return NULL;
    }
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;

    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

const struct usb_descriptor cdc_ncm_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback
};


static struct usbd_interface control_interface,data_interface;
static void usb_event(uint8_t busid,uint8_t event)
{
    (void)busid;ncm_tcp_usb_event(event);
    if(event==USBD_EVENT_CONFIGURED)printf("[ncm] USB configured speed=%u\r\n",usbd_get_port_speed(0));
}
void usb_device_start(void)
{
    uint8_t host_mac[6]=NCM_HOST_MAC;
    usbd_desc_register(0,&cdc_ncm_descriptor);
    usbd_add_interface(0,usbd_cdc_ncm_init_intf(&control_interface,CDC_INT_EP,CDC_OUT_EP,CDC_IN_EP,host_mac));
    usbd_add_interface(0,usbd_cdc_ncm_init_intf(&data_interface,CDC_INT_EP,CDC_OUT_EP,CDC_IN_EP,host_mac));
    int result=usbd_initialize(0,USB_BASE,usb_event);
    printf("[ncm] USB initialise result=%d\r\n",result);
}
