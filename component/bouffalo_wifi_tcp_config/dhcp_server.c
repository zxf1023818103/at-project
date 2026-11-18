/* Isolated SoftAP test DHCPv4 server. Four MAC-bound leases, fixed /24 subnet.
 * Deliberately not registered by STA, PAN or any production platform build. */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

static TaskHandle_t worker;
static volatile bool enabled;
static unsigned pool_start=100,pool_count=4;
static unsigned offers,acks,invalid;
static struct {uint8_t mac[6];TickType_t expires;bool used;} leases[4];

static void option(uint8_t *out,unsigned *size,unsigned tag,const void *value,unsigned length)
{out[(*size)++]=tag;out[(*size)++]=length;memcpy(out+*size,value,length);*size+=length;}

static void dhcp_task(void *unused)
{
    (void)unused;
    Socket_t socket=FreeRTOS_socket(FREERTOS_AF_INET,FREERTOS_SOCK_DGRAM,FREERTOS_IPPROTO_UDP);
    configASSERT(socket!=FREERTOS_INVALID_SOCKET);
    TickType_t timeout=pdMS_TO_TICKS(1000);
    FreeRTOS_setsockopt(socket,0,FREERTOS_SO_RCVTIMEO,&timeout,sizeof(timeout));
    struct freertos_sockaddr local={.sin_family=FREERTOS_AF_INET,.sin_port=FreeRTOS_htons(67)};
    configASSERT(FreeRTOS_bind(socket,&local,sizeof(local))==0);
    uint8_t request[600],reply[320];
    const uint8_t server[4]={192,168,77,1},mask[4]={255,255,255,0};
    const uint8_t lease_time[4]={0,0,0,120};
    for(;;) {
        struct freertos_sockaddr peer; socklen_t peer_size=sizeof(peer);
        BaseType_t n=FreeRTOS_recvfrom(socket,request,sizeof(request),0,&peer,&peer_size);
        if(n<=0 || !enabled) {vTaskDelay(1);continue;}
        if(n<240 || request[0]!=1 || request[1]!=1 || request[2]!=6 ||
           memcmp(request+236,"\x63\x82\x53\x63",4) || peer.sin_port!=FreeRTOS_htons(68)) {invalid++;continue;}
        unsigned type=0;uint8_t wanted[4]={0},selected[4]={0};bool malformed=false;
        for(unsigned at=240;at<(unsigned)n;) {
            unsigned tag=request[at++];if(tag==255) break;if(!tag)continue;
            if(at>=(unsigned)n){malformed=true;break;}
            unsigned length=request[at++];if(at+length>(unsigned)n){malformed=true;break;}
            if(tag==53 && length==1)type=request[at];
            if(tag==50 && length==4)memcpy(wanted,request+at,4);
            if(tag==54 && length==4)memcpy(selected,request+at,4);
            at+=length;
        }
        if(malformed){invalid++;continue;}
        if(selected[0] && memcmp(selected,server,4))continue;
        if(type!=1 && type!=3 && type!=7)continue;
        int slot=-1,free_slot=-1;
        TickType_t now=xTaskGetTickCount();
        for(unsigned i=0;i<pool_count;i++) {
            if(leases[i].used && !memcmp(leases[i].mac,request+28,6))slot=i;
            if(!leases[i].used || (int32_t)(now-leases[i].expires)>=0)free_slot=i;
        }
        if(type==7){if(slot>=0)leases[slot].used=false;continue;}
        if(slot<0)slot=free_slot;
        if(slot<0)continue;
        uint8_t address[4]={192,168,77,pool_start+(unsigned)slot};
        if(type==3 && ((wanted[0] && memcmp(wanted,address,4)) ||
           (!wanted[0] && request[12] && memcmp(request+12,address,4)))) {invalid++;continue;}
        leases[slot].used=true;memcpy(leases[slot].mac,request+28,6);
        leases[slot].expires=now+pdMS_TO_TICKS(120000);
        memset(reply,0,sizeof(reply));reply[0]=2;reply[1]=1;reply[2]=6;
        memcpy(reply+4,request+4,4);reply[10]=0x80;
        memcpy(reply+16,address,4);memcpy(reply+20,server,4);
        memcpy(reply+28,request+28,16);memcpy(reply+236,request+236,4);
        unsigned size=240;uint8_t response=type==1 ? 2 : 5;
        option(reply,&size,53,&response,1);option(reply,&size,54,server,4);
        option(reply,&size,51,lease_time,4);option(reply,&size,1,mask,4);
        option(reply,&size,3,server,4);reply[size++]=255;
        peer.sin_family=FREERTOS_AF_INET;peer.sin_port=FreeRTOS_htons(68);
        peer.sin_address.ulIP_IPv4=FreeRTOS_inet_addr("255.255.255.255");
        BaseType_t sent=FreeRTOS_sendto(socket,reply,size<300 ? 300 : size,0,&peer,sizeof(peer));
        if(sent>0){if(type==1)offers++;else acks++;}
        printf("[ap-dhcp] %s %u.%u.%u.%u sent=%ld offers=%u acks=%u invalid=%u\r\n",
            type==1 ? "OFFER":"ACK",address[0],address[1],address[2],address[3],(long)sent,offers,acks,invalid);
    }
}
int wifi_test_dhcp_start(int start,int limit)
{
    if(start<2 || limit<1 || limit>4 || start+limit>255)return -1;
    pool_start=start;pool_count=limit;enabled=true;
    if(!worker && xTaskCreate(dhcp_task,"ap_dhcp",1024,NULL,5,&worker)!=pdPASS){enabled=false;return -1;}
    return 0;
}
void wifi_test_dhcp_stop(void){enabled=false;}
void wifi_test_dhcp_counts(unsigned *offer,unsigned *ack,unsigned *bad)
{*offer=offers;*ack=acks;*bad=invalid;}
