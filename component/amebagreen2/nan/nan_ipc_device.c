/* The SDK gates these vendor events on its Linux cfg80211 flag. An on-chip
 * FreeRTOS host consumes the same payloads, without enabling Linux SME mode. */
#include "rtw_inic_common.h"
void __wrap_whc_ipc_dev_cfg80211_nan_cfgvendor_event_report(u16 id, void *data, u32 len)
{
    u32 args[3] = {id, (u32)data, len};
    if (data && len) DCache_Clean((u32)data, len);
    whc_ipc_dev_api_message_send(WHC_API_CFG80211_NAN_CFGVENDOR_EVENT, args, 3);
}
void __wrap_whc_ipc_dev_cfg80211_cfgvendor_send_cmd_reply(void *data, u32 len)
{
    u32 args[2] = {(u32)data, len};
    if (data && len) DCache_Clean((u32)data, len);
    whc_ipc_dev_api_message_send(WHC_API_CFG80211_NAN_CFGVENDOR_CMD_REPLY, args, 2);
}
