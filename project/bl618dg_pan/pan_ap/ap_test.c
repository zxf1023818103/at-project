/* Opt-in SoftAP validation: local HTTP data integrity and recovery fixture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "wifi_mgmr_ext.h"
#include "bouffalo_wifi_tcp.h"
#include "shell.h"
#include "mm.h"

static unsigned ap_starts,ap_stops,joins,leaves,auth_errors,requests,failures;
static uint32_t sent_bytes,received_bytes,bad_bytes;
static unsigned channel=36;
extern void wifi_test_dhcp_counts(unsigned *,unsigned *,unsigned *);

void ap_test_wifi_event(unsigned code)
{
    if(code==CODE_WIFI_ON_AP_STARTED)ap_starts++;
    if(code==CODE_WIFI_ON_AP_STOPPED)ap_stops++;
    if(code==CODE_WIFI_ON_AP_STA_ADD)joins++;
    if(code==CODE_WIFI_ON_AP_STA_DEL)leaves++;
    if(code==CODE_WIFI_ON_AP_STA_AUTH_FAIL)auth_errors++;
}
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
    unsigned offers,acks,invalid;wifi_test_dhcp_counts(&offers,&acks,&invalid);
    return snprintf(out,capacity,"{\"uptime_ms\":%lu,\"channel\":%u,\"ap_starts\":%u,\"ap_stops\":%u,\"joins\":%u,\"leaves\":%u,\"auth_errors\":%u,\"requests\":%u,\"failures\":%u,\"sent\":%lu,\"received\":%lu,\"bad\":%lu,\"heap\":%u,\"dhcp_offers\":%u,\"dhcp_acks\":%u,\"dhcp_invalid\":%u}\n",
        (unsigned long)(xTaskGetTickCount()*portTICK_PERIOD_MS),channel,ap_starts,ap_stops,joins,leaves,auth_errors,requests,failures,
        (unsigned long)sent_bytes,(unsigned long)received_bytes,(unsigned long)bad_bytes,
        (unsigned)kfree_size(0),offers,acks,invalid);
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
        printf("[ap-http] GET bytes=%u ms=%lu ok=%u heap=%u\r\n",sent,
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
        printf("[ap-http] POST bytes=%u bad=%u ms=%lu ok=%u heap=%u\r\n",received,bad,
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
    printf("[ap-http] listening 192.168.77.1:8080\r\n");
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
static int start_ap(int argc,char **argv)
{
    unsigned selected=argc==2 ? strtoul(argv[1],NULL,10):36;
    if(selected!=6 && selected!=36 && selected!=40 && selected!=44 && selected!=48)return -1;
    wifi_mgmr_ap_params_t cfg={0};
    cfg.ssid="BL618DG-AP-Test";cfg.key="bl618dg-test";cfg.akm="WPA2";
    cfg.channel=selected;cfg.type=0;cfg.bcn_interval=100;cfg.bcn_mode=2;
    cfg.ap_max_inactivity=300;cfg.use_ipcfg=true;cfg.use_dhcpd=true;
    cfg.ap_ipaddr=FreeRTOS_inet_addr("192.168.77.1");cfg.ap_mask=FreeRTOS_inet_addr("255.255.255.0");
    cfg.start=100;cfg.limit=4;
    int result=wifi_mgmr_ap_start(&cfg);
    if(!result)channel=selected;
    printf("[ap] start channel=%u result=%d\r\n",selected,result);
    return result;
}
static int stop_ap(int argc,char **argv)
{(void)argc;(void)argv;return wifi_mgmr_ap_stop();}
static int ap_status(int argc,char **argv)
{(void)argc;(void)argv;char out[600];status_json(out,sizeof(out));printf("[ap] %s",out);bouffalo_wifi_tcp_status();return 0;}
SHELL_CMD_EXPORT_ALIAS(start_ap,ap_start,Start test AP: optional channel 6 or 36/40/44/48.);
SHELL_CMD_EXPORT_ALIAS(stop_ap,ap_stop,Stop test AP.);
SHELL_CMD_EXPORT_ALIAS(ap_status,ap_status,Show SoftAP test counters.);
void ap_test_init(void)
{
    configASSERT(xTaskCreate(http_task,"ap_http",2048,NULL,6,NULL)==pdPASS);
    start_ap(0,NULL);
}
