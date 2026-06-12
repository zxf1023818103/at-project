/*
 * syscfg/syscfg.h — at-project override for the NimBLE 1.9.0 build on AmebaD KM4.
 *
 * NimBLE is normally configured by Mynewt's `newt` tool (syscfg.yml -> generated
 * syscfg.h). For our non-Mynewt CMake build we provide this small override header:
 * it pre-defines the handful of MYNEWT_VAL_* we change, then #include_next pulls
 * the upstream default (component/thirdparty/nimble/porting/nimble/include/syscfg/
 * syscfg.h) for everything else. Every upstream default is `#ifndef`-guarded, so
 * our pre-defines win. This dir MUST be first on the include path.
 */
#ifndef H_ATPROJECT_NIMBLE_SYSCFG_
#define H_ATPROJECT_NIMBLE_SYSCFG_

/* ---- HCI transport ----
 * NimBLE host runs on KM4 (HS = native); the BLE controller is separate silicon
 * reached over our HCI UART, so the controller side (LL) uses a CUSTOM transport
 * (component/nimble/transport_amebad.c provides ble_transport_to_ll_cmd/acl). */
#define MYNEWT_VAL_BLE_TRANSPORT_LL__socket   (0)   /* upstream default is socket (linux) */
#define MYNEWT_VAL_BLE_TRANSPORT_LL__native   (0)
#define MYNEWT_VAL_BLE_TRANSPORT_LL__custom   (1)

/* ---- features ---- */
#define MYNEWT_VAL_BLE_MESH                    (1)   /* BLE Mesh on */
#define MYNEWT_VAL_BLE_SM_LEGACY               (1)   /* SM legacy pairing */
#define MYNEWT_VAL_BLE_SM_SC                   (1)   /* SM LE Secure Connections (uses tinycrypt) */

/* not a host/linux simulator */
#define MYNEWT_VAL_BSP_SIMULATED               (0)

#endif /* H_ATPROJECT_NIMBLE_SYSCFG_ */

/* pull upstream defaults (porting/nimble/include/syscfg/syscfg.h) for the rest */
#include_next <syscfg/syscfg.h>

/* BLE Mesh sub-options — NOT in the upstream porting default (mesh off there); pulled
 * AFTER include_next so any BLE_MESH_* default that references a non-mesh MYNEWT_VAL
 * resolves. All #ifndef-guarded. */
#include "mesh_defaults.h"
