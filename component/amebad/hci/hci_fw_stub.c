/*
 * hci_fw_stub.c — weak fallbacks for the BT controller firmware blob symbols.
 *
 * The SDK's board/amebad/src/rtl8721d_bt.c provided these weak stubs, but it
 * also pulls in <drv_types.h> (the whole closed WLAN-driver header tree) for
 * bt_get_patch_code_8721d() / Hal_BT_Is_Supported() — functions our in-tree
 * lib_wlan.a already defines (T). So we drop rtl8721d_bt.c and keep only the
 * weak fw symbols here.
 *
 * The ACTIVE blob (hci_normal_patch.c for normal / hci_mp_patch.c for mp) is
 * compiled into the app_km4 EXECUTABLE (see app_km4/CMakeLists.txt), so its
 * STRONG rtlbt_fw / rtlbt_fw_len (or _mp) override the matching weak symbols
 * below; the OTHER, un-compiled blob stays the empty weak stub here.
 *
 * NB: the blob lives in the executable (not the amebad_hci archive) precisely so
 * the strong definition is always linked — a strong blob inside the archive
 * would NOT get pulled, because these weak stubs already satisfy hci_fwconfig.c's
 * rltk_bt_get_*_patch() references, leaving the controller with an empty {0}
 * patch ("we use single patch / not support single patch on rom").
 */
__attribute__((weak)) const unsigned char rtlbt_fw[]    = { 0 };
__attribute__((weak)) const unsigned char rtlbt_mp_fw[] = { 0 };
__attribute__((weak)) unsigned int        rtlbt_fw_len    = 0;
__attribute__((weak)) unsigned int        rtlbt_mp_fw_len = 0;
