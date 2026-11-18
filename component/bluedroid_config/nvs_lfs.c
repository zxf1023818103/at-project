/* SPDX-License-Identifier: Apache-2.0
 * Portable Bluedroid NVS subset over the platform's existing LittleFS mount.
 * Each handle owns a namespace snapshot. open..close holds a transaction mutex;
 * set changes only the snapshot, commit atomically replaces the whole namespace.
 * This includes all config chunks, local IR/ER, peer keys and profile settings.
 */
#include "nvs_lfs.h"
#include "lfs_blob.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { HEADER=16, KEY=32, RECORD=36, MAX_KEYS=32, MAX_VALUE=4096, MAX_FILE=16384 };
struct space {
    char path[32], temp[32];
    lfs_t *fs;
    uint8_t *data;
    size_t size;
    unsigned count, dirty;
    int failed;
};
static SemaphoreHandle_t mutex;
static int disk_error;
static unsigned commits;
void nvs_lfs_latch_error(int error) { if (error) disk_error=error; }
__attribute__((weak)) unsigned ble_bond_config_commits(void) { return 0; }
int nvs_lfs_error(void) { return disk_error; }
unsigned nvs_lfs_commits(void) { return commits+ble_bond_config_commits(); }
static uint32_t get32(const uint8_t *p)
{ return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t checksum(const uint8_t *p, size_t n)
{ return lfs_crc(lfs_crc(0xffffffffu,p,12),p+HEADER,n-HEADER); }
static int valid_name(const char *s, size_t limit)
{
    if (!s || !*s) return 0;
    size_t n=0;
    for (;n<limit && s[n];++n) {
        unsigned char c=s[n];
        if (!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
              (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.')) return 0;
    }
    return n<limit;
}
static void wipe(void *data, size_t n)
{ volatile uint8_t *p=data; while (n--) *p++=0; }
static esp_err_t failure(int rc)
{ disk_error=rc; return rc==LFS_ERR_NOMEM ? ESP_ERR_NO_MEM : ESP_FAIL; }
static int lock(void)
{
    /* Allocate outside critical section, publish once even with simultaneous
     * first opens. A failed allocation never silently runs without the lock. */
    taskENTER_CRITICAL(); SemaphoreHandle_t m=mutex; taskEXIT_CRITICAL();
    if (!m) {
        SemaphoreHandle_t candidate=xSemaphoreCreateMutex();
        if (!candidate) return 0;
        taskENTER_CRITICAL();
        if (!mutex) { mutex=candidate; candidate=NULL; }
        m=mutex;
        taskEXIT_CRITICAL();
        if (candidate) vSemaphoreDelete(candidate);
    }
    return xSemaphoreTake(m,portMAX_DELAY)==pdTRUE;
}
static int validate(struct space *s)
{
    uint8_t *p=s->data;
    if (s->size<HEADER || memcmp(p,"BNS1",4) || get32(p+4)>MAX_KEYS ||
        get32(p+8)!=s->size || get32(p+12)!=checksum(p,s->size)) return LFS_ERR_CORRUPT;
    s->count=get32(p+4);
    size_t off=HEADER;
    for (unsigned i=0;i<s->count;++i) {
        if (s->size-off<RECORD || !valid_name((char *)p+off,KEY)) return LFS_ERR_CORRUPT;
        size_t n=get32(p+off+KEY);
        if (n>MAX_VALUE || n>s->size-off-RECORD) return LFS_ERR_CORRUPT;
        for (size_t other=HEADER;other<off;other+=RECORD+get32(p+other+KEY))
            if (!strcmp((char *)p+other,(char *)p+off)) return LFS_ERR_CORRUPT;
        off+=RECORD+n;
    }
    return off==s->size ? 0 : LFS_ERR_CORRUPT;
}
static size_t find(struct space *s, const char *key)
{
    for (size_t off=HEADER;off<s->size;off+=RECORD+get32(s->data+off+KEY))
        if (!strcmp((char *)s->data+off,key)) return off;
    return s->size;
}
static esp_err_t set(struct space *s, const char *key, const void *data, size_t n)
{
    size_t off=find(s,key), old=off<s->size ? RECORD+get32(s->data+off+KEY) : 0;
    if (old==RECORD+n && (!n || !memcmp(s->data+off+RECORD,data,n))) return ESP_OK;
    size_t size=s->size-old+RECORD+n;
    if (size>MAX_FILE || (!old && s->count==MAX_KEYS)) return ESP_ERR_NO_MEM;
    /* Allocate before mutation. A failed set poisons this transaction so an
     * accidental commit cannot publish only a subset of requested keys. */
    uint8_t *p=malloc(size);
    if (!p) return ESP_ERR_NO_MEM;
    memcpy(p,s->data,off);
    memset(p+off,0,RECORD); strcpy((char *)p+off,key); put32(p+off+KEY,n);
    if (n) memcpy(p+off+RECORD,data,n);
    memcpy(p+off+RECORD+n,s->data+off+old,s->size-off-old);
    wipe(s->data,s->size); free(s->data);
    s->data=p; s->size=size; s->count+=!old; s->dirty=1;
    return ESP_OK;
}
/* Compatibility with old EasyFlash API backed by LFS: values were raw files
 * under /KV or /_ef4_kvs_. Never call EasyFlash or create another mount. Keep
 * originals for recovery; the new namespace takes precedence once committed. */
static int import_legacy(struct space *s, const char *name)
{
    if (strcmp(name,"bt_config.conf")) return 0;
    const char *dirs[]={"/KV","/_ef4_kvs_"};
    for (unsigned d=0;d<2;++d) for (unsigned i=0;i<MAX_KEYS;++i) {
        char key[KEY], path[80]; struct lfs_info info;
        snprintf(key,sizeof key,"bt_cfg_key%u",i);
        snprintf(path,sizeof path,"%s/bd_%s_%s",dirs[d],name,key);
        int rc=lfs_stat(s->fs,path,&info);
        if (rc==LFS_ERR_NOENT) { if (i) return 0; break; }
        if (rc) return rc;
        if (info.type!=LFS_TYPE_REG || info.size>MAX_VALUE) return LFS_ERR_CORRUPT;
        uint8_t *data=malloc(info.size ? info.size : 1);
        if (!data) return LFS_ERR_NOMEM;
        rc=lfs_blob_read_exact(s->fs,path,data,info.size);
        if (!rc && set(s,key,data,info.size)!=ESP_OK) rc=LFS_ERR_NOMEM;
        wipe(data,info.size); free(data);
        if (rc) return rc;
        /* Bluedroid's last chunk is shorter than CONFIG_FILE_MAX_SIZE (1536).
         * Stale trailing chunks from an older, larger config are not imported. */
        if (info.size<1536) return 0;
        if (i==MAX_KEYS-1) return LFS_ERR_FBIG;
    }
    return 0;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=0;
    if (mode!=NVS_READWRITE || !valid_name(name,NVS_NS_NAME_MAX_SIZE)) return ESP_ERR_INVALID_ARG;
    if (!lock()) return ESP_ERR_NO_MEM;
    if (disk_error) { xSemaphoreGive(mutex); return ESP_FAIL; }
    struct space *s=calloc(1,sizeof *s);
    if (!s) { xSemaphoreGive(mutex); return ESP_ERR_NO_MEM; }
    s->fs=vfs_lfs_mount();
    snprintf(s->path,sizeof s->path,"/bd_%s.nvs",name);
    snprintf(s->temp,sizeof s->temp,"/bd_%s.tmp",name);
    int rc=LFS_ERR_IO;
    if (!s->fs) goto error;
    struct lfs_info info;
    rc=lfs_stat(s->fs,s->path,&info);
    if (rc && rc!=LFS_ERR_NOENT) goto error;
    int missing=rc==LFS_ERR_NOENT;
    if (!missing && (info.type!=LFS_TYPE_REG || info.size<HEADER || info.size>MAX_FILE)) {
        rc=LFS_ERR_CORRUPT; goto error;
    }
    s->size=missing ? HEADER : info.size;
    s->data=calloc(1,s->size);
    if (!s->data) { rc=LFS_ERR_NOMEM; goto error; }
    if (missing) { memcpy(s->data,"BNS1",4); rc=import_legacy(s,name); }
    else {
        rc=lfs_blob_read_exact(s->fs,s->path,s->data,s->size);
        if (!rc) rc=validate(s);
    }
    if (rc) goto error;
    *out=(uintptr_t)s;
    return ESP_OK;
error:;
    esp_err_t error=failure(rc);
    nvs_close((uintptr_t)s);
    return error;
}
void nvs_close(nvs_handle_t handle)
{
    struct space *s=(void *)handle;
    if (!s) return;
    if (s->data) { wipe(s->data,s->size); free(s->data); }
    free(s);
    xSemaphoreGive(mutex);
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *n)
{
    struct space *s=(void *)handle;
    if (!s || !n || !valid_name(key,KEY)) return ESP_ERR_INVALID_ARG;
    if (s->failed || disk_error) return ESP_FAIL;
    size_t off=find(s,key);
    if (off==s->size) return ESP_ERR_NVS_NOT_FOUND;
    size_t size=get32(s->data+off+KEY), capacity=*n;
    *n=size;
    if (!out) return ESP_OK;
    if (capacity<size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,s->data+off+RECORD,size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t n)
{
    struct space *s=(void *)handle;
    if (!s) return ESP_ERR_INVALID_ARG;
    if (s->failed || disk_error) return ESP_FAIL;
    esp_err_t rc=(!valid_name(key,KEY) || (!data&&n) || n>MAX_VALUE) ?
        ESP_ERR_INVALID_ARG : set(s,key,data,n);
    if (rc) s->failed=rc;
    return rc;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    struct space *s=(void *)handle;
    if (!s) return ESP_ERR_INVALID_ARG;
    if (s->failed || disk_error) return ESP_FAIL;
    if (!s->dirty) return ESP_OK;
    put32(s->data+4,s->count); put32(s->data+8,s->size);
    put32(s->data+12,checksum(s->data,s->size));
    int rc=lfs_blob_replace(s->fs,s->path,s->temp,s->data,s->size);
    if (rc) { s->failed=rc; return failure(rc); }
    ++commits; s->dirty=0;
    return ESP_OK;
}
