option(AT_ENABLE_TFM "Build KM4TZ as TF-M nonsecure firmware" OFF)
option(AT_ENABLE_STORAGE "Enable TF-M ITS and protected storage" OFF)
# Use the kernel's next deadline for the AP PMC timer. The SDK's alternative
# policy defaults to sleeping forever, which also stops the NP watchdog task.
add_compile_definitions(CONFIG_STANDARD_TICKLESS)
if(AT_ENABLE_AWS OR AT_ENABLE_MINIMAL)
    # Match AmebaD's MQTT profile; throughput-sized socket buffers otherwise
    # consume the heap needed by the OTA worker when all services are enabled.
    add_compile_definitions(AT_NET_LOW_MEM)
endif()
if(AT_ENABLE_STORAGE AND NOT AT_ENABLE_TFM)
    message(FATAL_ERROR "STORAGE requires TFM")
endif()
if(AT_ENABLE_TFM AND AMEBAGREEN2_CORE STREQUAL "km4tz")
    add_compile_definitions(AT_ENABLE_TFM CONFIG_TRUSTZONE CONFIG_TRUSTZONE_EN=1
        CONFIG_IMG3_SRAM CONFIG_TZ_S_SIZE=64)
    set(AMEBAGREEN2_LD_DEFINES -DCONFIG_TRUSTZONE -DCONFIG_TRUSTZONE_EN=1
        -DCONFIG_IMG3_SRAM -DCONFIG_TZ_S_SIZE=64)
    if(AMEBAGREEN2_IMAGE STREQUAL "app_km4tz")
        # Only SPE may emit secure entry veneers; image2 executes in NS state.
        foreach(lang C CXX ASM EXE_LINKER)
            string(REPLACE " -mcmse" "" CMAKE_${lang}_FLAGS "${CMAKE_${lang}_FLAGS}")
        endforeach()
    endif()
endif()
