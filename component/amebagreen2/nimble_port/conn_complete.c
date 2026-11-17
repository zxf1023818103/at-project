/* This controller can resolve a peer identity while omitting peer_rpa from
 * Enhanced Connection Complete. Recover it on the host task before NimBLE
 * constructs the connection (and before Secure Connections uses f5/f6). */
#include <stdio.h>
#include <string.h>
#include "host/ble_hs.h"
#include "nimble/hci_common.h"
#include "ble_gap_priv.h"
#include "peer_rpa.h"

extern int ble_hs_hci_cmd_tx(uint16_t, const void *, uint8_t, void *, uint8_t);
extern int __real_ble_gap_rx_conn_complete(struct ble_gap_conn_complete *, uint8_t);

static int read_peer_rpa(uint8_t type, const uint8_t identity[6], uint8_t rpa[6])
{
    struct ble_hci_le_rd_peer_resolv_addr_cp cmd = { .peer_addr_type = type };
    struct ble_hci_le_rd_peer_resolv_addr_rp reply = {0};
    memcpy(cmd.peer_id_addr, identity, 6);
    int rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_PEER_RESOLV_ADDR),
                             &cmd, sizeof(cmd), &reply, sizeof(reply));
    if (!rc) memcpy(rpa, reply.rpa, 6);
    printf("[NAN-BLE] read peer RPA rc=%d\n", rc);
    return rc;
}

int __wrap_ble_gap_rx_conn_complete(struct ble_gap_conn_complete *event, uint8_t instance)
{
    struct ble_gap_conn_complete fixed = *event;
    if (!event->status && event->role == BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE &&
        green2_recover_peer_rpa(fixed.peer_addr_type, fixed.peer_addr, fixed.peer_rpa, read_peer_rpa))
        printf("[NAN-BLE] recovered missing peer RPA\n");
    return __real_ble_gap_rx_conn_complete(&fixed, instance);
}
