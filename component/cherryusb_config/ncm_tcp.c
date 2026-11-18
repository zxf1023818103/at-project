/* CherryUSB NTB16 transport. DMA buffers stay owned until completion/reset.
 * USB callbacks only signal workers; TCP buffer allocation runs in task context. */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_Routing.h"
#include "NetworkBufferManagement.h"
#include "usbd_core.h"
#include "usbd_cdc_ncm.h"
#include "ncm_tcp.h"
#include "ncm_identity.h"

#define NTB_SIZE 2048
#define TX_COUNT 4
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t rx_dma[NTB_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t tx_dma[TX_COUNT][NTB_SIZE];
static uint16_t tx_length[TX_COUNT],sequence;
static unsigned tx_generation[TX_COUNT];
static volatile bool active,armed;
static volatile bool rx_ready;
static volatile unsigned generation,rx_length;
static TaskHandle_t rx_task_handle,tx_task_handle;
static QueueHandle_t tx_free,tx_ready;
static NetworkInterface_t interface;
static NetworkEndPoint_t endpoint;
static unsigned rx_packets,tx_packets,rx_drop,tx_drop,invalid_ntb;
static const uint8_t device_mac[6]=NCM_DEVICE_MAC;

static void signal_isr(TaskHandle_t task)
{
    BaseType_t wake=pdFALSE;
    if(task)vTaskNotifyGiveFromISR(task,&wake);
    portYIELD_FROM_ISR(wake);
}
void usbd_cdc_ncm_link_changed(bool connected)
{
    active=connected;generation++;armed=false;rx_length=0;rx_ready=false;
    signal_isr(rx_task_handle);
    signal_isr(tx_task_handle);
}
void ncm_tcp_usb_event(unsigned event)
{
    if(event==USBD_EVENT_RESET || event==USBD_EVENT_DISCONNECTED)
        usbd_cdc_ncm_link_changed(false);
}
void usbd_cdc_ncm_data_recv_done(uint32_t length)
{rx_length=length;rx_ready=true;signal_isr(rx_task_handle);}
void usbd_cdc_ncm_data_send_done(uint32_t length)
{(void)length;signal_isr(tx_task_handle);}

static BaseType_t link_state(NetworkInterface_t *unused)
{(void)unused;return active ? pdTRUE:pdFALSE;}
static BaseType_t initialise(NetworkInterface_t *unused)
{return link_state(unused);}
static BaseType_t output(NetworkInterface_t *unused,NetworkBufferDescriptor_t *buffer,BaseType_t release)
{
    (void)unused;unsigned index;BaseType_t result=pdFAIL;
    unsigned length=buffer->xDataLength;
    if(active && length>=14 && length<=1514 && xQueueReceive(tx_free,&index,0)==pdTRUE) {
        /* Offset 34 aligns the IP payload (Ethernet header +14) to divisor 4. */
        uint8_t *out=tx_dma[index];memset(out,0,34);
        SET_LE32(out,CDC_NCM_NTH16_SIGNATURE);SET_LE16(out+4,12);
        SET_LE16(out+6,sequence++);SET_LE16(out+8,34+length);SET_LE16(out+10,12);
        SET_LE32(out+12,CDC_NCM_NDP16_SIGNATURE_NCM0);SET_LE16(out+16,16);
        SET_LE16(out+20,34);SET_LE16(out+22,length);
        memcpy(out+34,buffer->pucEthernetBuffer,length);
        tx_length[index]=34+length;tx_generation[index]=generation;
        if(xQueueSend(tx_ready,&index,0)==pdTRUE)result=pdPASS;
        else configASSERT(xQueueSend(tx_free,&index,0)==pdTRUE);
    }
    if(result!=pdPASS)tx_drop++;
    if(release)vReleaseNetworkBufferAndDescriptor(buffer);
    return result;
}
static void deliver(const uint8_t *data,unsigned length)
{
    NetworkBufferDescriptor_t *buffer=pxGetNetworkBufferWithDescriptor(length,0);
    if(!buffer){rx_drop++;return;}
    memcpy(buffer->pucEthernetBuffer,data,length);buffer->xDataLength=length;
    buffer->pxInterface=&interface;
    buffer->pxEndPoint=FreeRTOS_MatchingEndpoint(&interface,buffer->pucEthernetBuffer);
    IPStackEvent_t event={.eEventType=eNetworkRxEvent,.pvData=buffer};
    if(xSendEventStructToIPTask(&event,0)!=pdPASS) {
        rx_drop++;vReleaseNetworkBufferAndDescriptor(buffer);
    } else rx_packets++;
}
static void parse_ntb(unsigned length)
{
    if(length<12 || length>NTB_SIZE || GET_LE32(rx_dma)!=CDC_NCM_NTH16_SIGNATURE ||
       GET_LE16(rx_dma+4)!=12)goto bad;
    unsigned block=GET_LE16(rx_dma+8),ndp=GET_LE16(rx_dma+10);
    if(block>length || block<12)goto bad;
    for(unsigned chain=0;ndp;chain++) {
        if(chain>=8 || ndp<12 || ndp+8>block ||
           GET_LE32(rx_dma+ndp)!=CDC_NCM_NDP16_SIGNATURE_NCM0)goto bad;
        unsigned size=GET_LE16(rx_dma+ndp+4);
        if(size<12 || (size%4) || ndp+size>block)goto bad;
        bool end=false;
        for(unsigned at=ndp+8;at+4<=ndp+size;at+=4) {
            unsigned offset=GET_LE16(rx_dma+at),bytes=GET_LE16(rx_dma+at+2);
            if(!offset && !bytes){end=true;break;}
            if(offset<12 || bytes<14 || bytes>1514 || offset+bytes>block ||
               (offset<ndp+size && offset+bytes>ndp))goto bad;
            deliver(rx_dma+offset,bytes);
        }
        if(!end)goto bad;
        ndp=GET_LE16(rx_dma+ndp+6);
    }
    return;
bad:invalid_ntb++;
}
static void rx_worker(void *unused)
{
    (void)unused;unsigned previous=0;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10));
        if(previous!=generation) {
            previous=generation;FreeRTOS_NetworkDown(&interface);
            printf("[ncm] USB link=%u generation=%u\r\n",active,previous);
        }
        if(rx_ready) {
            unsigned length=rx_length;rx_length=0;rx_ready=false;
            if(active && length)parse_ntb(length);
            armed=false;
        }
        if(active && !armed) {
            armed=true;
            if(usbd_cdc_ncm_start_read(rx_dma,sizeof(rx_dma))!=0)armed=false;
        }
    }
}
static void tx_worker(void *unused)
{
    (void)unused;unsigned index;
    for(;;) {
        if(xQueueReceive(tx_ready,&index,portMAX_DELAY)!=pdTRUE)continue;
        unsigned epoch=tx_generation[index];
        ulTaskNotifyTake(pdTRUE,0);
        if(active && epoch==generation && usbd_cdc_ncm_start_write(tx_dma[index],tx_length[index])==0) {
            /* A timeout must not return a buffer still owned by USB DMA. */
            while(!ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100)))
                if(epoch!=generation || !active)break;
            if(epoch==generation && active)tx_packets++;else tx_drop++;
        } else tx_drop++;
        configASSERT(xQueueSend(tx_free,&index,0)==pdTRUE);
    }
}
void ncm_tcp_init(void)
{
    const uint8_t ip[4]={192,168,NCM_SUBNET,1},mask[4]={255,255,255,0},zero[4]={0};
    interface.pcName="usb0";interface.pfInitialise=initialise;
    interface.pfOutput=output;interface.pfGetPhyLinkStatus=link_state;
    FreeRTOS_AddNetworkInterface(&interface);
    FreeRTOS_FillEndPoint(&interface,&endpoint,ip,mask,zero,zero,device_mac);
    tx_free=xQueueCreate(TX_COUNT,sizeof(unsigned));tx_ready=xQueueCreate(TX_COUNT,sizeof(unsigned));
    configASSERT(tx_free && tx_ready);
    for(unsigned i=0;i<TX_COUNT;i++)configASSERT(xQueueSend(tx_free,&i,0)==pdTRUE);
    configASSERT(FreeRTOS_IPInit_Multi()==pdPASS);
    configASSERT(xTaskCreate(rx_worker,"ncm_rx",1024,NULL,26,&rx_task_handle)==pdPASS);
    configASSERT(xTaskCreate(tx_worker,"ncm_tx",768,NULL,25,&tx_task_handle)==pdPASS);
}
void ncm_tcp_status(void)
{
    printf("[ncm] link=%u generation=%u rx=%u tx=%u rx_drop=%u tx_drop=%u invalid_ntb=%u\r\n",
        active,generation,rx_packets,tx_packets,rx_drop,tx_drop,invalid_ntb);
}
