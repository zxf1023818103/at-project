add_library(nan_usb STATIC nan_usb.c nan_usb_wire.c)
target_include_directories(nan_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nan_usb PRIVATE FreeRTOS)
target_compile_options(nan_usb PRIVATE -O2)
