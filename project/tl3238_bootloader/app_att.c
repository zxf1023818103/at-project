/* SPDX-License-Identifier: Apache-2.0 */
/* Size probe: GAP + GATT + the real Telink OTA server, without HID/SPP/BAS.
 * This still uses the SDK's A/B OTA lifecycle; it is not a resident loader.
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

static const u16 primary = GATT_UUID_PRIMARY_SERVICE;
static const u16 characteristic = GATT_UUID_CHARACTER;
static const u16 ccc_uuid = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 gap_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const u16 name_uuid = GATT_UUID_DEVICE_NAME;
static const u16 appearance_uuid = GATT_UUID_APPEARANCE;
static const u16 appearance = GAP_APPEARANCE_UNKNOWN;
static const u16 gatt_uuid = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 changed_uuid = GATT_UUID_SERVICE_CHANGE;
static const u8 name[] = "TL3238-OTA-Probe";
static const u8 ota_service[] = {TELINK_OTA_UUID_SERVICE};
static const u8 ota_uuid[] = {TELINK_SPP_DATA_OTA};

/* Handles: GAP 1..5, GATT 6..9, OTA 10..13. */
static const u8 name_decl[] = {CHAR_PROP_READ, 3, 0, U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME)};
static const u8 appearance_decl[] = {CHAR_PROP_READ, 5, 0, U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE)};
static const u8 changed_decl[] = {CHAR_PROP_INDICATE, 8, 0, U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE)};
static const u8 ota_decl[] = {CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY,
                            12, 0, TELINK_SPP_DATA_OTA};
static u16 changed[2];
static u16 changed_ccc;
static u16 ota_ccc;
static u8 ota_value;

#define PTR(p) ((u8 *)(size_t)(p))
#define ATTR(n, perm, uuid, value, callback) \
    {n, perm, sizeof(uuid), sizeof(value), PTR(&(uuid)), PTR(&(value)), callback, NULL}
static const attribute_t attributes[] = {
    {13, 0, 0, 0, NULL, NULL, NULL, NULL},
    ATTR(5, ATT_PERMISSIONS_READ, primary, gap_uuid, NULL),
    ATTR(0, ATT_PERMISSIONS_READ, characteristic, name_decl, NULL),
    {0, ATT_PERMISSIONS_READ, 2, sizeof(name) - 1, PTR(&name_uuid), PTR(name), NULL, NULL},
    ATTR(0, ATT_PERMISSIONS_READ, characteristic, appearance_decl, NULL),
    ATTR(0, ATT_PERMISSIONS_READ, appearance_uuid, appearance, NULL),
    ATTR(4, ATT_PERMISSIONS_READ, primary, gatt_uuid, NULL),
    ATTR(0, ATT_PERMISSIONS_READ, characteristic, changed_decl, NULL),
    ATTR(0, ATT_PERMISSIONS_READ, changed_uuid, changed, NULL),
    ATTR(0, ATT_PERMISSIONS_RDWR, ccc_uuid, changed_ccc, NULL),
    ATTR(4, ATT_PERMISSIONS_READ, primary, ota_service, NULL),
    ATTR(0, ATT_PERMISSIONS_READ, characteristic, ota_decl, NULL),
    ATTR(0, ATT_PERMISSIONS_RDWR, ota_uuid, ota_value, otaWrite),
    ATTR(0, ATT_PERMISSIONS_RDWR, ccc_uuid, ota_ccc, NULL),
};

void my_gatt_init(void)
{
    bls_att_setAttributeTable(PTR(attributes));
}
