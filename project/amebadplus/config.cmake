option(AT_ENABLE_TFM "Build KM4 as TF-M nonsecure firmware" OFF)
option(AT_ENABLE_STORAGE "Enable TF-M ITS and protected storage" OFF)
if(AT_ENABLE_AWS OR AT_ENABLE_MINIMAL)
    # Match AmebaD's MQTT profile; throughput-sized socket buffers otherwise
    # consume the heap needed by the OTA worker when all services are enabled.
    add_compile_definitions(AT_NET_LOW_MEM)
endif()
if(AT_ENABLE_STORAGE AND NOT AT_ENABLE_TFM)
    message(FATAL_ERROR "STORAGE requires TFM")
endif()
if(AT_ENABLE_TFM AND AMEBADPLUS_CORE STREQUAL "km4")
    add_compile_definitions(AT_ENABLE_TFM CONFIG_TRUSTZONE CONFIG_TRUSTZONE_EN=1
        CONFIG_IMG3_SRAM CONFIG_TZ_S_SIZE=44)
    set(AMEBADPLUS_LD_DEFINES -DCONFIG_TRUSTZONE -DCONFIG_TRUSTZONE_EN=1
        -DCONFIG_IMG3_SRAM -DCONFIG_TZ_S_SIZE=44)
endif()
