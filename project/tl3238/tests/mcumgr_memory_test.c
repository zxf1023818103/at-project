/* SPDX-License-Identifier: Apache-2.0
 * Real SMP/CBOR core + BLE reassembly, with only the ATT boundary mocked.
 * Read-only mmap requests detect writes on success and the tx-error fallback.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "smp_bt.c"

static uint8_t transmitted[4096];
static size_t transmitted_len;
static unsigned tx_calls;
static int fail_tx;
struct mock_mbuf { struct os_mbuf om; struct os_mbuf_pkthdr hdr; uint8_t data[244]; };
static struct mock_mbuf outbound, inbound;
static struct os_mbuf *mbuf(struct mock_mbuf *m, const void *data, unsigned n)
{
    assert(n <= sizeof m->data);
    memset(m,0,sizeof *m); m->om.om_data=m->data;
    m->om.om_pkthdr_len=sizeof m->hdr; m->om.om_len=m->hdr.omp_len=n;
    memcpy(m->data,data,n); return &m->om;
}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p, uint16_t n) { return mbuf(&outbound,p,n); }
int os_mbuf_copydata(const struct os_mbuf *om, int off, int n, void *dest)
{
    assert(off>=0 && n>=0 && off+n<=om->om_len);
    memcpy(dest,om->om_data+off,n); return 0;
}
uint16_t ble_att_mtu(uint16_t conn) { assert(conn==1); return 247; }
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s) { (void)s; return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s) { *s->characteristics->val_handle=10; return 0; }
void tl3238_ble_log(const char *fmt, ...) { (void)fmt; }
static int capture(void *arg, const uint8_t *p, size_t n)
{
    (void)arg; tx_calls++;
    if (fail_tx) { fail_tx=0; return MGMT_ERR_ENOMEM; }
    assert(transmitted_len+n <= sizeof transmitted);
    memcpy(transmitted+transmitted_len,p,n); transmitted_len+=n; return 0;
}
int ble_gatts_notify_custom(uint16_t conn, uint16_t handle, struct os_mbuf *om)
{ assert(conn==1 && handle==10); return capture(NULL,om->om_data,om->om_len); }
static int receive(const void *p, unsigned n)
{
    struct ble_gatt_access_ctxt c={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=mbuf(&inbound,p,n)};
    return smp_chr_access(1,10,&c,NULL);
}
static int echo(struct mgmt_ctxt *ctxt)
{
    CborValue v; uint8_t data[1137]; size_t n=sizeof data;
    if (cbor_value_map_find_value(&ctxt->it,"d",&v) ||
        !cbor_value_is_byte_string(&v) || cbor_value_copy_byte_string(&v,data,&n,NULL))
        return MGMT_ERR_EINVAL;
    CborError err=cbor_encode_text_stringz(&ctxt->encoder,"d");
    err|=cbor_encode_byte_string(&ctxt->encoder,data,n);
    return mgmt_err_from_cbor(err);
}
static const struct mgmt_handler handlers[]={{.mh_write=echo}};
static struct mgmt_group group={.mg_handlers=handlers,.mg_handlers_count=1,.mg_group_id=64};
static size_t request(uint8_t *out, unsigned bytes)
{
    uint8_t payload[1137]; assert(bytes<=sizeof payload);
    for (unsigned i=0;i<bytes;i++) payload[i]=(uint8_t)i;
    CborEncoder e,map; cbor_encoder_init(&e,out+8,1152-8,0);
    assert(!cbor_encoder_create_map(&e,&map,CborIndefiniteLength));
    assert(!cbor_encode_text_stringz(&map,"d"));
    assert(!cbor_encode_byte_string(&map,payload,bytes));
    assert(!cbor_encoder_close_container(&e,&map));
    size_t n=cbor_encoder_get_buffer_size(&e,out+8);
    memcpy(out,(uint8_t[]){2,0,n>>8,n,0,64,7,0},8); return 8+n;
}
static void check_response(const uint8_t *req, size_t n)
{
    assert(transmitted_len==n && transmitted[0]==3);
    assert(!memcmp(transmitted+1,req+1,n-1));
}
int main(void)
{
    mgmt_register_group(&group); assert(!smp_bt_register());
    assert(mcumgr_smp_max_packet()==1152);
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *readonly=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(readonly!=MAP_FAILED);
    uint8_t req[1153]; size_t n=request(req,1137); assert(n==1152);
    memcpy(readonly,req,n); assert(!mprotect(readonly,page,PROT_READ));
    assert(!mcumgr_smp_process(readonly,n,capture,NULL)); check_response(req,n);
    assert(!memcmp(readonly,req,n));
    /* A transport failure makes SMP reuse req as its error-response handle.
     * The shim must redirect writes to owned storage even on this path. */
    transmitted_len=tx_calls=0; fail_tx=1;
    assert(mcumgr_smp_process(readonly,n,capture,NULL)==MGMT_ERR_ENOMEM);
    assert(tx_calls>=1 && !memcmp(readonly,req,n));
    assert(mcumgr_smp_process(req,sizeof req,capture,NULL)==MGMT_ERR_ENOMEM);
    assert(!munmap(readonly,page));
    static const uint8_t missing_key[]={2,0,0,1,0,64,7,0,0xa0};
    transmitted_len=0;
    assert(mcumgr_smp_process(missing_key,sizeof missing_key,capture,NULL)==MGMT_ERR_EINVAL);
    assert(transmitted_len>8);
    /* Maximum packet through actual BLE reassembly, fragmented at ATT MTU. */
    transmitted_len=tx_calls=0;
    for (size_t off=0;off<n;) {
        size_t count=n-off; if(count>244)count=244;
        assert(!receive(req+off,count)); off+=count;
    }
    check_response(req,n); assert(s_rx_len==0 && tx_calls==5);
    /* Two small requests in one write, then repeated buffers with new contents. */
    n=request(req,32); uint8_t combined[244];
    memcpy(combined,req,n); memcpy(combined+n,req,n); transmitted_len=0;
    assert(!receive(combined,2*n)); assert(transmitted_len==2*n && !s_rx_len);
    for (unsigned i=0;i<1000;i++) {
        n=request(req,i%100); req[6]=(uint8_t)i;
        transmitted_len=0; assert(!receive(req,n)); check_response(req,n);
    }
    /* Reset discards a partial request; malformed length/CBOR do not poison next. */
    assert(!receive(req,5)); smp_bt_reset_rx(); assert(!s_rx_len);
    assert(receive((uint8_t[]){2,0,0xff,0xff,0,64,7,0},8)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
    transmitted_len=0;
    assert(!receive((uint8_t[]){2,0,0,1,0,64,7,0,0xff},9));
    assert(!s_rx_len);
    n=request(req,64); transmitted_len=0; assert(!receive(req,n)); check_response(req,n);
    puts("PASS: SMP immutable request, tx-error fallback, 1152-byte packet, ATT fragments, coalesced requests, 1000 buffer reuses, malformed/reset recovery (ASan/UBSan)");
    return 0;
}
