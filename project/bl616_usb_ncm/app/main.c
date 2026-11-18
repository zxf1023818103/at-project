#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "board.h"
#include "bflb_uart.h"
#include "bflb_sec_trng.h"
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "ncm_tcp.h"
#include "ncm_identity.h"
#include "shell.h"
#include "mm.h"
static unsigned requests,failures;
static uint32_t sent_bytes,received_bytes,bad_bytes;
extern void usb_device_start(void);
extern int usb_dhcp_start(int,int);
extern void shell_init_with_task(struct bflb_device_s *);
const char *pcApplicationHostnameHook(void){return NCM_HOSTNAME;}
BaseType_t xApplicationGetRandomNumber(uint32_t *value)
{return bflb_trng_readlen((uint8_t *)value,4)==0 ? pdTRUE:pdFALSE;}
uint32_t ulApplicationGetNextSequenceNumber(uint32_t a,uint16_t b,uint32_t c,uint16_t d)
{(void)a;(void)b;(void)c;(void)d;uint32_t value=0;configASSERT(xApplicationGetRandomNumber(&value));return value;}
void vApplicationIPNetworkEventHook_Multi(eIPCallbackEvent_t event,NetworkEndPoint_t *endpoint)
{(void)endpoint;printf("[ncm] IP %s " NCM_DEVICE_IP "/24\r\n",event==eNetworkUp ? "UP":"DOWN");}
void vApplicationPingReplyHook(ePingReplyStatus_t status,uint16_t id)
{printf("[ncm] ping=%u id=%u\r\n",status,id);}
static bool send_all(Socket_t socket,const void *data,unsigned length)
{
    unsigned done=0;
    while(done<length) {
        BaseType_t n=FreeRTOS_send(socket,(const uint8_t *)data+done,length-done,0);
        if(n<=0)return false;
        done+=n;
    }
    return true;
}
static bool reply_header(Socket_t socket,unsigned length,const char *type,unsigned status)
{
    char header[240];
    int n=snprintf(header,sizeof(header),"HTTP/1.1 %u %s\r\nContent-Length: %u\r\nContent-Type: %s\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        status,status==200 ? "OK":"Bad Request",length,type);
    return n>0 && n<(int)sizeof(header) && send_all(socket,header,n);
}
static unsigned status_json(char *out,unsigned capacity)
{
    return snprintf(out,capacity,"{\"transport\":\"usb-ncm\",\"uptime_ms\":%lu,\"requests\":%u,\"failures\":%u,\"sent\":%lu,\"received\":%lu,\"bad\":%lu,\"heap\":%u}\n",
      (unsigned long)(xTaskGetTickCount()*portTICK_PERIOD_MS),requests,failures,
      (unsigned long)sent_bytes,(unsigned long)received_bytes,(unsigned long)bad_bytes,(unsigned)kfree_size(0));
}
static const char *content_length(const char *header)
{
    const char *key="content-length:";
    for(const char *p=header;*p;p++) {
        if(p!=header && p[-1]!='\n')continue;
        unsigned i=0;
        while(key[i] && p[i] && ((p[i]>='A' && p[i]<='Z' ? p[i]+32 : p[i])==key[i]))i++;
        if(!key[i])return p+i;
    }
    return NULL;
}
static void serve(Socket_t socket,uint8_t *data)
{
    char header[1200],method[8],path[160];unsigned used=0;
    TickType_t start=xTaskGetTickCount();
    while(used<sizeof(header)-1) {
        BaseType_t n=FreeRTOS_recv(socket,header+used,sizeof(header)-1-used,0);
        if(n<=0){failures++;return;}
        used+=n;header[used]=0;
        if(strstr(header,"\r\n\r\n"))break;
    }
    char *body=strstr(header,"\r\n\r\n");
    if(!body || sscanf(header,"%7s %159s",method,path)!=2){failures++;return;}
    body+=4;unsigned buffered=used-(body-header);
    requests++;
    if(!strcmp(method,"GET") && !strcmp(path,"/status")) {
        unsigned n=status_json((char *)data,1460);
        if(!reply_header(socket,n,"application/json",200) || !send_all(socket,data,n))failures++;
    } else if(!strcmp(method,"GET") && !strncmp(path,"/file",5)) {
        unsigned total=2*1024*1024;
        const char *size=strstr(path,"bytes=");
        if(size)total=strtoul(size+6,NULL,10);
        if(total<1 || total>32*1024*1024){failures++;reply_header(socket,0,"text/plain",400);return;}
        unsigned sent=0;bool ok=reply_header(socket,total,"application/octet-stream",200);
        while(ok && sent<total) {
            unsigned block=total-sent;if(block>1460)block=1460;
            for(unsigned i=0;i<block;i++)data[i]=(sent+i)%251;
            ok=send_all(socket,data,block);
            if(ok)sent+=block;
        }
        sent_bytes+=sent;if(!ok)failures++;
        printf("[usb-http] GET bytes=%u ms=%lu ok=%u heap=%u\r\n",sent,
            (unsigned long)((xTaskGetTickCount()-start)*portTICK_PERIOD_MS),ok,(unsigned)kfree_size(0));
    } else if(!strcmp(method,"POST") && !strcmp(path,"/upload")) {
        const char *length=content_length(header);
        unsigned total=length ? strtoul(length,NULL,10):0;
        if(!total || total>32*1024*1024){failures++;reply_header(socket,0,"text/plain",400);return;}
        unsigned received=0,bad=0;
        if(buffered>total)buffered=total;
        for(unsigned i=0;i<buffered;i++)if((uint8_t)body[i]!=i%251)bad++;
        received=buffered;
        while(received<total) {
            unsigned block=total-received;if(block>1460)block=1460;
            BaseType_t n=FreeRTOS_recv(socket,data,block,0);
            if(n<=0)break;
            for(unsigned i=0;i<(unsigned)n;i++)if(data[i]!=(received+i)%251)bad++;
            received+=n;
        }
        received_bytes+=received;bad_bytes+=bad;
        bool ok=received==total && !bad;
        unsigned n=snprintf((char *)data,1460,"{\"received\":%u,\"bad\":%u}\n",received,bad);
        if(!reply_header(socket,n,"application/json",ok ? 200:400) || !send_all(socket,data,n))ok=false;
        if(!ok)failures++;
        printf("[usb-http] POST bytes=%u bad=%u ms=%lu ok=%u heap=%u\r\n",received,bad,
            (unsigned long)((xTaskGetTickCount()-start)*portTICK_PERIOD_MS),ok,(unsigned)kfree_size(0));
    } else {reply_header(socket,0,"text/plain",400);failures++;}
}
static void http_task(void *unused)
{
    (void)unused;
    uint8_t *data=kmalloc(1460,MM_FLAG_HEAP_WRAM_0);configASSERT(data);
    Socket_t listener=FreeRTOS_socket(FREERTOS_AF_INET,FREERTOS_SOCK_STREAM,FREERTOS_IPPROTO_TCP);
    configASSERT(listener!=FREERTOS_INVALID_SOCKET);
    struct freertos_sockaddr local={.sin_family=FREERTOS_AF_INET,.sin_port=FreeRTOS_htons(8080)};
    configASSERT(FreeRTOS_bind(listener,&local,sizeof(local))==0);
    configASSERT(FreeRTOS_listen(listener,4)==0);
    printf("[usb-http] listening " NCM_DEVICE_IP ":8080\r\n");
    for(;;) {
        Socket_t socket=FreeRTOS_accept(listener,NULL,NULL);
        if(!socket || socket==FREERTOS_INVALID_SOCKET){vTaskDelay(1);continue;}
        TickType_t timeout=pdMS_TO_TICKS(3000);
        FreeRTOS_setsockopt(socket,0,FREERTOS_SO_RCVTIMEO,&timeout,sizeof(timeout));
        FreeRTOS_setsockopt(socket,0,FREERTOS_SO_SNDTIMEO,&timeout,sizeof(timeout));
        serve(socket,data);
        FreeRTOS_shutdown(socket,FREERTOS_SHUT_RDWR);
        while(FreeRTOS_recv(socket,data,1460,0)>0){}
        FreeRTOS_closesocket(socket);
    }
}
static int status_command(int argc,char **argv)
{(void)argc;(void)argv;ncm_tcp_status();char out[400];status_json(out,sizeof(out));printf("%s",out);return 0;}
SHELL_CMD_EXPORT_ALIAS(status_command,ncm_status,USB NCM and HTTP counters.);
static void start(void *unused)
{
    (void)unused;
    shell_init_with_task(bflb_device_get_by_name("uart0"));
    ncm_tcp_init();
    configASSERT(usb_dhcp_start(100,4)==0);
    configASSERT(xTaskCreate(http_task,"usb_http",2048,NULL,6,NULL)==pdPASS);
    usb_device_start();
    vTaskDelete(NULL);
}
int main(void)
{
    board_init();
    printf("[ncm] " NCM_CHIP_NAME " CherryUSB + FreeRTOS TCP, built %s %s\r\n",__DATE__,__TIME__);
    configASSERT(xTaskCreate(start,"usb_start",1024,NULL,20,NULL)==pdPASS);
    vTaskStartScheduler();
    for(;;){}
}
