#pragma once
#include "mbedtls_user_config.h"
/* Pairing persistence uses the radio's existing LittleFS over USB. Picolibc
 * has no directory API; this AP-only crypto profile does not load X.509 files. */
#undef MBEDTLS_FS_IO
