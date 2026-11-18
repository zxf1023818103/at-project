#include "nan_oob.h"
#include "vendor/rtw_nan_struct.h"
#include "vendor/rtw_nan_cmd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t now, entropy = 123456;
static int random_error, vendor_error;
static unsigned installs;
uint32_t xTaskGetTickCount(void) { return now; }
int green2_nan_random(void *out, size_t n)
{ assert(n == 4); memcpy(out, &entropy, n); return random_error; }
int green2_nan_vendor(unsigned short command, const void *data, size_t len)
{
    const struct vendor_pairing_setpw *pw = data;
    assert(command == 0x1907 && len == sizeof(*pw) && pw->pw_len == 6);
    ++installs;
    return vendor_error;
}
uint8_t *green2_nan_mac(void)
{ static uint8_t id[] = {2,3,4,5,6,7}; return id; }
static void no_secret(unsigned expected)
{
    uint8_t out[20]; green2_nan_oob_read(out);
    assert(!memcmp(out, "GNO1", 4) && out[4] == expected);
    for (unsigned i = 5; i < 13; ++i) assert(out[i] == 0);
}
int main(void)
{
    char pin[7]; uint32_t gen, newer;
    assert(!nan_oob_pin(0, pin) && !strcmp(pin, "000000"));
    assert(!nan_oob_pin(4293999999u, pin) && !strcmp(pin, "999999"));
    assert(nan_oob_pin(4294000000u, pin) == -1);
    no_secret(NAN_OOB_CLOSED);
    assert(!green2_nan_oob_password(pin, &gen));
    assert(green2_nan_oob_pending() && !green2_nan_oob_pending());
    no_secret(NAN_OOB_PENDING);
    now = UINT32_MAX - 2000;
    green2_nan_oob_open();
    assert(green2_nan_oob_password(pin, &gen) && !strcmp(pin, "123456"));
    assert(!green2_nan_oob_pending()); /* Re-read cannot rotate an active PIN. */
    now += 299000;
    assert(green2_nan_oob_valid(gen));
    now += 1000;
    assert(!green2_nan_oob_valid(gen)); no_secret(NAN_OOB_CLOSED);
    assert(green2_nan_oob_pending()); green2_nan_oob_open();
    assert(green2_nan_oob_password(pin, &newer) && newer != gen);
    assert(!green2_nan_oob_valid(gen));
    green2_nan_oob_close(); assert(!green2_nan_oob_valid(newer)); no_secret(NAN_OOB_CLOSED);
    unsigned before = installs;
    random_error = -1; green2_nan_oob_open(); no_secret(NAN_OOB_ERROR);
    assert(installs == before);
    random_error = 0; entropy = UINT32_MAX; green2_nan_oob_open(); no_secret(NAN_OOB_ERROR);
    assert(installs == before);
    entropy = 42; vendor_error = -1; green2_nan_oob_open(); no_secret(NAN_OOB_ERROR);
    puts("BLE PIN entropy, lifecycle, expiry/wrap, generation and failure tests PASS");
}
