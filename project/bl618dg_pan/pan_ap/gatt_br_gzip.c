/* Synthetic UART source -> real MCU gzip -> GATT. No UART hardware benchmark.
 * baud=0 measures capacity. A paced source models 8N1 arrivals independently
 * of compressor/link backpressure, reporting the resulting raw backlog. */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "bflb_mtimer.h"
#include "mm.h"
#include "zlib.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "lz4.h"
#include "gatt_br_gzip.h"

#define RAW_BLOCK 4096
#define RECORD_MAX (RAW_BLOCK+128)
typedef struct {unsigned size, raw; uint8_t bytes[RECORD_MAX];} record_t;
static QueueHandle_t output;
static SemaphoreHandle_t lock;
static TaskHandle_t compressor;
static record_t *pending;
static unsigned pending_offset;
static bool running, abort_run;
static unsigned seconds, profile, baud, level, codec;
static uint32_t raw_bytes, wire_bytes, peak_backlog, cpu_us, errors, start_tick, end_tick;
static uint32_t generation, heap_start, heap_low;
static uint32_t workspace_live, workspace_peak;

static void put32(uint8_t *p, uint32_t v)
{ for(unsigned i=0;i<4;i++) p[i]=v>>(8*i); }
static uint32_t arrived(void)
{
    uint32_t ms=(xTaskGetTickCount()-start_tick)*portTICK_PERIOD_MS;
    if(ms>seconds*1000) ms=seconds*1000;
    return (uint64_t)baud*ms/10000;
}
static void backlog(void)
{
    uint32_t want=baud ? arrived() : raw_bytes;
    uint32_t queued=want>raw_bytes ? want-raw_bytes : 0;
    if(queued>peak_backlog) peak_backlog=queued;
}
static void hex(char *out, uint32_t value, unsigned digits)
{
    static const char table[]="0123456789abcdef";
    while(digits) {out[--digits]=table[value&15];value>>=4;}
}
static void source(uint8_t *dst, unsigned size, unsigned offset)
{
    if(profile==1) {
        uint32_t random=0x9e3779b9u^(offset/RAW_BLOCK);
        for(unsigned i=0;i<size;i++) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            dst[i]=random;
        }
    } else {
        for(unsigned at=0;at<size;at+=64) {
            char line[64]="t=00000000 lvl=INFO rx=00000000 tx=00000000 adc=0000 state=00";
            uint32_t n=(offset+at)/64;
            hex(line+2,n,8);hex(line+23,n*37,8);hex(line+35,n*11,8);
            hex(line+48,n%4096,4);hex(line+59,n%4,2);
            line[61]=' '; line[62]='\r'; line[63]='\n';
            unsigned count=size-at<64 ? size-at : 64;
            memcpy(dst+at,line,count);
        }
    }
}
typedef union {uint64_t alignment; size_t size;} allocation_t;
static void *codec_alloc(void *opaque, size_t size)
{
    (void)opaque;
    /* pvPortMalloc selects OCRAM only in this SDK, while its reported free
     * heap includes WRAM. Keep compression workspace off the BT task heap. */
    if(size>SIZE_MAX-sizeof(allocation_t)) return NULL;
    allocation_t *p=kmalloc(size+sizeof(*p),MM_FLAG_HEAP_WRAM_0|MM_FLAG_ALIGN_8BYTE);
    if(!p) return NULL;
    p->size=size; workspace_live+=size;
    if(workspace_live>workspace_peak) workspace_peak=workspace_live;
    unsigned heap=xPortGetFreeHeapSize();if(heap<heap_low) heap_low=heap;
    return p+1;
}
static void codec_free(void *opaque, void *address)
{
    (void)opaque;
    if(address) {allocation_t *p=(allocation_t *)address-1;workspace_live-=p->size;kfree(p);}
}
static voidpf allocate(voidpf opaque, uInt items, uInt size)
{
    if(size && items>SIZE_MAX/size) return NULL;
    void *p=codec_alloc(opaque,(size_t)items*size);
    if(p) memset(p,0,(size_t)items*size);
    return p;
}
static void release(voidpf opaque, voidpf address)
{codec_free(opaque,address);}

static void work(void *unused)
{
    (void)unused;
    uint8_t *input=kmalloc(RAW_BLOCK,MM_FLAG_HEAP_WRAM_0); configASSERT(input);
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        unsigned my_generation=generation;
        z_stream stream={0}; stream.zalloc=allocate; stream.zfree=release;
        ZSTD_CCtx *zstd=NULL;void *lz4=NULL;
        int result=Z_OK;bool ready=false;
        if(codec==1) {
            result=deflateInit2(&stream,level,Z_DEFLATED,15+16,5,Z_DEFAULT_STRATEGY);
            ready=result==Z_OK;
        } else if(codec==2) {
            ZSTD_customMem memory={codec_alloc,codec_free,NULL};
            zstd=ZSTD_createCCtx_advanced(memory);
            ready=zstd && !ZSTD_isError(ZSTD_CCtx_setParameter(zstd,ZSTD_c_compressionLevel,level))
                && !ZSTD_isError(ZSTD_CCtx_setParameter(zstd,ZSTD_c_windowLog,12))
                && !ZSTD_isError(ZSTD_CCtx_setParameter(zstd,ZSTD_c_checksumFlag,1));
        } else {
            lz4=codec_alloc(NULL,LZ4_sizeofState());ready=lz4!=NULL;
        }
        if(!ready) errors++;
        unsigned offset=0, sequence=0;
        while(ready && !abort_run && generation==my_generation) {
            uint32_t elapsed=(xTaskGetTickCount()-start_tick)*portTICK_PERIOD_MS;
            xSemaphoreTake(lock,portMAX_DELAY); backlog(); xSemaphoreGive(lock);
            if(!baud && elapsed>=seconds*1000) break;
            if(baud && offset>=(uint64_t)baud*seconds/10) break;
            if(elapsed>seconds*1000+5000) {errors++; break;}
            unsigned count=RAW_BLOCK;
            if(baud) {
                uint32_t target=(uint64_t)baud*seconds/10;
                if(count>target-offset) count=target-offset;
                if(arrived()<offset+count) {vTaskDelay(1); continue;}
            }
            record_t *record=kmalloc(sizeof(*record),MM_FLAG_HEAP_WRAM_0);
            if(!record) {errors++; break;}
            source(input,count,offset);
            uint64_t before=bflb_mtimer_get_time_us();
            size_t packed=0;bool ok=false;
            if(codec==1) {
                deflateReset(&stream);
                stream.next_in=input; stream.avail_in=count;
                stream.next_out=record->bytes+24; stream.avail_out=RECORD_MAX-24;
                result=deflate(&stream,Z_FINISH);packed=stream.total_out;
                ok=result==Z_STREAM_END;
            } else if(codec==2) {
                packed=ZSTD_compress2(zstd,record->bytes+24,RECORD_MAX-24,input,count);
                ok=!ZSTD_isError(packed);
                if(!ok) printf("[codec] zstd error: %s\r\n",ZSTD_getErrorName(packed));
            } else {
                int n=LZ4_compress_fast_extState(lz4,(const char *)input,(char *)record->bytes+24,
                    count,RECORD_MAX-24,level);
                ok=n>0;packed=n>0 ? n : 0;
            }
            cpu_us+=(uint32_t)(bflb_mtimer_get_time_us()-before);
            if(!ok) {kfree(record); errors++; break;}
            bool compressed=packed<count;
            if(!compressed) {memcpy(record->bytes+24,input,count); packed=count;}
            memcpy(record->bytes,"GZB1",4);
            put32(record->bytes+4,sequence); put32(record->bytes+8,offset);
            put32(record->bytes+12,count); put32(record->bytes+16,packed);
            put32(record->bytes+20,compressed ? codec : 0);
            record->size=packed+24; record->raw=count;
            bool queued=false;
            while(!abort_run && generation==my_generation) {
                if(xQueueSend(output,&record,pdMS_TO_TICKS(20))==pdPASS) {queued=true;break;}
                xSemaphoreTake(lock,portMAX_DELAY); backlog(); xSemaphoreGive(lock);
                if((xTaskGetTickCount()-start_tick)*portTICK_PERIOD_MS>seconds*1000+5000) break;
            }
            if(!queued) {kfree(record); if(!abort_run) errors++; break;}
            offset+=count; sequence++;
            xSemaphoreTake(lock,portMAX_DELAY);
            raw_bytes=offset; backlog();
            unsigned heap=xPortGetFreeHeapSize(); if(heap<heap_low) heap_low=heap;
            xSemaphoreGive(lock);
            vTaskDelay(1);
        }
        if(codec==1 && ready) deflateEnd(&stream);
        if(zstd) ZSTD_freeCCtx(zstd);
        if(lz4) codec_free(NULL,lz4);
        /* A blocked enqueue can complete after STOP flushed the queue. */
        if(abort_run) {
            record_t *discard;
            while(xQueueReceive(output,&discard,0)==pdPASS) kfree(discard);
        }
        /* Ordered end marker lets the host drain notifications before asking
         * for status. Control responses must not compete with a saturated TX. */
        if(!abort_run) {
            record_t *end=kmalloc(sizeof(*end),MM_FLAG_HEAP_WRAM_0);
            if(end) {
                memset(end,0,sizeof(*end)); end->size=24;
                memcpy(end->bytes,"GZE1",4);put32(end->bytes+4,sequence);
                put32(end->bytes+8,offset);
                if(xQueueSend(output,&end,pdMS_TO_TICKS(1000))!=pdPASS) {kfree(end);errors++;}
            } else errors++;
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        end_tick=xTaskGetTickCount(); backlog(); running=false;
        xSemaphoreGive(lock);
        printf("[codec] PRODUCER_DONE raw=%lu wire=%lu backlog=%lu call_us=%lu errors=%lu\r\n",
            (unsigned long)raw_bytes,(unsigned long)wire_bytes,(unsigned long)peak_backlog,
            (unsigned long)cpu_us,(unsigned long)errors);
    }
}

bool gatt_gzip_start(unsigned sec, unsigned kind, unsigned speed, unsigned quality, unsigned algorithm)
{
    if(running || pending || uxQueueMessagesWaiting(output) || sec<1 || sec>60 || kind>1 ||
       speed>10000000 || quality<1 || quality>6 || algorithm<1 || algorithm>3) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    seconds=sec; profile=kind; baud=speed; level=quality;codec=algorithm;
    raw_bytes=wire_bytes=peak_backlog=cpu_us=errors=0;
    workspace_live=workspace_peak=0;
    heap_start=heap_low=xPortGetFreeHeapSize();
    start_tick=xTaskGetTickCount(); end_tick=start_tick;
    generation++; abort_run=false; running=true;
    xSemaphoreGive(lock);
    xTaskNotifyGive(compressor);
    printf("[codec] START codec=%u seconds=%u profile=%u baud=%u level=%u heap=%lu\r\n",
        codec,sec,kind,speed,quality,(unsigned long)heap_start);
    return true;
}
void gatt_gzip_stop(void)
{
    abort_run=true;
    if(pending) {kfree(pending); pending=NULL;}
    record_t *record;
    while(xQueueReceive(output,&record,0)==pdPASS) kfree(record);
}
unsigned gatt_gzip_peek(const uint8_t **data)
{
    if(!pending) {
        if(xQueueReceive(output,&pending,0)!=pdPASS) return 0;
        pending_offset=0;
    }
    *data=pending->bytes+pending_offset;
    unsigned left=pending->size-pending_offset;
    return left>512 ? 512 : left;
}
void gatt_gzip_accept(unsigned length)
{
    pending_offset+=length; wire_bytes+=length;
    if(pending_offset==pending->size) {kfree(pending); pending=NULL;}
}
unsigned gatt_gzip_status(uint8_t *data)
{
    xSemaphoreTake(lock,portMAX_DELAY);
    memcpy(data,"GZS1",4);
    put32(data+4,running); put32(data+8,raw_bytes); put32(data+12,wire_bytes);
    put32(data+16,peak_backlog); put32(data+20,cpu_us); put32(data+24,errors);
    put32(data+28,((running ? xTaskGetTickCount() : end_tick)-start_tick)*portTICK_PERIOD_MS);
    put32(data+32,baud); put32(data+36,profile); put32(data+40,level);
    put32(data+44,heap_start); put32(data+48,heap_low);
    put32(data+52,!!pending+uxQueueMessagesWaiting(output));
    put32(data+56,codec);put32(data+60,workspace_peak);
    xSemaphoreGive(lock);
    return 64;
}
void gatt_gzip_init(void)
{
    output=xQueueCreate(4,sizeof(record_t *)); lock=xSemaphoreCreateMutex();
    configASSERT(output && lock);
    configASSERT(xTaskCreate(work,"codec_load",4096,NULL,4,&compressor)==pdPASS);
}
