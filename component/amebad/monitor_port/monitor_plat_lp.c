/*
 * monitor_plat_lp.c — KM0 (LP, Cortex-M23) per-core seam for the CLI console.
 * The only thing that differs per core is the identity string; everything else (the
 * shared engine in component/monitor_config, and the RX/switch/reboot/ROM glue in
 * monitor_cli.c) is common.
 */
#include "monitor.h"

const char *monitor_core_name(void)
{
    return "KM0";
}
