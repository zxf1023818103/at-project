# Build-local linker view: keep the pinned SDK and shared protocol sources intact.
# The measured hot path is RX -> IP/TCP -> stream/allocator, plus USB/RTOS wakeup.
if(CONFIG_PSRAM AND NOT CONFIG_NAN_SRAM_HOTCODE)
    return()
endif()
if(NOT CHIP STREQUAL "bl618dg" OR NOT CPU_ID STREQUAL "ap" OR
   NOT CPU_MODEL STREQUAL "b0" OR NOT CONFIG_DUALCORE_DISABLE)
    message(FATAL_ERROR "NAN SRAM layout requires BL618DG B0 AP with NP disabled")
endif()
if(CONFIG_PSRAM_COPY_CODE OR CONFIG_PSRAM_XIP)
    message(FATAL_ERROR "NAN SRAM layout requires PSRAM code copying/XIP disabled")
endif()
get_property(nan_linker_source GLOBAL PROPERTY LINKER_SCRIPT_IN)
file(READ "${nan_linker_source}" nan_linker)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${nan_linker_source}")

macro(nan_linker_replace needle replacement)
    string(FIND "${nan_linker}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "SDK linker layout changed: missing ${needle}")
    endif()
    string(REPLACE "${needle}" "${replacement}" nan_linker "${nan_linker}")
endmacro()

if(NOT CONFIG_PSRAM)
    # Local radio/NP is off, so WRAM is exclusively CPU-owned packet storage.
    # Its documented cacheable alias avoids uncached metadata/stream accesses.
    # USB DMA buffers and EHCI descriptors remain in their original nocache OCRAM.
    nan_linker_replace("#define CONFIG_WRAM_ADDRESS 0x21070000"
                       "#define CONFIG_WRAM_ADDRESS 0xA1070000")
    string(APPEND nan_linker "\nASSERT(SIZEOF(psram_data) == 0 && SIZEOF(psram_bss) == 0 && SIZEOF(psram_noinit) == 0, \"NAN SRAM-only image contains PSRAM data\")\n")
endif()

if(CONFIG_NAN_SRAM_HOTCODE)
    # Object selection also includes local static helpers. --gc-sections still
    # drops unused functions; authentication/crypto and formatting stay in Flash.
    set(nan_hot_objects
        "*libfreertos.a:*.o*"
        "*libfreertos_plus_tcp.a:FreeRTOS_IP*.o*"
        "*libfreertos_plus_tcp.a:FreeRTOS_TCP*.o*"
        "*libfreertos_plus_tcp.a:FreeRTOS_Sockets*.o*"
        "*libfreertos_plus_tcp.a:FreeRTOS_Routing*.o*"
        "*libfreertos_plus_tcp.a:FreeRTOS_Stream_Buffer*.o*"
        "*libfreertos_plus_tcp.a:FreeRTOS_ND*.o*"
        "*libfreertos_plus_tcp_port.a:BufferAllocation_2*.o*"
        "*libmm.a:*.o*"
        "*libcherryusb.a:*.o*"
        "*libwhc_usb.a:*.o*"
        "*libnan_usb.a:*.o*"
        "*libbl618dg_nan.a:nan_network*.o*"
        "*libbl618dg_nan.a:nan_tcp_protocol*.o*"
        "*liblibc.a:lib_vikmemcpy*.o*"
        "*liblibc.a:lib_memset*.o*"
        "*liblibc.a:lib_memcmp*.o*"
        "*liblibc.a:lib_memmove*.o*"
        "*nan_port.c.o*" "*nan_native_port.c.o*" "*nan_heap.c.o*"
        "*nan_rx_perf.c.o*")
    set(hot_text "        __nan_hot_text_start__ = .;\n")
    set(hot_rodata "        __nan_hot_rodata_start__ = .;\n")
    foreach(object IN LISTS nan_hot_objects)
        string(APPEND hot_text "        ${object}(.text*)\n")
        string(APPEND hot_rodata "        ${object}(.rodata* .srodata*)\n")
    endforeach()
    string(APPEND hot_text "        __nan_hot_text_end__ = .;\n")
    string(APPEND hot_rodata "        __nan_hot_rodata_end__ = .;\n")
    nan_linker_replace("SECTION_LOAD_PROLOGUE(tcm_text, ,)\n    {"
        "SECTION_LOAD_PROLOGUE(tcm_text, ,)\n    {\n${hot_text}")
    nan_linker_replace("SECTION_LOAD_PROLOGUE(tcm_rodata, ,)\n    {"
        "SECTION_LOAD_PROLOGUE(tcm_rodata, ,)\n    {\n${hot_rodata}")
endif()
set(nan_linker_view "${CMAKE_BINARY_DIR}/generated/nan_memory.ld.in")
file(CONFIGURE OUTPUT "${nan_linker_view}" CONTENT "${nan_linker}" @ONLY)
sdk_set_linker_script_macro("${nan_linker_view}")
