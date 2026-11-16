#include "ameba_soc.h"
#include <stddef.h>
#include <string.h>
int mbedtls_hardware_poll(void *ctx, unsigned char *out, size_t len, size_t *olen)
{
    (void)ctx;
    int rc = TRNG_get_random_bytes(out, len);
    *olen = rc == 0 ? len : 0;
    return rc;
}
