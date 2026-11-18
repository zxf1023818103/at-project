#include "FreeRTOS.h"
#include "semphr.h"
#include "FreeRTOS_CLI.h"
#include "os_wrapper/common.h"
#include "os_wrapper/mutex.h"
#include "tfm_ns_interface.h"
#include "tfm_platform_api.h"
#include "psa/client.h"
#include <stdio.h>
#include <string.h>

static unsigned selftest_flags, entropy_test_bytes;
static BaseType_t status_cmd(char *out, size_t len, const char *cmd)
{
    (void)cmd;
    snprintf(out,len,"TFM selftest=%u entropy=%u\r\n",selftest_flags,entropy_test_bytes);
    return pdFALSE;
}
void dplus_tfm_status_register(void)
{
    static const CLI_Command_Definition_t def={"tfm_status","tfm_status: completed boot self-tests\r\n",status_cmd,0};
    FreeRTOS_CLIRegisterCommand(&def);
}

int dplus_tfm_validate_layout(uint32_t off, uint32_t len)
{
    uint32_t args[2]={off,len}; psa_invec in={args,sizeof(args)};
    return tfm_platform_ioctl(0x8721d6,&in,NULL);
}
int dplus_tfm_active_slot(void)
{
    uint32_t active = 0;
    psa_outvec out = {&active, sizeof(active)};
    if (tfm_platform_ioctl(0x8721d3, NULL, &out)) return -1;
    return active == 0x10000 ? 0 : active == 0x190000 ? 1 : -1;
}
int __wrap_FLASH_WriteStream(uint32_t address, uint32_t length, uint8_t *data)
{
    struct { uint32_t address, length; uint8_t data[256]; } packet;
    while (length) {
        uint32_t n = length > 256 ? 256 : length;
        packet.address = address; packet.length = n; memcpy(packet.data, data, n);
        psa_invec in = {&packet, n + 8};
        if (tfm_platform_ioctl(0x8721d1, &in, NULL)) return -1;
        address += n; data += n; length -= n;
    }
    return 1;
}
void __wrap_FLASH_EraseXIP(uint32_t type, uint32_t address)
{
    /* Existing public callers erase sectors. Reject chip/block erase requests. */
    configASSERT(type == 2);
    uint32_t args[2] = {address, 4096};
    psa_invec in = {args, sizeof(args)};
    configASSERT(tfm_platform_ioctl(0x8721d2, &in, NULL) == 0);
}

void *os_wrapper_mutex_create(void) { return xSemaphoreCreateMutex(); }
uint32_t os_wrapper_mutex_acquire(void *p, uint32_t timeout)
{
    if (!p) return OS_WRAPPER_ERROR;
    return xSemaphoreTake(p, timeout == OS_WRAPPER_WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(timeout))
        == pdTRUE ? OS_WRAPPER_SUCCESS : OS_WRAPPER_ERROR;
}
uint32_t os_wrapper_mutex_release(void *p)
{ return p && xSemaphoreGive(p) == pdTRUE ? OS_WRAPPER_SUCCESS : OS_WRAPPER_ERROR; }

void dplus_tfm_client_init(void)
{
    configASSERT(tfm_ns_interface_init() == OS_WRAPPER_SUCCESS);
    /* Secure services share the SFN stack; NS FreeRTOS owns no secure context. */
    extern volatile uint32_t g_tfm_ns_mask_irq;
    g_tfm_ns_mask_irq = 1;
}
void dplus_tfm_selftest(void)
{
    uint32_t nonce = 0x12345678, result = 0;
    psa_invec in = {&nonce, sizeof(nonce)};
    psa_outvec out = {&result, sizeof(result)};
    int rc = tfm_platform_ioctl(0x8721d0, &in, &out);
    printf("[TF-M] PSA framework=%lu ping=%d result=%08lx %s\n",
        (unsigned long)psa_framework_version(), rc, (unsigned long)result,
        rc == 0 && result == (nonce ^ 0x5aa55aa5) ? "PASS" : "FAIL");
    configASSERT(rc == 0 && result == (nonce ^ 0x5aa55aa5));
    const uintptr_t rejected[] = {0x3000b000, 0x2000b000, 0x083f0000, 0xfffffff0};
    for (unsigned i=0; i<sizeof(rejected)/sizeof(rejected[0]); ++i) {
        psa_invec bad = {(void *)rejected[i], i == 3 ? 32 : 4};
        result = 0xdeadbeef; out.len = sizeof(result);
        configASSERT(tfm_platform_ioctl(0x8721d0, &bad, &out) != 0);
        configASSERT(result == 0xdeadbeef);
    }
    uint32_t denied[] = {0,4096};
    psa_invec deny = {denied, sizeof(denied)};
    configASSERT(tfm_platform_ioctl(0x8721d2, &deny, NULL) != 0);
    denied[0] = 0x3f0000;
    configASSERT(tfm_platform_ioctl(0x8721d2, &deny, NULL) != 0);
    denied[0] = dplus_tfm_active_slot() == 0 ? 0x10000 : 0x190000;
    configASSERT(tfm_platform_ioctl(0x8721d2, &deny, NULL) != 0);
    printf("[TF-M] secure/overflow buffers and boot/active/storage write rejection PASS\n");
    selftest_flags=1;
#ifdef AT_ENABLE_STORAGE
#ifdef AT_DPLUS_CRYPTO_KAT
    uint32_t bytes = 4096;
    psa_invec probe = {&bytes,sizeof(bytes)};
    for (unsigned i=0;i<16;++i)
        configASSERT(tfm_platform_ioctl(0x8721d5,&probe,NULL) == 0);
    entropy_test_bytes=65536;
    printf("[TF-M] TRNG 65536-byte continuous health check PASS\n");
#endif
    extern void dplus_storage_test(void);
    dplus_storage_test();
    selftest_flags |= 2;
#endif
}
