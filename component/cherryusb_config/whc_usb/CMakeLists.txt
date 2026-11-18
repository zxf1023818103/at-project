add_library(whc_usb STATIC whc_usb.c whc_wire.c)
target_include_directories(whc_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(whc_usb PRIVATE FreeRTOS sdk_intf_lib)
target_compile_options(whc_usb PRIVATE -O2)
