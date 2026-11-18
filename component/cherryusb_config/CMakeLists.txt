# SDK owns CherryUSB core, OSAL and Bouffalo USB DMA drivers. Add only the
# missing CDC-NCM device class and our FreeRTOS+TCP transport.
add_library(cherryusb_ncm STATIC upstream/usbd_cdc_ncm.c ncm_tcp.c)
target_include_directories(cherryusb_ncm PUBLIC . upstream)
target_link_libraries(cherryusb_ncm PUBLIC freertos_plus_tcp PRIVATE sdk_intf_lib)
