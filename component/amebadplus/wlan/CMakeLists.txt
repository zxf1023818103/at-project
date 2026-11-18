# wlan: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_wlan_headers INTERFACE)
target_include_directories(amebadplus_wlan_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}")

if(AMEBADPLUS_CORE STREQUAL "km0")
    set(_wlan_libs wifi_whc_np wifi_common coex)
else()
    set(_wlan_libs wifi_whc_ap wifi_com_sec wifi_rtk_app)
endif()
foreach(lib IN LISTS _wlan_libs)
    add_library(amebadplus_${lib} STATIC IMPORTED GLOBAL)
    set_target_properties(amebadplus_${lib} PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_${lib}.a")
endforeach()
