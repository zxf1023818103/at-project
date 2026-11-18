#include <section_config.h>
#include <osdep_service.h>
#include <skbuff.h>

#define MAX_SKB_BUF_SIZE     1658	// should >= the size in wlan driver
/* skb pool depth — the driver's TX+RX share these fixed pools: MAX_SKB_BUF_NUM data buffers
 * (skb_data_pool) + MAX_LOCAL_SKB_NUM skb structs (skb_pool). 24 for the throughput images
 * (iperf/QUIC); the AWS/MINIMAL connect-only images (AT_NET_LOW_MEM) are low-rate, so 10 is ample.
 *
 * CRITICAL (found via OpenOCD live read 2026-06-18): the prebuilt lib_wlan.a WEAK-defines all of
 * nr_xmitframe/nr_xmitbuff/max_local_skb_num/max_skb_buf_num + skb_pool/skb_data_pool with its stock
 * defaults (8 data / 10 struct). This TU strong-defines them, but it lives in a static lib
 * (amebad_wlan_glue) and the linker pulls lib_wlan.a's freertos_skbuff.o first (for alloc_skb/kfree_skb)
 * — so nothing references THIS object and it's never pulled, silently leaving the pool at 8/10 (a 50%
 * cut of the configured depth). Fix: `rtw_opt_skbuf_force_link` below is referenced from realtek_netif.c
 * (always linked) to force this object in, so these strong defs win and the configured depth is real.
 * NB: nr_xmitframe/nr_xmitbuff MUST stay == MAX_SKB_BUF_NUM — lib_wlan.a couples the TX caps to the
 * pool size; decoupling them (TX < pool) corrupts its xmit accounting and hard-faults under load.
 * (This restores the intended pool depth; it does NOT fix the ~4 s sustained-upload wedge — that is a
 * lib_wlan.a/firmware-level hang reproduced identically on the stock Realtek SDK image, independent of
 * the IP stack and of pool depth/rate. See the iperf3 component memo.) */
#if defined( AT_NET_LOW_MEM )
#define MAX_SKB_BUF_NUM      10
#else
#define MAX_SKB_BUF_NUM      24   /* deeper TX/RX pipeline for the throughput images (iperf/QUIC) */
#endif
#define MAX_LOCAL_SKB_NUM    (MAX_SKB_BUF_NUM + 2)

/* Referenced from realtek_netif.c so the linker actually pulls this object (see note above). */
int rtw_opt_skbuf_force_link = 1;

/* DO NOT modify skb_buf and skb_data structure */
struct skb_buf {
	struct list_head list;
	struct sk_buff skb;
};

struct skb_data {
	struct list_head list;
	unsigned char buf[MAX_SKB_BUF_SIZE];
	atomic_t ref;
};

unsigned int nr_xmitframe = MAX_SKB_BUF_NUM;
unsigned int nr_xmitbuff = MAX_SKB_BUF_NUM;
int max_local_skb_num = MAX_LOCAL_SKB_NUM;
int max_skb_buf_num = MAX_SKB_BUF_NUM;

/* DO NOT access skb_pool and skb_data_pool out of wlan driver */
struct skb_buf skb_pool[MAX_LOCAL_SKB_NUM];

#define SKB_DATA_POOL_USING_GLOBAL_BUF	1
#if SKB_DATA_POOL_USING_GLOBAL_BUF
// SRAM_BD_DATA_SECTION default in SRAM. Can modify image2.icf to link to the end of SDRAM
SRAM_BD_DATA_SECTION
struct skb_data skb_data_pool[MAX_SKB_BUF_NUM];

#else
// Change to use heap (malloc) to save SRAM memory
SRAM_BD_DATA_SECTION
struct skb_data * skb_data_pool;

extern struct list_head skbdata_list;
extern int skbdata_used_num;
extern int max_skbdata_used_num;
void init_skb_data_pool(void)
{
	int i;
	
	//printf("\ninit_skb_data_pool\n");
	skb_data_pool = (struct skb_data *)rtw_zmalloc(max_skb_buf_num * sizeof(struct skb_data));
	if(!skb_data_pool){
		printf("\nskb_data_pool alloc fail\n");
		return;
	}
	
	memset(skb_data_pool, '\0', max_skb_buf_num*sizeof(struct skb_data));
	INIT_LIST_HEAD(&skbdata_list);
	
	for (i=0; i<max_skb_buf_num; i++) {
		INIT_LIST_HEAD(&skb_data_pool[i].list);
		list_add_tail(&skb_data_pool[i].list, &skbdata_list);
	}
	skbdata_used_num = 0;	
	max_skbdata_used_num = 0;
}

void deinit_skb_data_pool(void)
{
	//printf("\ndeinit_skb_data_pool\n");
	rtw_mfree(skb_data_pool, MAX_SKB_BUF_NUM * sizeof(struct skb_data));
}
#endif
