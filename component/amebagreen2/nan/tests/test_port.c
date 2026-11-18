#include <stddef.h>
#include <stdint.h>
#include <sys/random.h>
#include <time.h>
#include <stdlib.h>
struct eapol_sm;
/* The native PASN initiator has no EAPOL state machine. Its PMKSA cache must
 * never enter the EAP-only reauthentication branch. */
void eapol_sm_request_reauth(struct eapol_sm *sm) { (void)sm; abort(); }
void green2_nan_heap_check(const char *where) { (void)where; }
int green2_nan_random(void *out, size_t len)
{ return getrandom(out, len, 0) == (long)len ? 0 : -1; }
uint32_t xTaskGetTickCount(void)
{ struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint32_t)(t.tv_sec * 1000ULL + t.tv_nsec / 1000000); }
void vTaskDelay(uint32_t ticks)
{ struct timespec t = {ticks / 1000, (ticks % 1000) * 1000000}; nanosleep(&t, 0); }
__attribute__((weak)) int green2_nan_mgmt(const unsigned char *p, size_t n) { (void)p; (void)n; return -1; }
__attribute__((weak)) int green2_nan_vendor(unsigned short id, const void *p, size_t n) { (void)id; (void)p; (void)n; return -1; }
__attribute__((weak)) int green2_nan_store(void *p, size_t n, int write) { (void)p; (void)n; (void)write; return -1; }
__attribute__((weak)) void green2_nan_authenticated(const unsigned char *p, unsigned char t) { (void)p; (void)t; }
