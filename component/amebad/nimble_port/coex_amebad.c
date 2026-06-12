/*
 * coex_amebad.c — WiFi/BT coexistence glue for NimBLE on AmebaD.
 *
 * On this combo chip the coex ARBITRATION is autonomous: lib_wlan.a
 * (rtw_coex_mailbox_thread) + the BT controller firmware coordinate over the
 * on-chip HW mailbox. The host's role is tiny:
 *   - rltk_coex_bt_enable(1): turn coex on. (Our vendored hci_normal_start() —
 *     hci_process.c, runs during every controller bring-up — already calls this;
 *     we re-assert it here so it's explicit and logged.)
 *   - rltk_wlan_btcoex_set_bt_state(on): hint the WiFi side that a BLE link is
 *     active so it weights the PTA accordingly. Driven from NimBLE GAP events.
 *
 * NB: lib_wlan.a does NOT call a host mailbox_to_bt() (verified via nm), so unlike
 * the old btgap rtk_coex.c we do NOT inject vendor HCI commands from the host —
 * no conflict with NimBLE's HCI command flow control.
 */
extern void rltk_coex_bt_enable(unsigned char enable);          /* lib_wlan.a (bt_intf.h) */
extern void rltk_wlan_btcoex_set_bt_state(unsigned char state); /* lib_wlan.a (wlan_intf.h) */
extern unsigned int DiagPrintf(const char *fmt, ...);

void amebad_nimble_coex_init(void)
{
    /* Coex is ALREADY enabled by the controller bring-up: our vendored
     * hci_normal_start() (hci_process.c, runs in the first bring-up step) calls
     * rltk_coex_bt_enable(1) and creates the WiFi-side coex context (pcoex). Calling
     * it again here errors "pcoex has been created", so we DON'T — just note it. */
    (void)rltk_coex_bt_enable;
    DiagPrintf("[NimBLE] coex active (rltk_coex_bt_enable done by bring-up; PTA via lib_wlan + ctrl fw)\n\r");
}

/* connected != 0 -> a BLE link is up; 0 -> idle. Called from NimBLE GAP events. */
void amebad_nimble_coex_set_conn(int connected)
{
    rltk_wlan_btcoex_set_bt_state(connected ? 1 : 0);
}
