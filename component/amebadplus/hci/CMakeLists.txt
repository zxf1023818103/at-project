set(bt ${AMEBADPLUS_SDK}/component/bluetooth)
set(driver ${bt}/driver)
add_library(amebadplus_hci STATIC EXCLUDE_FROM_ALL
    ${driver}/hci/hci_common.c
    ${driver}/hci/hci_process/hci_process.c
    ${driver}/hci/hci_process/hci_download_v1.c
    ${driver}/hci/hci_process/hci_download_v2.c
    ${driver}/hci/hci_process/hci_download_v3.c
    ${driver}/hci/hci_process/hci_controller.c
    ${driver}/hci/hci_process/hci_standalone.c
    ${driver}/hci/hci_transport/hci_h4.c
    ${driver}/platform/amebadplus/hci/hci_platform.c
    ${driver}/platform/amebadplus/hci/hci_uart.c
    ${driver}/platform/amebadplus/hci/bt_normal_patch.c
    ${driver}/platform/amebadplus/hci/bt_mp_patch.c
    ${bt}/osif/osif.c ${bt}/osif/bt_debug.c
    ${bt}/rtk_coex/rtk_coex.c)
target_include_directories(amebadplus_hci SYSTEM PUBLIC
    ${driver}/inc ${driver}/inc/hci ${driver}/platform/amebadplus/inc
    ${bt}/osif ${bt}/rtk_coex ${bt}/api/include)
target_link_libraries(amebadplus_hci PUBLIC amebadplus_sdk FreeRTOS)
target_compile_options(amebadplus_hci PRIVATE -w)
