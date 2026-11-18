/* NAN task owns password installation. BLE callbacks only queue requests and
 * read a short, critical-section-protected snapshot. No secret is logged. */
#include "nan_oob.h"
#include "nan_network.h"
#include "rtw_nan_struct.h"
#include "rtw_nan_cmd.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mbedtls/platform_util.h"
#include <stdio.h>
#include <string.h>

extern int green2_nan_random(void *, size_t);
extern int green2_nan_vendor(unsigned short, const void *, size_t);
static char current_pin[7];
static TickType_t opened;
static uint32_t generation;
static unsigned state;

/* Caller holds the critical section; tick subtraction also handles wrap. */
static unsigned remaining(void)
{
    TickType_t elapsed = xTaskGetTickCount() - opened;
    if (state != NAN_OOB_READY) return 0;
    if (elapsed >= pdMS_TO_TICKS(NAN_OOB_WINDOW_SECONDS * 1000u)) {
        state = NAN_OOB_CLOSED;
        mbedtls_platform_zeroize(current_pin, sizeof(current_pin));
        return 0;
    }
    return NAN_OOB_WINDOW_SECONDS - elapsed / configTICK_RATE_HZ;
}
void green2_nan_oob_close(void)
{
    taskENTER_CRITICAL();
    state = NAN_OOB_CLOSED;
    mbedtls_platform_zeroize(current_pin, sizeof(current_pin));
    taskEXIT_CRITICAL();
}
void green2_nan_oob_error(void)
{
    green2_nan_oob_close();
    taskENTER_CRITICAL(); state = NAN_OOB_ERROR; taskEXIT_CRITICAL();
}
int green2_nan_oob_pending(void)
{
    taskENTER_CRITICAL();
    int needed = !remaining() && state != NAN_OOB_PENDING;
    if (needed) state = NAN_OOB_PENDING;
    taskEXIT_CRITICAL();
    return needed;
}
void green2_nan_oob_open(void)
{
    char pin[7] = {0};
    uint32_t entropy = 0;
    int rc = -1;
    struct vendor_pairing_setpw pw = {0};
    for (unsigned i = 0; i < 16; ++i) {
        if (green2_nan_random(&entropy, sizeof(entropy))) break;
        if (!nan_oob_pin(entropy, pin)) { rc = 0; break; }
    }
    if (!rc) {
        memcpy(pw.pw, pin, 6); pw.pw_len = 6;
        rc = green2_nan_vendor(0x1907, &pw, sizeof(pw));
    }
    taskENTER_CRITICAL();
    if (!rc) {
        memcpy(current_pin, pin, sizeof(pin));
        opened = xTaskGetTickCount();
        if (!++generation) ++generation;
        state = NAN_OOB_READY;
    } else {
        state = NAN_OOB_ERROR;
        mbedtls_platform_zeroize(current_pin, sizeof(current_pin));
    }
    taskEXIT_CRITICAL();
    mbedtls_platform_zeroize(pin, sizeof(pin));
    mbedtls_platform_zeroize(&pw, sizeof(pw));
    mbedtls_platform_zeroize(&entropy, sizeof(entropy));
    printf("[NAN-BLE] pairing window rc=%d seconds=%u\n", rc, rc ? 0 : NAN_OOB_WINDOW_SECONDS);
}
int green2_nan_oob_password(char pin[7], uint32_t *value)
{
    taskENTER_CRITICAL();
    int ready = remaining() != 0;
    if (ready) { memcpy(pin, current_pin, 7); *value = generation; }
    taskEXIT_CRITICAL();
    return ready;
}
int green2_nan_oob_valid(uint32_t value)
{
    taskENTER_CRITICAL();
    int valid = remaining() && generation == value;
    taskEXIT_CRITICAL();
    return valid;
}
void green2_nan_oob_read(uint8_t out[NAN_OOB_PACKET_SIZE])
{
    taskENTER_CRITICAL();
    unsigned seconds = remaining();
    nan_oob_packet(out, state, seconds, current_pin, green2_nan_mac());
    taskEXIT_CRITICAL();
}
