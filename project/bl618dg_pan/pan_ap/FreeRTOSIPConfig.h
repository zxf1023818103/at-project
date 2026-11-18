#pragma once
#define FREERTOS_IP_CONFIG_H
#define ipconfigIPv4_BACKWARD_COMPATIBLE 0
#define ipconfigUSE_IPv4 1
#define ipconfigUSE_IPv6 0
#define ipconfigUSE_RA 0
#define ipconfigUSE_TCP 1
#define ipconfigUSE_DHCP 1
#define ipconfigUSE_DNS 1
#define ipconfigUSE_DNS_CACHE 1
#define ipconfigDNS_CACHE_ADDRESSES_PER_ENTRY 4
#define ipconfigDNS_USE_CALLBACKS 0
#define ipconfigUSE_LLMNR 0
#define ipconfigUSE_MDNS 0
#define ipconfigUSE_NBNS 0
#define ipconfigDHCP_REGISTER_HOSTNAME 1
#define ipconfigUSE_NETWORK_EVENT_HOOK 1
#define ipconfigSUPPORT_NETWORK_DOWN_EVENT 1
#define ipconfigUSE_DHCP_HOOK 0
#define ipconfigUSE_TCP_WIN 1
#define ipconfigTCP_WIN_SEG_COUNT 16
#define ipconfigTCP_RX_BUFFER_LENGTH (4 * 1460)
#define ipconfigTCP_TX_BUFFER_LENGTH (4 * 1460)
#define ipconfigTCP_MSS 1460
#define ipconfigNETWORK_MTU 1500
#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS 16
#define ipconfigEVENT_QUEUE_LENGTH 21
#define ipconfigBYTE_ORDER pdFREERTOS_LITTLE_ENDIAN
#define ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM 0
#define ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM 0
#define ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES 0
#define ipconfigETHERNET_DRIVER_FILTERS_PACKETS 0
#define ipconfigZERO_COPY_RX_DRIVER 0
#define ipconfigZERO_COPY_TX_DRIVER 0
#define ipconfigUSE_LINKED_RX_MESSAGES 0
#define ipconfigIP_TASK_PRIORITY (configMAX_PRIORITIES - 4)
#define ipconfigIP_TASK_STACK_SIZE_WORDS 1536
#define ipconfigARP_CACHE_ENTRIES 6
#define ipconfigMAX_ARP_RETRANSMISSIONS 5
#define ipconfigARP_MAX_RETRIES 5
#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD pdMS_TO_TICKS(30000)
#define ipconfigSUPPORT_SELECT_FUNCTION 0
#define ipconfigSUPPORT_OUTGOING_PINGS 1
#define ipconfigREPLY_TO_INCOMING_PINGS 1
#define ipconfigHAS_PRINTF 0
#define ipconfigHAS_DEBUG_PRINTF 0
