/**
 ****************************************************************************************
 *
 * @file bl_tx.c
 * Copyright (C) Bouffalo Lab 2016-2018
 *
 ****************************************************************************************
 */

#include <string.h>
#include <stdio.h>
#include <FreeRTOS.h>          /* pvPortMalloc / vPortFree for the TX block (was lwIP mem/pbuf) */
#include <bflb_os_private.h>

#include "bl_tx.h"
#include "bl_irqs.h"
#include "bl_utils.h"
#include "mm.h"           /* kmalloc(MM_FLAG_HEAP_WRAM_0) / kfree for the ram_wifi TX block */

#define RETRY_LIMIT_REACHED_BIT    (1 << 16)
#define LIFETIME_EXPIRED_BIT       (1 << 17)
#define FRAME_REPUSHABLE_PS_BIT    (1 << 19)
#define FRAME_REPUSHABLE_CHAN_BIT  (1 << 20)
#define FRAME_SUCCESSFUL_TX_BIT    (1 << 23)
#define DESC_DONE_TX_BIT           (1 << 31)
#define DESC_DONE_SW_TX_BIT        (1 << 30)

#define IS_BC_MC(byte)             ((byte) & 0x01)
#define BIT_VIF(idx)               (1 << (idx))
#define BIT_STA(idx)               (1 << (idx))

/* Trigger Flag */
static uint32_t tx_cntrl_sta_trigger = 0;
static uint32_t tx_cntrl_sta_trigger_pending = 0;
extern struct bl_hw wifi_hw;
int internel_cal_size_tx_hdr = sizeof(struct bl_txhdr);

/* WiFi MTU + 14 eth hdr; was pulled via lwIP before. */
#ifndef WIFI_MTU_SIZE
#define WIFI_MTU_SIZE 1514
#endif

void  bl_irq_handler(void);
#ifdef CFG_NETBUS_WIFI_ENABLE
int update_tx_pbuf_free_cnt_to_scratch_reg(void);
#endif

static void bl_tx_cntrl_purge_check(struct bl_sta *sta, uint8_t only_check)
{
    if (!utils_list_is_empty(&sta->pending_list) ||
        !utils_list_is_empty(&sta->waiting_list))
    {
        if (only_check)
        {
            /* Only check */
            bflb_os_printf("[TX] Have remaining packets when checking!\n\r");
        }
        else
        {
            /* Purging, trigger tx and let fw cfm to handle packets */
            bflb_os_enter_critical();
            bl_irq_handler();
            tx_cntrl_sta_trigger |= BIT_STA(sta->sta_idx);
            bflb_os_exit_critical();
        }
    }
}

static inline int bl_tx_cntrl_check_fc(struct bl_sta *sta)
{
    return ((0 == sta->fc_ps) &&
            (0 == wifi_hw.vif_table[sta->vif_idx].fc_chan));
}

static uint32_t bl_tx_cntrl_update_fc(struct ke_tx_fc *tx_fc_field)
{
    uint32_t sta_trigger = 0, fixed_sta_idx;

    /* FW fc  trigger: consider interface/sta_id/reason */
    for (uint8_t i = 0; i < 2; i++)
    {
        if (tx_fc_field->vif_bits & BIT_VIF(i))
        {
            /* VIF_STA */
            if (i == 0)
            {
                fixed_sta_idx = wifi_hw.vif_table[BL_VIF_STA].fixed_sta_idx;
                if (tx_fc_field->sta.fc_chan)
                {
                    wifi_hw.vif_table[BL_VIF_STA].fc_chan = 0;
                }
                if (tx_fc_field->sta.fc_ps)
                {
                    wifi_hw.sta_table[fixed_sta_idx].fc_ps = 0;
                }
                sta_trigger |= BIT_STA(fixed_sta_idx);
            }
            /* VIF_AP (Broadcast + Remote STAs) */
            else
            {
                fixed_sta_idx = wifi_hw.vif_table[BL_VIF_AP].fixed_sta_idx;
                if (tx_fc_field->ap.fc_chan)
                {
                    wifi_hw.vif_table[BL_VIF_AP].fc_chan = 0;
                    sta_trigger |= BIT_STA(fixed_sta_idx);
                }
                sta_trigger |= tx_fc_field->ap.fc_ps_sta_bits;
            }
        }
    }

    return sta_trigger;
}

static int bl_tx_cntrl_get_sta_id(int is_sta, int is_broadcast, struct mac_addr *addr)
{
    struct bl_sta* sta;
    int ap_idx = -1, bcmc_sta_idx = -1, links_num;

    /* For STA */
    if (is_sta)
    {
        links_num = wifi_hw.vif_table[BL_VIF_STA].links_num;
        ap_idx    = wifi_hw.vif_table[BL_VIF_STA].fixed_sta_idx;
        sta       = &wifi_hw.sta_table[ap_idx];
        return ((links_num && sta->is_used) ? sta->sta_idx : -1);
    }

    /* For AP */
    links_num = wifi_hw.vif_table[BL_VIF_AP].links_num;
    if (0 == links_num)
    {
        return -1;
    }

    /* BC/MC */
    if (is_broadcast)
    {
        bcmc_sta_idx = wifi_hw.vif_table[BL_VIF_AP].fixed_sta_idx;
        sta          = &wifi_hw.sta_table[bcmc_sta_idx];
        return (sta->is_used ? sta->sta_idx : -1);
    }

    /* UC */
    for (int i = 0; i < NX_REMOTE_STA_STORE_MAX; i++)
    {
        /* Skip ap_idx and bcmc_sta_idx */
        if (i == bcmc_sta_idx || i == ap_idx)
        {
            continue;
        }

        sta = &wifi_hw.sta_table[i];
        if ((sta->is_used) &&
            (0 == memcmp(&sta->sta_addr, addr, sizeof(struct mac_addr))))
        {
            return i;
        }
    }

    return -1;
}

static int bl_tx_check_ret(uint8_t is_sta, uint8_t is_groupcast, uint32_t value)
{
    /* For handled packet, whether handled OK */
    if (is_sta) {
        if (value & FRAME_SUCCESSFUL_TX_BIT) {
            return 1;
        }
    } else {
        if ((!is_groupcast && (value & FRAME_SUCCESSFUL_TX_BIT)) ||
            (is_groupcast  && (value & DESC_DONE_TX_BIT)))
        {
            return 1;
        }
    }

    return 0;
}

static void bl_tx_push(struct bl_sta* sta, struct txdesc_host *txdesc_host, void *ptxbuf, struct bl_txhdr *txhdr)
{
    struct hostdesc *host = NULL;
    struct ethhdr *ethhdr = NULL;
    uint8_t *block = NULL;
    uint16_t link_offset_len = 0;

    (void)ptxbuf;   /* firmware txbuf pool is disabled (#if 0 in ipc_*); the payload
                     * block itself lives in ram_wifi so the MAC DMA can read it. */

    /* Get hostdesc */
    memset((void*)txdesc_host->pad_txdesc, 0, sizeof(txdesc_host->pad_txdesc));
    host = &(((struct txdesc_upper*)txdesc_host->pad_txdesc)->host);

    /* The tx "packet" is the [HLEN headroom | ethhdr | payload] block from
     * bl602_ll_output (txhdr->p), allocated in the WiFi RAM heap (ram_wifi) so the
     * WiFi MAC TX DMA can fetch the payload -- a pvPortMalloc block in ram_memory
     * DMAs as all-zeros (frame ACKs but the AP drops the zeroed EAPOL). */
    block  = (uint8_t *)txhdr->p;
    ethhdr = (struct ethhdr *)(block + PBUF_LINK_ENCAPSULATION_HLEN);

    /* eth header is skipped in the header */
    link_offset_len = sizeof(struct ethhdr) + PBUF_LINK_ENCAPSULATION_HLEN;
    /* Fill-in the descriptor */
    memcpy(&host->eth_dest_addr, ethhdr->h_dest, ETH_ALEN);
    memcpy(&host->eth_src_addr, ethhdr->h_source, ETH_ALEN);
    host->ethertype   = ethhdr->h_proto;
    host->vif_type    = txhdr->vif_type;
    host->packet_len  = txhdr->len - link_offset_len;
    host->vif_idx     = wifi_hw.vif_table[sta->vif_idx].vif_idx;
    host->staid       = sta->sta_idx;
    host->tid         = (sta->qos ? 0 : 0xFF);
    host->packet_addr = (uint32_t)(0x11111111); // FIXME we use this magic for unvaild packet_addr
    host->flags       = 0;

    /* Single linear segment: payload starts after [HLEN | ethhdr]. */
    host->pbuf_chained_ptr[0] = (uint32_t)(block + link_offset_len);
    host->pbuf_chained_len[0] = txhdr->len - link_offset_len;
    host->status_addr = (uint32_t)(&(txhdr->status));

    /* host_id == the block, freed on txdone via bl_tx_cfm. */
    host->pbuf_addr   = (uint32_t)block;

    ipc_host_txdesc_push(wifi_hw.ipc_env, block);

#ifdef CFG_BL_STATISTIC
    wifi_hw.stats.cfm_balance++;
#endif
}

int bl_tx_cfm(void *pthis, void *host_id)
{
    struct bl_txhdr *txhdr = NULL;
    struct ethhdr     *eth = NULL;
    struct bl_sta     *sta = NULL;
    struct bl_vif     *vif = NULL;
    struct bl_tx_cfm custom_cfm;
    int ret = 0, links_num;
    uint32_t value;

    /* ATTENTION: About return value
     * 1. return ( -1), only for unhandled pakcet (status==0)
     * 2. return (>=0), handled packet, if there is a cb
     *    a. unicast, MUST FRAME_SUCCESSFUL_TX_BIT, return (1), other to c.
     *    b. groupcast, MUST DESC_DONE_TX_BIT, return (1), other to c.
     *    c. return (0)
     */
    /* Host_id is the [HLEN | ethhdr | payload] block from bl602_ll_output (ram_wifi). */
    uint8_t *block = (uint8_t*)host_id;
    txhdr = (struct bl_txhdr*)(block + RWNX_HWTXHDR_ALIGN_PADS((uint32_t)block));
    eth = (struct ethhdr *)(block + PBUF_LINK_ENCAPSULATION_HLEN);

    /* Read status in the TX control header */
    value = txhdr->status.value;
    if (value == 0) {
        bflb_os_printf("[TX] FW return status is NULL!!!\n\r");
    }

    /* Get tx result */
    ret = bl_tx_check_ret(txhdr->vif_type, IS_BC_MC(eth->h_dest[0]), value);

    /* Get sta id and links_num */
    sta = &wifi_hw.sta_table[txhdr->sta_id];
    links_num = wifi_hw.vif_table[sta->vif_idx].links_num;

    do {
        /* Try to repush it, if not handle OK, Check whether
         * can repush it (sta is still valid, ret,...)
         */
        if ((ret) || (txhdr->repush >= 3) || (links_num == 0) || (!sta->is_used))
        {
            break;
        }

        vif = &wifi_hw.vif_table[sta->vif_idx];

        /* If try repush, will cause more pakcet out-of-order but less loss
         * If not,        will cause less pakcet out-of-order but more loss
         */
        if (value & RETRY_LIMIT_REACHED_BIT)
        {
            tx_cntrl_sta_trigger_pending |= BIT_STA(sta->sta_idx);
        } else if (value & FRAME_REPUSHABLE_CHAN_BIT) {
            vif->fc_chan = 1;
        } else if (value & FRAME_REPUSHABLE_PS_BIT) {
            sta->fc_ps = 1;
        } else {
            break;
        }

        txhdr->repush++;
        utils_list_push_back(&sta->pending_list, &(txhdr->item));

        return 0;
    } while (0);

    /* Get customer cb param */
    custom_cfm = txhdr->custom_cfm;

    /* Release the WiFi-RAM block allocated in bl602_ll_output. */
    kfree(block);

#ifdef CFG_NETBUS_WIFI_ENABLE
    update_tx_pbuf_free_cnt_to_scratch_reg();
#endif

    /* Re-trigger this sta tx */
    tx_cntrl_sta_trigger_pending |= BIT_STA(sta->sta_idx);

    /* Notify tx status */
    if (custom_cfm.cb) {
        custom_cfm.cb(custom_cfm.cb_arg, ret > 0);
    }

    return ret;
}

void bl_tx_try_flush(int param, struct ke_tx_fc *tx_fc_field)
{
    struct txdesc_host *txdesc_host = NULL;
    struct bl_txhdr          *txhdr = NULL;
    struct bl_sta              *sta = NULL;
    uint32_t sta_trigger = 0;

    /* Whether need to manipulate flow control */
    if (param && tx_fc_field)
    {
        sta_trigger = bl_tx_cntrl_update_fc(tx_fc_field);
    }

    /* Get trigger flags and clear */
    sta_trigger |= tx_cntrl_sta_trigger_pending;
    tx_cntrl_sta_trigger_pending = 0;
    bflb_os_enter_critical();
    sta_trigger |= tx_cntrl_sta_trigger;
    tx_cntrl_sta_trigger = 0;
    bflb_os_exit_critical();

    // TX: Walking through all STAs
    for (uint8_t i = 0; i < NX_REMOTE_STA_STORE_MAX; i++)
    {
        /* Whether has trigger */
        if (!sta_trigger)
        {
            break;
        }

        /* whether can TX, only check flow control */
        sta = &wifi_hw.sta_table[i];
        if (!((sta_trigger & BIT_STA(i)) && bl_tx_cntrl_check_fc(sta)))
        {
            continue;
        }

        if (!utils_list_is_empty(&sta->pending_list))
        {
            while (1)
            {
                /* Get txdesc_host */
                txdesc_host = (struct txdesc_host *)ipc_host_txdesc_get(wifi_hw.ipc_env);
                if (!txdesc_host)
                {
                    bflb_os_log_warn("[TX] no more txdesc, wait!\n\r");
                    tx_cntrl_sta_trigger_pending |= BIT_STA(i);
                    break;
                }

                /* First get packet from pending list */
                txhdr = (struct bl_txhdr *)utils_list_pop_front(&sta->pending_list);
                if (!txhdr)
                {
                    tx_cntrl_sta_trigger_pending |= BIT_STA(i);
                    break;
                }

                /* Just push */
                bl_tx_push(sta, txdesc_host, (void*)txhdr->p, txhdr);
            }
        }

        if (!utils_list_is_empty(&sta->waiting_list))
        {
            while (1)
            {
                /* Get txdesc_host */
                txdesc_host = (struct txdesc_host *)ipc_host_txdesc_get(wifi_hw.ipc_env);
                if (!txdesc_host)
                {
                    bflb_os_log_warn("[TX] no more txdesc, wait!\n\r");
                    tx_cntrl_sta_trigger_pending |= BIT_STA(i);
                    break;
                }

                /* Get packet (the payload block lives in ram_wifi; no separate
                 * firmware txbuf is used -- that pool is #if 0 in ipc_host). */
                bflb_os_enter_critical();
                txhdr = (struct bl_txhdr *)utils_list_pop_front(&sta->waiting_list);
                bflb_os_exit_critical();
                if (!txhdr)
                {
                    break;
                }

                /* Packet push */
                bl_tx_push(sta, txdesc_host, NULL, txhdr);
            }
        }
    }

    /* Reset trigger flags */
}

/* FreeRTOS-Plus-TCP TX entry (replaces the pbuf-based bl_output). Takes one linear
 * ethernet frame (ethhdr + payload) and copies it into a firmware TX block laid out
 * exactly like the old pbuf did after pbuf_header(HLEN):
 *   [ HLEN(48) headroom: align | bl_txhdr | reserved ] [ ethhdr | payload ]
 * The block replaces the pbuf as the tx "packet"; bl_tx_push reads it as a single
 * segment and bl_tx_cfm vPortFree()s it on txdone. */
int bl602_ll_output(int is_sta, void *frame, uint16_t frame_len, void *cfm)
{
    struct ethhdr   *ethhdr;
    struct bl_txhdr *txhdr;
    struct bl_sta   *sta;
    struct bl_tx_cfm default_cfm = { NULL, NULL };   /* no tx-done cb; block freed in bl_tx_cfm */
    struct bl_tx_cfm *use_cfm = cfm ? (struct bl_tx_cfm *)cfm : &default_cfm;
    int sta_id;
    uint16_t align_offset, link_desc_len;
    uint8_t *block;

    if (!frame || frame_len == 0 || frame_len > WIFI_MTU_SIZE) {
        return -1;
    }

    /* Allocate from the WiFi RAM heap (ram_wifi): the WiFi MAC TX DMA can only
     * fetch the payload from ram_wifi, not from the general ram_memory heap that
     * pvPortMalloc uses -- a ram_memory block DMAs as an all-zero payload (the
     * frame still gets an 802.11 ACK, but the AP drops the zeroed EAPOL and the
     * WPA2 4-way handshake times out). Matches the vendor, whose lwIP PBUF_RAM
     * pool lives in .wifi_ram. */
    block = kmalloc(PBUF_LINK_ENCAPSULATION_HLEN + frame_len, MM_FLAG_HEAP_WRAM_0);
    if (!block) {
        return -1;
    }
    /* frame (ethhdr+payload) sits right after the HLEN headroom */
    memcpy(block + PBUF_LINK_ENCAPSULATION_HLEN, frame, frame_len);
    ethhdr = (struct ethhdr *)(block + PBUF_LINK_ENCAPSULATION_HLEN);

    sta_id = bl_tx_cntrl_get_sta_id(is_sta, IS_BC_MC(ethhdr->h_dest[0]), (struct mac_addr*)ethhdr->h_dest);
    if (sta_id < 0) {
        kfree(block);
        return -1;
    }
    sta = &wifi_hw.sta_table[sta_id];

    /* bl_txhdr sits in the headroom, 8-aligned; must fit in HLEN. */
    align_offset  = RWNX_HWTXHDR_ALIGN_PADS((uint32_t)block);
    link_desc_len = align_offset + sizeof(struct bl_txhdr) + 16;
    if (link_desc_len > PBUF_LINK_ENCAPSULATION_HLEN) {
        kfree(block);
        return -1;
    }
    txhdr = (struct bl_txhdr *)(block + align_offset);
    memset(txhdr, 0, sizeof(struct bl_txhdr));
    memcpy(&txhdr->custom_cfm, use_cfm, sizeof(*use_cfm));
    txhdr->p        = (uint32_t*)block;                        /* the block, freed on txdone */
    txhdr->len      = PBUF_LINK_ENCAPSULATION_HLEN + frame_len; /* mimics old p->tot_len */
    txhdr->vif_type = is_sta;
    txhdr->sta_id   = sta_id;

    bflb_os_enter_critical();
    utils_list_push_back(&sta->waiting_list, &(txhdr->item));
    tx_cntrl_sta_trigger |= BIT_STA(sta_id);
    if (bl_tx_cntrl_check_fc(sta)) {
        bl_irq_handler();
    }
    bflb_os_exit_critical();

    return 0;
}

void bl_tx_cntrl_link_up(struct bl_sta *sta)
{
    /* Check pending_list and waiting_list */
    bl_tx_cntrl_purge_check(sta, 1);
}

void bl_tx_cntrl_link_down(struct bl_sta *sta)
{
    /* Purge pending_list and waiting_list */
    bl_tx_cntrl_purge_check(sta, 0);
}

/**
 * @brief Find STA index by MAC address in the given VIF
 */
int bl_tx_find_sta_by_mac(uint8_t vif_idx, struct mac_addr *mac)
{
    struct bl_sta *sta;
    int i;

    for (i = 0; i < NX_REMOTE_STA_STORE_MAX; i++)
    {
        sta = &wifi_hw.sta_table[i];
        if (sta->is_used && sta->vif_idx == vif_idx &&
            memcmp(&sta->sta_addr, mac, sizeof(struct mac_addr)) == 0)
        {
            return i;
        }
    }
    return -1;
}

#if 0  /* AP intra-BSS forwarding (pbuf-based) — not built in the STA-only FreeRTOS+TCP slice */
/**
 * @brief Forward a unicast frame to a specific STA (intra-BSS forwarding)
 */
err_t bl_tx_intra_bss_forward(struct pbuf *p, int dst_sta_idx)
{
    struct bl_sta *sta;
    struct pbuf *q;
    err_t ret;

    if (dst_sta_idx < 0 || dst_sta_idx >= NX_REMOTE_STA_STORE_MAX)
    {
        return ERR_ARG;
    }

    sta = &wifi_hw.sta_table[dst_sta_idx];
    if (!sta->is_used)
    {
        return ERR_CONN;
    }

    /* Allocate new pbuf with enough headroom for WiFi TX header */
    q = pbuf_alloc(PBUF_RAW, PBUF_LINK_ENCAPSULATION_HLEN + p->tot_len, PBUF_RAM);
    if (!q)
    {
        return ERR_MEM;
    }

    /* Reserve space for WiFi TX header */
    pbuf_header(q, (s16_t)(-PBUF_LINK_ENCAPSULATION_HLEN));
    /* Copy the original data */
    pbuf_copy(q, p);

    /* Use bl_output to send the packet, is_sta=0 for AP mode */
#ifdef CFG_NETBUS_WIFI_ENABLE
    ret = bl_output(&wifi_hw, 0, q, NULL, 1);
#else
    ret = bl_output(&wifi_hw, 0, q, NULL);
#endif
    pbuf_free(q);
    return ret;
}

/**
 * @brief Forward a broadcast/multicast frame to all other STAs (excluding sender)
 */
err_t bl_tx_intra_bss_broadcast(struct pbuf *p, int src_sta_idx)
{
    struct bl_sta *sta;
    struct pbuf *q;
    int i;
    int fwd_count = 0;

    /* Get vif_idx from the source STA's entry in sta_table */
    uint8_t vif_idx = wifi_hw.sta_table[src_sta_idx].vif_idx;

    for (i = 0; i < NX_REMOTE_STA_STORE_MAX; i++)
    {
        /* Skip the source STA */
        if (i == src_sta_idx)
        {
            continue;
        }

        sta = &wifi_hw.sta_table[i];
        /* Only forward to STAs belonging to the same VIF */
        if (sta->is_used && sta->vif_idx == vif_idx)
        {
            if (sta->sta_addr.array[0] == 0 && sta->sta_addr.array[1] == 0 &&
                sta->sta_addr.array[2] == 0 && sta->sta_addr.array[3] == 0 &&
                sta->sta_addr.array[4] == 0 && sta->sta_addr.array[5] == 0)
            {
                continue;
            }

            /* Allocate new pbuf with enough headroom for WiFi TX header */
            q = pbuf_alloc(PBUF_RAW, PBUF_LINK_ENCAPSULATION_HLEN + p->tot_len, PBUF_RAM);
            if (q)
            {
                /* Reserve space for WiFi TX header */
                pbuf_header(q, (s16_t)(-PBUF_LINK_ENCAPSULATION_HLEN));
                /* Copy the original data */
                pbuf_copy(q, p);

#ifdef CFG_NETBUS_WIFI_ENABLE
                (void)bl_output(&wifi_hw, 0, q, NULL, 1);
#else
                (void)bl_output(&wifi_hw, 0, q, NULL);
#endif
                /* bl_output refs the pbuf, we need to free our reference */
                pbuf_free(q);
                fwd_count++;
            }
            else
            {
            }
        }
    }

    return ERR_OK;
}
#endif  /* AP intra-BSS forwarding */
