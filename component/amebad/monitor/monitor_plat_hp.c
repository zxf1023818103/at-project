/*
 * monitor_plat_hp.c — KM4 (HP, Cortex-M33) per-core seam for the CLI console.
 * The only thing that differs per core is the identity string; everything else (engine,
 * commands, the WDG reboot) is shared in monitor_cli.c.
 */
#include "monitor.h"

const char *amebad_monitor_core_name(void)
{
    return "KM4";
}
