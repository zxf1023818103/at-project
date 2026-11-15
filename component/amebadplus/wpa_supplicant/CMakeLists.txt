# wpa_supplicant: local AmebaDplus files, grouped like component/amebad.
add_library(amebadplus_wpa_supplicant_headers INTERFACE)
target_include_directories(amebadplus_wpa_supplicant_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${CMAKE_CURRENT_LIST_DIR}/wpa_supplicant"
    "${CMAKE_CURRENT_LIST_DIR}/src/crypto"
    "${CMAKE_CURRENT_LIST_DIR}/src/wps"
    "${CMAKE_CURRENT_LIST_DIR}/wpa_lite"
    "${CMAKE_CURRENT_LIST_DIR}/wpa_lite/rom")

if(AMEBADPLUS_CORE STREQUAL "km4")
    foreach(lib wpa_lite wps eap p2p)
        add_library(amebadplus_${lib} STATIC IMPORTED GLOBAL)
        set_target_properties(amebadplus_${lib} PROPERTIES
            IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/lib_${lib}.a")
    endforeach()
endif()
