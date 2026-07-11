# component/bouffalo_sdk/80211d_port — bl602/BL702 (wifi4) port of the 802.11d country
# auto-detect (component/80211d_config): broadcast wifi_mgmr scan + Country-IE tally feed +
# wifi_mgmr_set_country_code apply. bl602-only: the wl80211 chips (bl616/cl) don't carry the
# Country IE in their scan records (yet).
if(NOT "${BOUFFALO_CHIP}" STREQUAL "bl602")
    return()
endif()

add_library(80211d_port STATIC dot11d_bl602.c)
target_link_libraries(80211d_port PUBLIC 80211d)                            # dot11d.h (the contract)
target_link_libraries(80211d_port PRIVATE FreeRTOS wifi4_internal_headers)  # wifi_mgmr_ext.h + its deps
