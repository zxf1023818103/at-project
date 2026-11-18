add_library(amebagreen2_hci STATIC EXCLUDE_FROM_ALL
    hci_common.c hci_process.c hci_download_v1.c hci_download_v2.c hci_download_v3.c
    hci_controller.c hci_standalone.c hci_h4.c hci_platform.c hci_uart.c
    bt_normal_patch.c bt_mp_patch.c osif.c bt_debug.c rtk_coex.c)
target_include_directories(amebagreen2_hci SYSTEM PUBLIC
    ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(amebagreen2_hci PUBLIC amebagreen2_sdk FreeRTOS)
target_compile_options(amebagreen2_hci PRIVATE -w)
