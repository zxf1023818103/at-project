/* The closed WHC driver exchanges lwIP-shaped packets. Only this ABI shim uses
 * lwIP headers; IP, DHCP, DNS and sockets are provided by FreeRTOS+TCP. */
#include "lwip/pbuf.h"
#include "FreeRTOS.h"
#include <string.h>
#include <stddef.h>
extern void amebagreen2_rx(const void *data, size_t len);
_Static_assert(offsetof(struct pbuf, payload) == 4, "WHC packet payload ABI");
_Static_assert(offsetof(struct pbuf, tot_len) == 8, "WHC packet length ABI");
struct pbuf *pbuf_alloc(pbuf_layer layer, u16_t len, pbuf_type type)
{
    (void)layer; (void)type;
    struct pbuf *p = pvPortMalloc(32 + len);
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    p->payload = (unsigned char *)p + 32;
    p->len = p->tot_len = len;
    p->ref = 1;
    p->type_internal = (uint8_t)PBUF_RAM;
    return p;
}
u8_t pbuf_free(struct pbuf *p)
{
    u8_t count = 0;
    while (p) {
        struct pbuf *next = p->next;
        if (--p->ref) break;
        vPortFree(p); count++; p = next;
    }
    return count;
}
void LwIP_ethernetif_recv_inic(uint8_t idx, struct pbuf *p)
{
    if (idx == 0 && p && p->len == p->tot_len) amebagreen2_rx(p->payload, p->len);
    pbuf_free(p);
}
