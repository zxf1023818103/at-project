//----------------------------------------------------------------------------//
#include "wifi/wifi_ind.h"
#include "wifi/wifi_conf.h"
#include "osdep_service.h"
#include "platform_stdlib_rtl8721d.h"
#include "wifi_fast_connect.h"    /* wlan_init_done_ptr / p_wlan_init_done_callback (lib_wlan.a) */

/******************************************************
 *                    Constants
 ******************************************************/

#define WIFI_INDICATE_MSG	0
#define WIFI_MANAGER_STACKSIZE	1300
#define WIFI_MANAGER_PRIORITY		(0) //Actual priority is 4 since calling rtw_create_task
#define WIFI_MANAGER_Q_SZ	8

#define WIFI_EVENT_MAX_ROW	3
/******************************************************
 *                    Globals
 ******************************************************/

static event_list_elem_t     event_callback_list[WIFI_EVENT_MAX][WIFI_EVENT_MAX_ROW];
#if CONFIG_WIFI_IND_USE_THREAD
static rtw_worker_thread_t          wifi_worker_thread;
#endif
/* Shared EventGroup: wifi_indication() sets bit (1<<event); tasks block on it via wifi_event_wait().
 * Created once at wifi init (init_event_callback_list / wifi_manager_init), both single-threaded. */
static EventGroupHandle_t    wifi_event_group = NULL;

//----------------------------------------------------------------------------//
#if CONFIG_WIFI_IND_USE_THREAD
static rtw_result_t rtw_send_event_to_worker(int event_cmd, char *buf, int buf_len, int flags)
{
	rtw_event_message_t message;
	int i;
	rtw_result_t ret = RTW_SUCCESS;
	char *local_buf = NULL;
	
	if(event_cmd >= WIFI_EVENT_MAX)
		return RTW_BADARG;
	
	for(i = 0; i < WIFI_EVENT_MAX_ROW; i++){
		if(event_callback_list[event_cmd][i].handler == NULL)
			continue;

		message.function = (event_handler_t)event_callback_list[event_cmd][i].handler;
		message.buf_len = buf_len;
		if(buf_len){
			local_buf = (char*)pvPortMalloc(buf_len);
			if(local_buf == NULL)
				return RTW_NOMEM;
			memcpy(local_buf, buf, buf_len);
			//printf("\n!!!!!Allocate %p(%d) for evcmd %d\n", local_buf, buf_len, event_cmd);
		}
		message.buf = local_buf;
		message.flags = flags;
		message.user_data = event_callback_list[event_cmd][i].handler_user_data;

		ret = rtw_push_to_xqueue(&wifi_worker_thread.event_queue, &message, 0);
		if(ret != RTW_SUCCESS){
			if(local_buf){
				printf("\r\nrtw_send_event_to_worker: enqueue cmd %d failed and free %p(%d)\n", event_cmd, local_buf, buf_len);
				vPortFree(local_buf);
			}
			break;
		}
	}
	return ret;
}
#else
static rtw_result_t rtw_indicate_event_handle(int event_cmd, char *buf, int buf_len, int flags)
{
	rtw_event_handler_t handle = NULL;
	int i;
	
	if(event_cmd >= WIFI_EVENT_MAX)
		return (rtw_result_t)RTW_BADARG;
	
	for(i = 0; i < WIFI_EVENT_MAX_ROW; i++){
		handle = event_callback_list[event_cmd][i].handler;
		if(handle == NULL)
			continue;
		handle(buf, buf_len, flags, event_callback_list[event_cmd][i].handler_user_data);
	}

	return RTW_SUCCESS;
}
#endif

void wifi_indication( rtw_event_indicate_t event, char *buf, int buf_len, int flags)
{
	//
	// If upper layer application triggers additional operations on receiving of wext_wlan_indicate,
	// 		please strictly check current stack size usage (by using uxTaskGetStackHighWaterMark() ) 
	//		, and tries not to share the same stack with wlan driver if remaining stack space is 
	//		not available for the following operations. 
	//		ex: using semaphore to notice another thread.
	switch(event)
	{
		case WIFI_EVENT_DISCONNECT:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r %s():Disconnection indication received", __FUNCTION__);
#endif
			break;
		case WIFI_EVENT_CONNECT:
			// For WPA/WPA2 mode, indication of connection does not mean data can be  
			// 		correctly transmitted or received. Data can be correctly transmitted or
			// 		received only when 4-way handshake is done. 
			// Please check WIFI_EVENT_FOURWAY_HANDSHAKE_DONE event
#if(WIFI_INDICATE_MSG==1)						
			// Sample: return mac address
			if(buf != NULL && buf_len == 6)
			{
				printf("\n\r%s():Connect indication received: %02x:%02x:%02x:%02x:%02x:%02x", __FUNCTION__, 
							buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
			}
#endif
			break;
		case WIFI_EVENT_FOURWAY_HANDSHAKE_DONE:
#if(WIFI_INDICATE_MSG==1)			
			if(buf != NULL)			
			{
				if(buf_len == strlen(IW_EXT_STR_FOURWAY_DONE))
					printf("\n\r%s():%s", __FUNCTION__, buf);
			}
#endif
			break;
		case WIFI_EVENT_SCAN_RESULT_REPORT:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_SCAN_RESULT_REPORT\n", __func__);
#endif
			break;
		case WIFI_EVENT_SCAN_DONE:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_SCAN_DONE\n", __func__);
#endif
			break;
		case WIFI_EVENT_RECONNECTION_FAIL:
#if(WIFI_INDICATE_MSG==1)			
			if(buf != NULL){
				if(buf_len == strlen(IW_EXT_STR_RECONNECTION_FAIL))
					printf("\n\r%s", buf);
			}
#endif
			break;
		case WIFI_EVENT_NO_NETWORK:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_NO_NETWORK\n", __func__);
#endif
			break;
		case WIFI_EVENT_RX_MGNT:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_RX_MGNT\n", __func__);
#endif
			break;
#if CONFIG_ENABLE_P2P
		case WIFI_EVENT_SEND_ACTION_DONE:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_SEND_ACTION_DONE\n", __func__);
#endif
			break;
#endif //CONFIG_ENABLE_P2P
		case WIFI_EVENT_STA_ASSOC:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_STA_ASSOC\n", __func__);
#endif
			break;
		case WIFI_EVENT_STA_DISASSOC:
#if(WIFI_INDICATE_MSG==1)			
			printf("\n\r%s(): WIFI_EVENT_STA_DISASSOC\n", __func__);
#endif
			break;
#ifdef CONFIG_WPS
		case WIFI_EVENT_STA_WPS_START:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_STA_WPS_START\n", __func__);
#endif
			break;
		case WIFI_EVENT_WPS_FINISH:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_WPS_FINISH\n", __func__);
#endif
			break;
		case WIFI_EVENT_EAPOL_RECVD:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_EAPOL_RECVD\n", __func__);
#endif
			break;
#endif
		case WIFI_EVENT_BEACON_AFTER_DHCP:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_BEACON_AFTER_DHCP\n", __func__);
#endif
			break;
		case WIFI_EVENT_IP_CHANGED:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_IP_CHANNGED\n", __func__);
#endif
			break;
		case WIFI_EVENT_ICV_ERROR:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_ICV_ERROR\n", __func__);
#endif
		case WIFI_EVENT_CHALLENGE_FAIL:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_CHALLENGE_FAIL\n", __func__);
#endif
			break;
		case WIFI_EVENT_SOFTAP_START:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_SOFTAP_START\n", __func__);
#endif
			break;
		case WIFI_EVENT_SOFTAP_STOP:
#if(WIFI_INDICATE_MSG==1)
			printf("\n\r%s(): WIFI_EVENT_SOFTAP_STOP\n", __func__);
#endif
			break;

	}

#if CONFIG_INIC_EN
	inic_indicate_event(event, buf, buf_len, flags);
#endif//CONFIG_INIC_EN

#if CONFIG_WIFI_IND_USE_THREAD
	rtw_send_event_to_worker(event, buf, buf_len, flags);
#else
	rtw_indicate_event_handle(event, buf, buf_len, flags);
#endif

	/* Multi-task notification: wake any task blocked on this event in wifi_event_wait(). Called from
	 * the driver's thread context (the sync/worker paths above assume the same), so the task-level set
	 * is correct - no FromISR needed. */
	if(wifi_event_group != NULL && event < WIFI_EVENT_MAX)
		xEventGroupSetBits(wifi_event_group, WIFI_EVENT_BIT(event));
}

void wifi_reg_event_handler(unsigned int event_cmds, rtw_event_handler_t handler_func, void *handler_user_data)
{
	int i = 0, j = 0;
	if(event_cmds < WIFI_EVENT_MAX){
		for(i=0; i < WIFI_EVENT_MAX_ROW; i++){
			if(event_callback_list[event_cmds][i].handler == NULL){
			    for(j=0; j<WIFI_EVENT_MAX_ROW; j++){
			        if(event_callback_list[event_cmds][j].handler == handler_func){
			            return;
			        }
			    }
				event_callback_list[event_cmds][i].handler = handler_func;
				event_callback_list[event_cmds][i].handler_user_data = handler_user_data;
				return;
			}
		}
	}
}

void wifi_unreg_event_handler(unsigned int event_cmds, rtw_event_handler_t handler_func)
{
	int i;
	if(event_cmds < WIFI_EVENT_MAX){
		for(i = 0; i < WIFI_EVENT_MAX_ROW; i++){
			if(event_callback_list[event_cmds][i].handler == handler_func){
				event_callback_list[event_cmds][i].handler = NULL;
				event_callback_list[event_cmds][i].handler_user_data = NULL;
				return;
			}
		}
	}
}

void init_event_callback_list(void){
	memset(event_callback_list, 0, sizeof(event_callback_list));
	if(wifi_event_group == NULL)
		wifi_event_group = xEventGroupCreate();
}

/* ---- EventGroup multi-task notification API (see wifi_ind.h) ---- */
EventGroupHandle_t wifi_event_group_handle(void){
	return wifi_event_group;
}

EventBits_t wifi_event_wait(EventBits_t bits, BaseType_t clearOnExit, BaseType_t waitForAll, uint32_t timeout_ms){
	if(wifi_event_group == NULL)
		return 0;
	return xEventGroupWaitBits(wifi_event_group, bits, clearOnExit, waitForAll,
				(timeout_ms == 0xFFFFFFFFu) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
}

EventBits_t wifi_event_clear(EventBits_t bits){
	return (wifi_event_group != NULL) ? xEventGroupClearBits(wifi_event_group, bits) : 0;
}

EventBits_t wifi_event_get(void){
	return (wifi_event_group != NULL) ? xEventGroupGetBits(wifi_event_group) : 0;
}

/* ---- WLAN init-done over the EventGroup (replaces wifi_conf.c's g_wifi_init_done_sema) ----
 * The prebuilt driver invokes p_wlan_init_done_callback once WLAN init completes; wifi_on() blocks on
 * the WIFI_INIT_DONE_BIT instead of polling rltk_wlan_running() at 1 Hz, saving up to ~1 s of bring-up
 * latency. The bit is cleared on arm, so the early-return paths in wifi_on() need no cleanup. */
static int wifi_init_done_cb(void){
	if(wifi_event_group != NULL)
		xEventGroupSetBits(wifi_event_group, WIFI_INIT_DONE_BIT);
	return 0;
}

void wifi_arm_init_done(void){
	/* Clear any stale signal, then install the driver callback before driver init is kicked off so a
	 * fast init cannot complete before we are listening. */
	if(wifi_event_group != NULL)
		(void)xEventGroupClearBits(wifi_event_group, WIFI_INIT_DONE_BIT);
	p_wlan_init_done_callback = wifi_init_done_cb;
}

int wifi_wait_init_done(uint32_t timeout_ms){
	return (wifi_event_wait(WIFI_INIT_DONE_BIT, pdTRUE, pdFALSE, timeout_ms) & WIFI_INIT_DONE_BIT) ? 1 : 0;
}

int wifi_manager_init(void)
{
	if(wifi_event_group == NULL)
		wifi_event_group = xEventGroupCreate();
#if CONFIG_WIFI_IND_USE_THREAD
	rtw_create_worker_thread(&wifi_worker_thread,
							WIFI_MANAGER_PRIORITY,
							WIFI_MANAGER_STACKSIZE,
							WIFI_MANAGER_Q_SZ);
#endif
	return 0;
}

void rtw_wifi_manager_deinit(void)
{
#if CONFIG_WIFI_IND_USE_THREAD
	rtw_delete_worker_thread(&wifi_worker_thread);
#endif
}

