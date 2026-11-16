#include "nan_wire.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    const uint8_t ndi[6] = {2,1,2,3,4,5};
    uint8_t attrs[] = {0x29,11,0, 1,0,0, 2,1,2,3,4,5, 7,4};
    assert(green2_nan_secure_ndp(attrs, sizeof(attrs), 7, ndi, NULL));
    for (size_t n = 0; n < sizeof(attrs); ++n)
        assert(!green2_nan_secure_ndp(attrs, n, 7, ndi, NULL));
    attrs[13] = 0;
    assert(!green2_nan_secure_ndp(attrs, sizeof(attrs), 7, ndi, NULL));
    attrs[13] = 4;
    assert(!green2_nan_secure_ndp(attrs, sizeof(attrs), 8, ndi, NULL));
    attrs[6] ^= 1;
    assert(!green2_nan_secure_ndp(attrs, sizeof(attrs), 7, ndi, NULL));
    attrs[6] ^= 1;
    uint8_t both[sizeof(attrs)*2]; memcpy(both, attrs, sizeof(attrs)); memcpy(both+sizeof(attrs), attrs, sizeof(attrs));
    assert(!green2_nan_secure_ndp(both, sizeof(both), 7, ndi, NULL));
    both[0] = 0x10;
    assert(green2_nan_secure_ndp(both, sizeof(both), 7, ndi, NULL));
    uint8_t token = 0;
    assert(green2_nan_secure_ndp(both, sizeof(both), 7, ndi, &token) && token == 1);
    both[sizeof(attrs) + 3] ^= 1;
    assert(!green2_nan_secure_ndp(both, sizeof(both), 7, ndi, NULL));
    return 0;
}
