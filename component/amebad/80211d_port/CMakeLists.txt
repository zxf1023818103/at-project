# component/amebad/80211d_port — AmebaD (RTL8721D KM4) port of the 802.11d country auto-detect
# (component/80211d_config): extended-country scan (lib_wlan records) + channel-plan apply
# (wifi_set_channel_plan_by_country_code, driver reboot) + the WIFI_CHANPLAN provisioning gate
# (strong dot11d_port_provisioned; the config layer's weak default is pulled along with
# dot11d_autodetect_country from the 80211d archive, same-member so no -u anchor needed).
if(NOT "${AMEBAD_CHIP}" STREQUAL "amebad-km4")
    return()
endif()

add_library(80211d_port STATIC dot11d_amebad.c)
target_link_libraries(80211d_port PUBLIC 80211d)          # dot11d.h (the contract)
# wifi_conf.h + the SDK surface it pulls.
target_link_libraries(80211d_port PRIVATE amebad_sdk_includes amebad_wifi_headers amebad_wlan_headers FreeRTOS)
# wifi_util.h (pulled by wifi_conf.h) includes the driver-internal osdep headers (wireless.h);
# amebad_wlan_headers only exposes the wlan root, so add osdep/ directly (same dir the app TU gets).
target_include_directories(80211d_port PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../wlan/osdep")
