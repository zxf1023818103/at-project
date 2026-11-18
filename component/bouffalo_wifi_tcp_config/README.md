# Bouffalo Wi-Fi FHOST adapter for FreeRTOS+TCP

This component replaces the SDK's `wifi6_lwip_adapter`. RF calibration,
coexistence recipes, Wi-Fi MAC/FHOST and WPA remain upstream implementations;
Ethernet frames and DHCP use FreeRTOS+TCP. No lwIP library is linked.

The initial adapter supports IPv4 STA with DHCP. AP DHCP server, routing/NAT,
IPv6, P2P and promiscuous packet hooks are not implemented. Unsupported service
requests return errors. FHOST interface names remain `wl1`/`wl2` for WPA, with
`wl1` exposed as the STA interface alongside the PAN endpoint.

TX buffers reserve the FHOST-required 384-byte headroom and are allocated from
WRAM for DMA access. RX is copied into FreeRTOS network buffers, then the vendor
RX buffer is released. EAPOL frames go to the upstream WPA event loop. Interface
registration completes before `FreeRTOS_IPInit_Multi()`; association and DHCP
requests update interface state through network-down/reinitialization events.

`sdk-net-al.patch` supplies two narrow SDK integration changes: select an
application-provided net_al directory, and let WPA include address conversion
types without requiring BSD socket headers. `wifi_inet.h` supplies these address
types/conversions; it is not a socket layer. Apply to official SDK commit
`237ed937856bf435f4f3642ea4728294723e1d87`. The validation project's Makefile
checks whether this exact patch is already applied and applies it when needed.

Wi-Fi-only hardware validation now reaches FHOST startup, WPA association and
DHCP, DNS, HTTP and 1500-byte IPv4 ping on both 2.4 and 5 GHz using GPIO16 low. Full results are recorded in the
validation project's Wi-Fi report. Concurrent PAN/Wi-Fi operation is still
blocked earlier by standalone Bluetooth RF calibration; Wi-Fi-only success
must not be interpreted as validation of coexistence.
