extern void LOGUART_PutChar(unsigned char c);
/* A platform-only SPE needs no keys. Storage supplies its own key backend. */
#include "tfm_plat_otp.h"
#include "tfm_plat_provisioning.h"
#ifndef GREEN2_TFM_STORAGE
enum tfm_plat_err_t tfm_plat_otp_init(void) { return TFM_PLAT_ERR_SUCCESS; }
int tfm_plat_provisioning_is_required(void) { return 0; }
enum tfm_plat_err_t tfm_plat_provisioning_perform(void)
{ return TFM_PLAT_ERR_UNSUPPORTED; }
void tfm_plat_provisioning_check_for_dummy_keys(void) {}
#else
#include <string.h>
#include "tfm_spm_log.h"
static void diag(const char *s, int rc)
{
    while (*s) LOGUART_PutChar(*s++);
    LOGUART_PutChar('0'+rc);
    LOGUART_PutChar('\n');
}
extern int green2_secure_random(unsigned char *, size_t);
int tfm_plat_provisioning_is_required(void)
{
    unsigned char key[32];
    if (tfm_plat_otp_read(PLAT_OTP_ID_HUK, sizeof(key), key)) return 1;
    unsigned zero = 0, erased = 0;
    for (unsigned i = 0; i < sizeof(key); ++i) { zero |= key[i]; erased |= key[i] ^ 255; key[i] = 0; }
    return !zero || !erased;
}
enum tfm_plat_err_t tfm_plat_provisioning_perform(void)
{
    unsigned char key[32];
    if (green2_secure_random(key, sizeof(key))) { diag("[TF-M] TRNG error=",1); return TFM_PLAT_ERR_SYSTEM_ERR; }
    enum tfm_plat_err_t rc = tfm_plat_otp_write(PLAT_OTP_ID_HUK, sizeof(key), key);
    diag("[TF-M] HUK provision=",rc);
    volatile unsigned char *p = key;
    for (unsigned i = 0; i < sizeof(key); ++i) p[i] = 0;
    return rc;
}
void tfm_plat_provisioning_check_for_dummy_keys(void)
{
    SPMLOG_INFMSG("[TF-M] Development HUK: TRNG-generated, secure flash-backed; eFuse unchanged.\r\n");
}
#endif
