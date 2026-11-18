# Instrument the existing build-local workaround; do not modify SDK sources
# or change the wait/scheduling policy during this measurement.
file(READ ${ehci_output} diagnostic_source)
set(needle "uint32_t remaining = urb->timeout;")
string(REPLACE "${needle}" "${needle}\n        unsigned recovered = 0;" diagnostic_source "${diagnostic_source}")
set(needle "if (ret == 0) break;")
string(REPLACE "${needle}" "if (ret == 0) { if (!recovered) ++bl618dg_ehci_irq_completions; break; }"
    diagnostic_source "${diagnostic_source}")
set(needle "ehci_check_qh(bus, &g_async_qh_head[bus->hcd.hcd_id], qh);")
set(replacement [=[++bl618dg_ehci_poll_checks;
                ehci_check_qh(bus, &g_async_qh_head[bus->hcd.hcd_id], qh);
                if (urb->errorcode != -USB_ERR_BUSY) {
                    recovered = 1; ++bl618dg_ehci_poll_completions;
                }]=])
# Match only the workaround's indented call, not the normal ISR scanner.
string(REPLACE "                ${needle}" "                ${replacement}"
    diagnostic_source "${diagnostic_source}")
set(diagnostic_source "#include <stdint.h>\nvolatile uint32_t bl618dg_ehci_irq_completions, bl618dg_ehci_poll_completions, bl618dg_ehci_poll_checks;\n${diagnostic_source}")
file(CONFIGURE OUTPUT ${ehci_output} CONTENT "${diagnostic_source}" @ONLY)
