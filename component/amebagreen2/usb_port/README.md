# Green2 CherryUSB port

Build with `make -C project/amebagreen2 USB_BENCH=ON USB_ROLE=device MINIMAL=ON`
or select `USB_ROLE=host`. Use separate `BUILD=` directories for the two images.
The existing COM15 / COM19 flashing path and flash-layout safeguards apply.

The USB controller is DWC2 at `0x40080000`, interrupt 59, with an internal
16-bit UTMI PHY on PA4 (DM) and PA5 (DP). The shared benchmark uses EP4 IN and
EP5 OUT. The controller uses buffer DMA; its descriptors and payload storage
are allocated in `.nocache.data.usb`, covered by the existing MPU setup.

`lib_usb_hal.a` is copied from the SDK revision and path recorded in
`../UPSTREAM.json`, under `../LICENSE.realtek`. Only the SoC initialization and
PHY calibration routines are used. CherryUSB owns enumeration, endpoint-zero
requests, transfers and interrupts. Building does not access `platform/ameba-rtos`.
The released library omits its private header; the narrow table declaration in
`usb_green2.c` documents the verified ABI entries used by this port.

The benchmark protocol and timing definition are in
`../../cherryusb_config/benchmark/README.md`.
