/* Closed WHC dispatch tables include optional WTN/AP entry points even in a
 * station-only image. Reject unsupported tunnel requests; never report success. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
void wtn_rnat_ap_init(uint8_t enable) { if (enable) printf("WTN NAT is disabled\n"); }
int wtn_socket_init(uint8_t enable, uint8_t ap) { (void)enable; (void)ap; return -1; }
int wtn_socket_send(uint8_t *buf, uint32_t len) { (void)buf; (void)len; return -1; }
int dhcps_ip_in_table_check(uint8_t gate, uint8_t ip) { (void)gate; (void)ip; return 0; }
static int path_for(char *path, size_t size, const char *key)
{
    if (!key || !*key || strchr(key, '/') || strlen(key) > 128) return -1;
    return snprintf(path, size, "/sdk_kv/%s", key) >= (int)size ? -1 : 0;
}
int32_t rt_kv_get(const char *key, void *buf, int32_t len)
{
    char path[160];
    if (len < 0 || path_for(path,sizeof(path),key)) return -1;
    FILE *f = fopen(path,"rb"); if (!f) return -1;
    int32_t n = fread(buf,1,len,f); fclose(f); return n;
}
int32_t rt_kv_set(const char *key, const void *buf, int32_t len)
{
    char path[160];
    if (len < 0 || path_for(path,sizeof(path),key)) return -1;
    mkdir("/sdk_kv",0777);
    FILE *f = fopen(path,"wb"); if (!f) return -1;
    int32_t n = fwrite(buf,1,len,f); int rc = fclose(f);
    return n == len && rc == 0 ? n : -1;
}
