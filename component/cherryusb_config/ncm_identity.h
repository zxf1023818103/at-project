#pragma once
/* Per-chip lab identity allows both USB fixtures to coexist on one host. */
#if defined(BL618DG)
#define NCM_CHIP_NAME "BL618DG"
#define NCM_HOSTNAME "bl618dg-usb"
#define NCM_PRODUCT_ID 0x618d
#define NCM_SERIAL "BL618DGNCM0001"
#define NCM_HOST_MAC_STRING "02618D000002"
#define NCM_HOST_MAC {0x02,0x61,0x8d,0,0,2}
#define NCM_DEVICE_MAC {0x02,0x61,0x8d,0,0,1}
#define NCM_SUBNET 79
#define NCM_DEVICE_IP "192.168.79.1"
#elif defined(BL616)
#define NCM_CHIP_NAME "BL616"
#define NCM_HOSTNAME "bl616-usb"
#define NCM_PRODUCT_ID 0x6161
#define NCM_SERIAL "BL616NCM0001"
#define NCM_HOST_MAC_STRING "026160000002"
#define NCM_HOST_MAC {0x02,0x61,0x60,0,0,2}
#define NCM_DEVICE_MAC {0x02,0x61,0x60,0,0,1}
#define NCM_SUBNET 78
#define NCM_DEVICE_IP "192.168.78.1"
#else
#error "USB NCM validation supports BL616 and BL618DG only"
#endif
