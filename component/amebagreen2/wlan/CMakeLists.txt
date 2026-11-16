# wlan: local AmebaGreen2 files, grouped like component/amebad.
add_library(amebagreen2_wlan_headers INTERFACE)
target_include_directories(amebagreen2_wlan_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

if(AMEBAGREEN2_CORE STREQUAL "km4ns")
    set(_wlan_libs wifi_whc_np wifi_common coex)
else()
    set(_wlan_libs wifi_whc_ap wifi_common wifi_rtk_app)
endif()
foreach(lib IN LISTS _wlan_libs)
    add_library(amebagreen2_${lib} STATIC IMPORTED GLOBAL)
    if(lib STREQUAL "wifi_common")
        set(_file lib_wifi_common_${AMEBAGREEN2_CORE}.a)
    else()
        set(_file lib_${lib}.a)
    endif()
    set_target_properties(amebagreen2_${lib} PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/${_file}")
endforeach()
