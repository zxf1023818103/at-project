# component/80211d_config — cross-platform 802.11d country-code auto-detect (config layer):
# gate + Country-IE tally/majority vote + WIFI_COUNTRY persistence (dot11d.c). The driver-specific
# scan + apply live in each platform's 80211d_port (target 80211d_port), which implements the
# dot11d_port_* contract in dot11d.h. Target name `80211d` follows the dir convention
# (xxx_config -> target xxx); the C symbols use the dot11d_ prefix (identifiers can't start
# with a digit).
add_library(80211d STATIC EXCLUDE_FROM_ALL dot11d.c dot11d_tally.c)   # tally/valid split out: see dot11d_tally.c
target_include_directories(80211d PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
