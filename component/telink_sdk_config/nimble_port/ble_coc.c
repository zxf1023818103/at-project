/* SPDX-License-Identifier: Apache-2.0 */
/* TL3238 CLI/log integration. The service and protocol are shared components. */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "FreeRTOS_CLI.h"
#include "host/ble_hs.h"
#include "ble_coc.h"
#include "coc_stream.h"
extern void tl3238_ble_log(const char *format, ...);

static BaseType_t coc_command(char *out, size_t size, const char *command)
{
    BaseType_t n, extra;
    const char *p=FreeRTOS_CLIGetParameter(command, 1, &n);
    if (p) {
        if (n!=8 || strncmp(p, "selftest", 8) || FreeRTOS_CLIGetParameter(command, 2, &extra))
            snprintf(out, size, "Usage: coc [selftest]\r\n");
        else if (ble_coc_nimble_is_open()) snprintf(out, size, "CoC selftest requires no open channel\r\n");
        else {
            int rc=coc_stream_selftest();
            snprintf(out, size, "COC SELFTEST %s synthetic=1 rc=%d\r\n", rc ? "FAIL" : "PASS", rc);
        }
        return pdFALSE;
    }
    ble_coc_nimble_status(out, size);
    return pdFALSE;
}
int tl3238_coc_init(void)
{
    int rc=ble_coc_nimble_init(tl3238_ble_log);
    if (rc) return rc;
    static const CLI_Command_Definition_t cmd={"coc", "coc [selftest]: Flash Studio LE CoC status\r\n", coc_command, -1};
    return FreeRTOS_CLIRegisterCommand(&cmd)==pdPASS ? 0 : BLE_HS_ENOMEM;
}
