/*
 * console/console.h — minimal shim for NimBLE's console facade on AmebaD.
 *
 * NimBLE core (ble_hs.c, ble_gatts_lcl.c, ble_hs_adv.c, ...) #includes this
 * unconditionally and only ever calls console_printf(). The Mynewt console / the
 * porting npl/linux shim map it to printf; we do the same — our printf wrapper
 * (component/amebad/hci/hci_compat.c -> ROM _rtl_vsnprintf + DiagPrintf) provides
 * the symbol, so no newlib stdio impl is pulled.
 */
#ifndef H_ATPROJECT_NIMBLE_CONSOLE_
#define H_ATPROJECT_NIMBLE_CONSOLE_

#include <stdio.h>   /* printf prototype (impl = hci_compat.c) */

#define console_printf(_fmt, ...) printf(_fmt, ##__VA_ARGS__)

#endif /* H_ATPROJECT_NIMBLE_CONSOLE_ */
