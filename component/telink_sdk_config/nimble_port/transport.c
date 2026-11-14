/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "os/os_mbuf.h"
#include "host/ble_hs.h"
#include "nimble/transport.h"
#include "controller.h"
extern void tl3238_ble_log(const char *format, ...);

void ble_transport_ll_init(void) { tl3238_controller_start(); }

int ble_transport_to_ll_cmd_impl(void *buf)
{
    uint8_t *p = buf;
    int rc = tl3238_controller_send(1, p, 3u + p[2]);
    ble_transport_free(buf);
    return rc ? BLE_HS_EAGAIN : 0;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om)
{
    uint8_t packet[255];
    size_t len = OS_MBUF_PKTLEN(om);
    int rc = BLE_HS_EMSGSIZE;
    if (len <= sizeof(packet) && os_mbuf_copydata(om, 0, len, packet) == 0) {
        rc = tl3238_controller_send(2, packet, len) ? BLE_HS_EAGAIN : 0;
    }
    os_mbuf_free_chain(om);
    return rc;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om)
{
    os_mbuf_free_chain(om);
    return BLE_HS_ENOTSUP;
}

int tl3238_host_receive(const uint8_t *h4, size_t len)
{
    if (len < 3) return -1;
    if (h4[0] == 4) {
        if (len != (size_t)h4[2] + 3) return -1;
        /* Report privacy command status without logging IRKs or LTKs. */
        if (h4[1] == 0x0e && len >= 7) {
            unsigned opcode=h4[4] | (unsigned)h4[5]<<8;
            if ((opcode>=0x2027 && opcode<=0x202e) || opcode==0x204e)
                tl3238_ble_log("BLE privacy opcode=0x%04x status=0x%02x",opcode,h4[6]);
        }
        void *evt = ble_transport_alloc_evt(0);
        if (!evt) return -1;
        memcpy(evt, h4 + 1, len - 1);
        ble_transport_to_hs_evt(evt); /* ownership transferred */
        return 0;
    }
    if (h4[0] == 2) {
        if (len < 5 || len != 5u + h4[3] + ((size_t)h4[4] << 8)) return -1;
        struct os_mbuf *om = ble_transport_alloc_acl_from_ll();
        if (!om) return -1;
        if (os_mbuf_append(om, h4 + 1, len - 1) != 0) {
            os_mbuf_free_chain(om);
            return -1;
        }
        ble_transport_to_hs_acl(om);
        return 0;
    }
    return -1;
}
