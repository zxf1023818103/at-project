/* Real LittleFS + NOR model. All values are synthetic. Tests include the adapter
 * to reset its RAM-only error latch after a simulated reboot, never on target. */
#include "../../bluedroid_config/nvs_lfs.c"
#include "../lfs_blank.h"
#include <assert.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define FLASH_SIZE (15*4096)
static uint8_t *flash, snapshot[FLASH_SIZE];
static lfs_t fs;
static uint8_t read_cache[512],prog_cache[512],lookahead[8];
static unsigned mutations,cut_at,partial,cuts,io_fail,write_error;
static struct lfs_config cfg;
lfs_t *vfs_lfs_mount(void) { return &fs; }
static int rd(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,void *p,lfs_size_t n)
{ assert(b<c->block_count && off+n<=c->block_size); if(io_fail) return LFS_ERR_IO; memcpy(p,flash+b*4096+off,n); return 0; }
static int mutate(unsigned off,const void *p,unsigned n)
{
    if(write_error) return LFS_ERR_IO;
    assert(off+n<=FLASH_SIZE); ++mutations;
    unsigned count=cut_at==mutations ? (partial ? n/2 : 0) : n;
    for(unsigned i=0;i<count;++i) {
        if(p) { uint8_t v=((const uint8_t *)p)[i]; assert((flash[off+i]&v)==v); flash[off+i]&=v; }
        else flash[off+i]=0xff;
    }
    if(cut_at==mutations) _exit(86);
    return 0;
}
static int wr(const struct lfs_config *c,lfs_block_t b,lfs_off_t off,const void *p,lfs_size_t n)
{ (void)c; return mutate(b*4096+off,p,n); }
static int er(const struct lfs_config *c,lfs_block_t b)
{ (void)c; return mutate(b*4096,NULL,4096); }
static int sync_disk(const struct lfs_config *c) { (void)c; return 0; }
static int fs_lock(const struct lfs_config *c) { (void)c; return 0; }
static void reboot(void)
{ memset(&fs,0,sizeof fs); disk_error=0; assert(!lfs_mount(&fs,&cfg)); }
static nvs_handle_t open_ns(const char *name)
{ nvs_handle_t h=0; assert(nvs_open(name,NVS_READWRITE,&h)==ESP_OK && h); return h; }
static void put(nvs_handle_t h,const char *k,const void *p,size_t n)
{ assert(!nvs_set_blob(h,k,p,n)); }
static void get(nvs_handle_t h,const char *k,const void *p,size_t n)
{
    size_t size=0; assert(!nvs_get_blob(h,k,NULL,&size) && size==n);
    uint8_t buf[4096]; size=sizeof buf;
    assert(!nvs_get_blob(h,k,buf,&size) && size==n && !memcmp(buf,p,n));
}
static void update(void)
{
    nvs_handle_t h=open_ns("alpha");
    uint8_t data[1536]; memset(data,0x22,sizeof data);
    put(h,"first",data,sizeof data); put(h,"last","new",3); put(h,"empty",NULL,0);
    assert(!nvs_commit(h)); nvs_close(h);
}
static void atomic_update(void)
{
    memcpy(snapshot,flash,FLASH_SIZE); mutations=0; update(); unsigned total=mutations; assert(total);
    for(unsigned at=1;at<=total;++at) for(unsigned half=0;half<2;++half) {
        memcpy(flash,snapshot,FLASH_SIZE); reboot();
        pid_t child=fork(); assert(child>=0);
        if(!child) { mutations=0;cut_at=at;partial=half;update();_exit(0); }
        int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==86);
        reboot(); nvs_handle_t h=open_ns("alpha");
        uint8_t first[1536],expected[1536];size_t n=sizeof first;
        assert(!nvs_get_blob(h,"first",first,&n) && n==sizeof first);
        assert(first[0]==0x11 || first[0]==0x22); memset(expected,first[0],sizeof expected);
        assert(!memcmp(first,expected,n)); get(h,"last",first[0]==0x11?"old":"new",3);
        n=0; assert(nvs_get_blob(h,"empty",NULL,&n)==(first[0]==0x11?ESP_ERR_NVS_NOT_FOUND:ESP_OK));
        nvs_close(h);h=open_ns("beta");get(h,"keep","unchanged",9);nvs_close(h);
        uint8_t sentinel[3];assert(!lfs_blob_read_exact(&fs,"/nimble.bonds",sentinel,3));assert(!memcmp(sentinel,"BLS",3));
        ++cuts;
    }
    memcpy(flash,snapshot,FLASH_SIZE);reboot();
}
static void *concurrent(void *arg)
{
    char key[16];snprintf(key,sizeof key,"thread%u",(unsigned)(uintptr_t)arg);
    for(unsigned i=0;i<8;++i) { nvs_handle_t h=open_ns("threads");put(h,key,&i,sizeof i);assert(!nvs_commit(h));nvs_close(h); }
    return NULL;
}
static void run(unsigned cache)
{
    memset(flash,0xff,FLASH_SIZE);
    cfg=(struct lfs_config){.read=rd,.prog=wr,.erase=er,.sync=sync_disk,.lock=fs_lock,.unlock=fs_lock,
      .read_size=256,.prog_size=256,.block_size=4096,.block_count=15,.block_cycles=500,
      .cache_size=cache,.lookahead_size=8,.read_buffer=read_cache,.prog_buffer=prog_cache,.lookahead_buffer=lookahead};
    assert(lfs_region_is_blank(&cfg)==1);flash[FLASH_SIZE-1]=0;assert(lfs_region_is_blank(&cfg)==0);flash[FLASH_SIZE-1]=0xff;
    io_fail=1;assert(lfs_region_is_blank(&cfg)==LFS_ERR_IO);io_fail=0;
    assert(!lfs_format(&fs,&cfg));reboot();
    assert(!lfs_blob_replace(&fs,"/nimble.bonds","/nimble.tmp","BLS",3));
    nvs_handle_t h=open_ns("alpha");uint8_t data[1536];memset(data,0x11,sizeof data);
    put(h,"first",data,sizeof data);put(h,"last","old",3);assert(!nvs_commit(h));nvs_close(h);
    h=open_ns("beta");put(h,"keep","unchanged",9);assert(!nvs_commit(h));nvs_close(h);
    atomic_update();
    h=open_ns("alpha");put(h,"last","discard",7);nvs_close(h);reboot();h=open_ns("alpha");get(h,"last","old",3);
    size_t n=1;assert(nvs_get_blob(h,"last",data,&n)==ESP_ERR_NVS_INVALID_LENGTH && n==3);
    unsigned before=mutations;put(h,"last","old",3);assert(!nvs_commit(h) && before==mutations);
    put(h,"last","partial",7);assert(nvs_set_blob(h,"bad/key","x",1)==ESP_ERR_INVALID_ARG);assert(nvs_commit(h)!=ESP_OK);
    nvs_close(h);reboot();h=open_ns("alpha");get(h,"last","old",3);nvs_close(h);
    h=open_ns("alpha");put(h,"last","failed IO",9);write_error=1;
    assert(nvs_commit(h)!=ESP_OK && nvs_lfs_error()==LFS_ERR_IO);nvs_close(h);
    assert(nvs_open("beta",NVS_READWRITE,&h)!=ESP_OK);write_error=0;reboot();
    h=open_ns("alpha");get(h,"last","old",3);nvs_close(h);
    pthread_t a,b;assert(!pthread_create(&a,NULL,concurrent,(void *)1));assert(!pthread_create(&b,NULL,concurrent,(void *)2));
    pthread_join(a,NULL);pthread_join(b,NULL);reboot();h=open_ns("threads");unsigned seven=7;
    get(h,"thread1",&seven,sizeof seven);get(h,"thread2",&seven,sizeof seven);nvs_close(h);
    /* Old LFS-backed EasyFlash chunk files migrate, ignoring stale tail. */
    assert(!lfs_mkdir(&fs,"/KV"));
    memset(data,'A',sizeof data);assert(!lfs_blob_replace(&fs,"/KV/bd_bt_config.conf_bt_cfg_key0","/legacy.tmp",data,1536));
    assert(!lfs_blob_replace(&fs,"/KV/bd_bt_config.conf_bt_cfg_key1","/legacy.tmp","tail",4));
    assert(!lfs_blob_replace(&fs,"/KV/bd_bt_config.conf_bt_cfg_key2","/legacy.tmp","stale",5));
    h=open_ns("bt_config.conf");get(h,"bt_cfg_key0",data,1536);get(h,"bt_cfg_key1","tail",4);
    n=0;assert(nvs_get_blob(h,"bt_cfg_key2",NULL,&n)==ESP_ERR_NVS_NOT_FOUND);assert(!nvs_commit(h));nvs_close(h);
    reboot();h=open_ns("bt_config.conf");get(h,"bt_cfg_key1","tail",4);put(h,"bt_cfg_key0",NULL,0);assert(!nvs_commit(h));nvs_close(h);
    reboot();h=open_ns("bt_config.conf");get(h,"bt_cfg_key0","",0);nvs_close(h);
    /* Corruption is rejected and blocks overwrite until restart. */
    struct lfs_info info;assert(!lfs_stat(&fs,"/bd_alpha.nvs",&info));uint8_t *bad=malloc(info.size);
    assert(!lfs_blob_read_exact(&fs,"/bd_alpha.nvs",bad,info.size));bad[info.size-1]^=1;
    assert(!lfs_blob_replace(&fs,"/bd_alpha.nvs","/bad.tmp",bad,info.size));free(bad);
    h=123;before=mutations;assert(nvs_open("alpha",NVS_READWRITE,&h)!=ESP_OK && h==0 && nvs_lfs_error());
    assert(nvs_open("beta",NVS_READWRITE,&h)!=ESP_OK && before==mutations);
    printf("PASS: NVS cache=%u, multi-key atomicity, namespace isolation, reopen, no-op writes, failed-set rollback, concurrency, legacy import, empty blob, corruption\n",cache);
}
int main(void)
{
    flash=mmap(NULL,FLASH_SIZE,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(flash!=MAP_FAILED);
    run(256);run(512);printf("PASS: %u abrupt power cuts\n",cuts);munmap(flash,FLASH_SIZE);vSemaphoreDelete(mutex);return 0;
}
