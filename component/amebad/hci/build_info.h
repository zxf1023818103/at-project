/* build_info.h — minimal shim for the vendored BT source (hci_board.c uses only
 * UTS_VERSION, a build-date string for a banner print). The SDK normally
 * generates this per build under project/.../inc/inc_hp/; we don't need a real
 * timestamp for the driver-only DTM image, so a static string suffices. */
#ifndef _AT_PROJECT_BT_BUILD_INFO_H_
#define _AT_PROJECT_BT_BUILD_INFO_H_

#define UTS_VERSION          "at-project-bt-dtm"
#define RTL_FW_COMPILE_TIME  "at-project-bt-dtm"
#define RTL_FW_COMPILE_DATE  "at-project-bt-dtm"
#define RTL_FW_COMPILE_BY    "at-project"
#define RTL_FW_COMPILE_HOST  ""
#define RTL_FW_COMPILE_DOMAIN ""
#define RTL_FW_COMPILER      "arm-none-eabi-gcc"

#endif /* _AT_PROJECT_BT_BUILD_INFO_H_ */
