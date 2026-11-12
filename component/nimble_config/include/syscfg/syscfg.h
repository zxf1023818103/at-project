/*
 * syscfg/syscfg.h — at-project override for the NimBLE 1.9.0 build on AmebaD KM4.
 *
 * NimBLE is normally configured by Mynewt's `newt` tool (syscfg.yml -> generated
 * syscfg.h). For our non-Mynewt CMake build we provide this small override header:
 * it pre-defines the handful of MYNEWT_VAL_* we change, then #include_next pulls
 * the upstream default (component/nimble_config/nimble/porting/nimble/include/syscfg/
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
#ifdef AT_NIMBLE_COC
#define MYNEWT_VAL_BLE_MESH                    (0)
#define MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM        (1)
#define MYNEWT_VAL_BLE_L2CAP_COC_MPS            (247)
#define MYNEWT_VAL_BLE_L2CAP_COC_SDU_BUFF_COUNT (1)
#elif defined(AT_NIMBLE_GATT)
#define MYNEWT_VAL_BLE_MESH                    (0)
#define MYNEWT_VAL_BLE_ATT_PREFERRED_MTU       (517)
#else
#define MYNEWT_VAL_BLE_MESH                    (1)   /* BLE Mesh on */
#endif
#define MYNEWT_VAL_BLE_SM_LEGACY               (1)   /* SM legacy pairing */
#define MYNEWT_VAL_BLE_SM_SC                   (1)   /* SM LE Secure Connections (uses tinycrypt) */

/* not a host/linux simulator */
#define MYNEWT_VAL_BSP_SIMULATED               (0)

/* ---- .bss pool trims (shrink static os_membuf_t[] pools -> grow the heap) ----
 * These pools are compile-time static .bss (no malloc/heap switch in NimBLE 1.9.0),
 * and .bss directly shrinks the heap region (heap = __sram_end__ - __bss_end__). We
 * run as a single-connection peripheral (advertise + a sample GATT), no LE Audio and
 * no long/prepared writes, so the upstream defaults are oversized. Trimming them frees
 * ~7 KB of heap permanently with no functional impact for our use case.
 * (For our transport config -- HS=native, LL=custom -- transport.c uses the *_FROM_LL_*
 * counts: POOL_ACL_COUNT = ACL_FROM_LL_COUNT, POOL_ISO_COUNT = ISO_FROM_LL_COUNT.) */
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_LL_COUNT (0)   /* no isochronous (LE Audio) -> drop pool_iso_buf (~3.5 KB) */
#define MYNEWT_VAL_BLE_TRANSPORT_ISO_FROM_HS_COUNT (0)
#define MYNEWT_VAL_BLE_TRANSPORT_ACL_FROM_LL_COUNT (16)  /* controller->host ACL buffers: 4 exhausted under the
                                                          * bletp RX flood -> hci_h4 alloc returned NULL and
                                                          * hci_h4_sm_rx NULL-deref'd (secure fault). 16 sustains it. */
#define MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES    (4)   /* was 64; no long/prepared writes (~1.3 KB) */
#if defined(AT_NIMBLE_COC) || defined(AT_NIMBLE_GATT)
#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT              (64)
#else
#define MYNEWT_VAL_MSYS_1_BLOCK_COUNT              (24)  /* was 10; the bletp notification blast needs mbuf headroom
                                                          * on both the TX (notify) and RX (reassembly) paths */
#endif

#endif /* H_ATPROJECT_NIMBLE_SYSCFG_ */

/* pull upstream defaults (porting/nimble/include/syscfg/syscfg.h) for the rest */
#include_next <syscfg/syscfg.h>

/* BLE Mesh sub-options — NOT in the upstream porting default (mesh off there); pulled
 * AFTER include_next so any BLE_MESH_* default that references a non-mesh MYNEWT_VAL
 * resolves. All #ifndef-guarded. */
#include "mesh_defaults.h"
