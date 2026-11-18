if(AT_NAN_USB_BRIDGE)
    set(cherry ${AT_ROOT}/component/cherryusb_config/cherryusb)
    add_library(green2_usb_port STATIC usb_green2.c ${cherry}/osal/usb_osal_freertos.c
        ${cherry}/core/usbd_core.c ${cherry}/port/dwc2/usb_dc_dwc2.c)
    target_include_directories(green2_usb_port PUBLIC . ${cherry}/common ${cherry}/core
        ${cherry}/class/hub ${cherry}/port/dwc2
        PRIVATE ${AT_ROOT}/component/cherryusb_config/benchmark)
    target_compile_definitions(green2_usb_port PRIVATE USB_BENCH_HOST=0)
    target_link_libraries(green2_usb_port PUBLIC amebagreen2_sdk FreeRTOS freertos_config
        PRIVATE monitor ${CMAKE_CURRENT_LIST_DIR}/lib_usb_hal.a)
    add_subdirectory(${AT_ROOT}/component/cherryusb_config/nan_usb ${CMAKE_CURRENT_BINARY_DIR}/nan_usb)
    target_compile_definitions(nan_usb PUBLIC NAN_USB_HOST=0)
    target_link_libraries(nan_usb PRIVATE green2_usb_port)
    return()
endif()
set(cherry ${AT_ROOT}/component/cherryusb_config/cherryusb)
set(bench ${AT_ROOT}/component/cherryusb_config/benchmark)
add_library(green2_usb_bench STATIC usb_green2.c ${bench}/usb_bench.c
    ${cherry}/osal/usb_osal_freertos.c)
target_include_directories(green2_usb_bench PRIVATE . ${bench} ${cherry}/common ${cherry}/core
    ${cherry}/class/hub ${cherry}/port/dwc2)
target_link_libraries(green2_usb_bench PUBLIC amebagreen2_sdk FreeRTOS freertos_config
    PRIVATE at_console monitor ${CMAKE_CURRENT_LIST_DIR}/lib_usb_hal.a)
target_compile_definitions(green2_usb_bench PRIVATE USB_BENCH_BOARD="amebagreen2")
if(AT_USB_BENCH_ROLE STREQUAL "host")
    target_compile_definitions(green2_usb_bench PRIVATE USB_BENCH_HOST=1)
    target_sources(green2_usb_bench PRIVATE ${cherry}/core/usbh_core.c
        ${cherry}/class/hub/usbh_hub.c ${cherry}/port/dwc2/usb_hc_dwc2.c)
else()
    target_compile_definitions(green2_usb_bench PRIVATE USB_BENCH_HOST=0)
    target_sources(green2_usb_bench PRIVATE ${cherry}/core/usbd_core.c ${cherry}/port/dwc2/usb_dc_dwc2.c)
endif()
target_compile_options(green2_usb_bench PRIVATE -O2)
