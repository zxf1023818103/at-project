/* The shared wifi_service owns credentials and reconnects. Disable the vendor
 * persistence/autoreconnect entry points that the closed wifi_on() calls. */
#include "wifi_api_types.h"
#include "os_wrapper.h"
/* The closed API archive references the vendor reconnect record even when
 * CONFIG_AUTO_RECONNECT is disabled. Expose its ABI without enabling the task. */
#undef CONFIG_AUTO_RECONNECT
#define CONFIG_AUTO_RECONNECT 1
#include "wifi_auto_reconnect.h"
/* lib_wifi_whc_ap records the selected EAP method here. Reconnect remains
 * disabled; the shared service / provisioning task own association policy. */
struct rtw_auto_reconn_t rtw_reconn;
extern int (*p_wifi_do_fast_connect)(void);
extern int (*p_store_fast_connect_info)(unsigned int, unsigned int);
void wifi_fast_connect_enable(u8 enable) { (void)enable; p_wifi_do_fast_connect = 0; p_store_fast_connect_info = 0; }
s32 wifi_set_autoreconnect(u8 enable) { (void)enable; return RTK_SUCCESS; }
void rtw_reconn_new_conn(struct rtw_network_info *info) { (void)info; }
int wifi_stop_autoreconnect(void) { return RTK_SUCCESS; }
