/* Standalone GATT fixture: preserve ATT responses while bulk notifications
 * congest the single test link. All calls run on BTU; no cross-task queue.
 * SUCCESS/CONGESTED from L2CAP transfers ownership exactly once. */
#include <stdio.h>
#include <string.h>
#include "common/bt_target.h"
#include "gatt_int.h"
#include "stack/l2c_api.h"
#include "osi/allocator.h"
#include "bflb_mtimer.h"
#include "bluedroid_bouffalo.h"

#define CONTROL_DEPTH 4
static struct {
    tGATT_TCB *owner;
    BD_ADDR peer;
    UINT16 cid;
    BT_HDR *packets[CONTROL_DEPTH];
    unsigned count;
    bool congested;
    uint64_t queued_us;
} control;

void bluedroid_bouffalo_gatt_control_reset(void)
{
    for (unsigned i=0; i<control.count; ++i) osi_free(control.packets[i]);
    memset(&control,0,sizeof(control));
}
static bool blocked(tGATT_TCB *tcb)
{
    return control.congested || (tcb->att_lcid==L2CAP_ATT_CID &&
                                L2CA_CheckIsCongest(L2CAP_ATT_CID,tcb->peer_bda));
}
bool bluedroid_bouffalo_gatt_control_pending(void) { return control.count != 0; }

void bluedroid_bouffalo_gatt_control_flush(void)
{
    while (control.count) {
        tGATT_TCB *tcb=control.owner;
        if (!tcb || !tcb->in_use || tcb->att_lcid!=control.cid ||
            memcmp(tcb->peer_bda,control.peer,sizeof(BD_ADDR))) {
            bluedroid_bouffalo_gatt_control_reset();
            return;
        }
        if (blocked(tcb)) return;
        BT_HDR *packet=control.packets[0];
        unsigned opcode=*((UINT8 *)(packet+1)+packet->offset);
        --control.count;
        memmove(control.packets,control.packets+1,control.count*sizeof(packet));
        /* The lower send consumes packet even on failure. Never retry a
         * post-enqueue GATT_CONGESTED result: it is already queued in L2CAP. */
        unsigned status=attp_send_msg_to_l2cap(tcb,packet);
        printf("[gatt-control] FLUSH opcode=%02x status=%u wait_us=%lu remaining=%u\r\n",
               opcode,status,(unsigned long)(bflb_mtimer_get_time_us()-control.queued_us),control.count);
        if (status==GATT_CONGESTED) control.congested=true;
        if (status!=GATT_SUCCESS) return;
    }
}
void bluedroid_bouffalo_gatt_control_congestion(bool full)
{
    control.congested=full;
    /* L2CAP invokes this callback from its TX scheduler. Sending recursively
     * here can overtake the PDU the scheduler has just removed from its queue.
     * The fixture's next BTU process pass flushes before bulk notifications. */
}

/* Called by attp_send_sr_msg only for responses, never notifications/indications.
 * The test host permits one ACL link; four pending responses are a hard cap. */
tGATT_STATUS bluedroid_gatt_control_send(tGATT_TCB *tcb, BT_HDR *packet)
{
    if (!control.count && !blocked(tcb)) return attp_send_msg_to_l2cap(tcb,packet);
    if (control.count==CONTROL_DEPTH || (control.count && control.owner!=tcb)) {
        printf("[gatt-control] response queue full\r\n");
        osi_free(packet);
        return GATT_NO_RESOURCES;
    }
    if (!control.count) {
        control.owner=tcb; control.cid=tcb->att_lcid;
        memcpy(control.peer,tcb->peer_bda,sizeof(BD_ADDR));
        control.queued_us=bflb_mtimer_get_time_us();
    }
    unsigned opcode=*((UINT8 *)(packet+1)+packet->offset);
    control.packets[control.count++]=packet;
    printf("[gatt-control] DEFER opcode=%02x cid=%04x depth=%u\r\n",opcode,tcb->att_lcid,control.count);
    return GATT_SUCCESS;
}
