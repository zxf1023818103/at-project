#ifndef _AMEBAD_WIFI_GET_BEACON_FRAME_H_
#define _AMEBAD_WIFI_GET_BEACON_FRAME_H_

/*
 * Beacon-frame report callback hook owned by the prebuilt WiFi driver lib_wlan.a.
 *
 * Relocated here (the wlan component) off the SDK example
 * component/common/example/get_beacon_frame/example_get_beacon_frame.* so the
 * driver-owned global lives with the lib that defines it:
 *   get_beacon_frame_callback   lib_wlan.a (B) — invoked by the driver with the
 *                               decoded beacon 802.11 MAC header; the example
 *                               assigns it (get_beacon_frame_callback = cb) to
 *                               receive a copy of each connected-AP beacon.
 *
 * BEACON_INFO_T is the by-value argument the driver passes (self-contained, no
 * external types). No consumer in the minimal driver-only image yet (the
 * get-beacon-frame example is not built); this is the declaration home for when
 * it is wired.
 */

typedef struct beacon_info_str {
	/* 802.11 MAC header */
	unsigned char frame_ctl[2];
	unsigned char duration_id[2];
	unsigned char addr1[6];
	unsigned char addr2[6];
	unsigned char addr3[6];
	unsigned char seq_ctl[2];
	/* 802.11 beacon IE */
	unsigned char timestamp[8];
} BEACON_INFO_T;

typedef int (*get_beacon_frame_func_ptr)(BEACON_INFO_T beacon_frame);

extern get_beacon_frame_func_ptr get_beacon_frame_callback;   /* lib_wlan.a */

#endif /* _AMEBAD_WIFI_GET_BEACON_FRAME_H_ */
