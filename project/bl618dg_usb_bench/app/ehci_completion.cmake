# COM37's BL618DG can complete a qTD with no USBINT left pending. Check the
# descriptor after each 1 ms semaphore wait, without submitting the data twice.
# Generate a fixture-local driver so the pinned SDK remains unchanged.
set(ehci ${BL_SDK_BASE}/components/usb/cherryusb/port/ehci)
set(ehci_input ${ehci}/usb_hc_ehci.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ehci_input})
file(READ ${ehci_input} ehci_source)
set(wait_original "ret = usb_osal_sem_take(qh->waitsem, urb->timeout);")
string(REGEX MATCHALL "ret = usb_osal_sem_take\\(qh->waitsem, urb->timeout\\)" wait_matches "${ehci_source}")
list(LENGTH wait_matches wait_count)
if(NOT wait_count EQUAL 1)
    message(FATAL_ERROR "EHCI source changed: review the BL618DG completion workaround")
endif()
set(wait_replacement [=[/* Preserve IRQ completion; recover a missing notification from the qTD.
         * Polling and scheduling overhead remain inside the benchmark timer. */
        uint32_t remaining = urb->timeout;
        do {
            ret = usb_osal_sem_take(qh->waitsem, 1);
            if (ret == 0) break;
            flags = usb_osal_enter_critical_section();
            __asm volatile("fence iorw, iorw" ::: "memory");
            unsigned ep_type = USB_GET_ENDPOINT_TYPE(urb->ep->bmAttributes);
            if (urb->errorcode == -USB_ERR_BUSY &&
                (ep_type == USB_ENDPOINT_TYPE_CONTROL || ep_type == USB_ENDPOINT_TYPE_BULK)) {
                ehci_check_qh(bus, &g_async_qh_head[bus->hcd.hcd_id], qh);
            }
            usb_osal_leave_critical_section(flags);
        } while (--remaining);
        if (ret < 0) ret = usb_osal_sem_take(qh->waitsem, 0);]=])
string(REPLACE "${wait_original}" "${wait_replacement}" ehci_source "${ehci_source}")
set(ehci_output ${CMAKE_CURRENT_BINARY_DIR}/usb_hc_ehci_bench.c)
file(CONFIGURE OUTPUT ${ehci_output} CONTENT "${ehci_source}" @ONLY)
get_target_property(usb_sources libcherryusb SOURCES)
list(FILTER usb_sources EXCLUDE REGEX "usb_hc_ehci.c$")
set_property(TARGET libcherryusb PROPERTY SOURCES ${usb_sources})
target_sources(libcherryusb PRIVATE ${ehci_output})
target_include_directories(libcherryusb PRIVATE ${ehci})
