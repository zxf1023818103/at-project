/*
 * FreeRTOS-Plus-TCP config for AmebaD KM4 — minimal DHCP-only IPv4 bring-up.
 *
 * Goal of this slice: associate to an AP and obtain an IP via DHCP, nothing
 * more (no TCP, no IPv6, no DNS). Uses the LEGACY single-interface API
 * (FreeRTOS_IPInit + xNetworkInterfaceInitialise(void)) — one STA endpoint is
 * enough for "get an IP"; multi-endpoint (STA+AP) comes later.
 *
 * The Ethernet frames are copied between the Realtek WiFi driver and the stack
 * (BufferAllocation_2, no zero-copy) — simplest to get right first.
 */
#ifndef FREERTOS_IP_CONFIG_H
#define FREERTOS_IP_CONFIG_H

/* Use the legacy (single-interface) FreeRTOS_IPInit() API. */
#define ipconfigIPv4_BACKWARD_COMPATIBLE        1

#define ipconfigUSE_IPv6                        0
#define ipconfigUSE_TCP                         1       /* iperf throughput test */
#define ipconfigUSE_DHCP                        1

/* TCP tuning for throughput. Window must cover the bandwidth-delay product;
 * with power-save OFF the WiFi RTT drops to ~1-2 ms, but keep generous buffers
 * so the sender can keep the pipe full. */
#define ipconfigTCP_WIN_SEG_COUNT               64
#define ipconfigUSE_TCP_WIN                      1
#define ipconfigTCP_RX_BUFFER_LENGTH            ( 24 * 1460 )   /* ~35 KB */
#define ipconfigTCP_TX_BUFFER_LENGTH            ( 24 * 1460 )   /* ~35 KB */
#define ipconfigTCP_MSS                         ( 1500 - 40 )   /* 1460 */
#define ipconfigTCP_TIME_TO_LIVE                128
#define ipconfigIGNORE_UNKNOWN_PACKETS          0
#define ipconfigDHCP_REGISTER_HOSTNAME          0
#define ipconfigUSE_DNS                         1       /* FreeRTOS_gethostbyname: resolve the AWS IoT endpoint (tcp_sockets_wrapper) */
#define ipconfigUSE_LLMNR                       0
#define ipconfigUSE_NBNS                        0

#define ipconfigUSE_NETWORK_EVENT_HOOK          1       /* vApplicationIPNetworkEventHook -> print IP */
#define ipconfigUSE_DHCP_HOOK                   0       /* no xApplicationDHCPHook needed */

/* Driver does NOT do checksum offload — let the stack compute in software. */
#define ipconfigDRIVER_INCLUDED_RX_IP_CHECKSUM  0
#define ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM  0
#define ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES   1
#define ipconfigETHERNET_DRIVER_FILTERS_PACKETS       0
#define ipconfigZERO_COPY_RX_DRIVER             0
#define ipconfigZERO_COPY_TX_DRIVER             0
#define ipconfigUSE_LINKED_RX_MESSAGES          0

#define ipconfigNETWORK_MTU                     1500
#define ipconfigBYTE_ORDER                      pdFREERTOS_LITTLE_ENDIAN

/* Buffers (BufferAllocation_2 = heap). Bump for TCP throughput. */
#define ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS  40
#define ipconfigEVENT_QUEUE_LENGTH              ( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS + 5 )
#define ipconfigUSE_CALLBACKS                   0

/* IP task. configMAX_PRIORITIES comes from FreeRTOSConfig.h via FreeRTOS.h. */
#define ipconfigIP_TASK_PRIORITY                ( configMAX_PRIORITIES - 2 )
#define ipconfigIP_TASK_STACK_SIZE_WORDS        ( configMINIMAL_STACK_SIZE * 5 )

#define ipconfigUSE_ARP_REMOVE_ENTRY            0
#define ipconfigUSE_ARP_REVERSED_LOOKUP         0
#define ipconfigARP_CACHE_ENTRIES               6
#define ipconfigMAX_ARP_RETRANSMISSIONS         5
#define ipconfigMAX_ARP_AGE                     150

#define ipconfigCHECK_IP_QUEUE_SPACE            0
#define ipconfigSUPPORT_SELECT_FUNCTION         0
#define ipconfigSUPPORT_OUTGOING_PINGS          0
#define ipconfigREPLY_TO_INCOMING_PINGS         1

/* DHCP timing — give the AP time to respond. */
#define ipconfigMAXIMUM_DISCOVER_TX_PERIOD      ( pdMS_TO_TICKS( 30000 ) )

/* Debug/asserts off for bring-up size; flip on if something misbehaves. */
#define ipconfigHAS_DEBUG_PRINTF                0
#define ipconfigHAS_PRINTF                      0

#endif /* FREERTOS_IP_CONFIG_H */
