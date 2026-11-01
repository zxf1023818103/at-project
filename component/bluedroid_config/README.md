# BL618DG Bluedroid PANU port

ESP-IDF Bluedroid Host runs above the Bouffalo dual-mode controller. Ethernet
frames from PAN/BNEP connect to a FreeRTOS+TCP multi-interface endpoint. No lwIP
is linked. The initial integration targets BL618DG B0 and Bouffalo SDK's CMake
build; it is not yet a generic port for other targets.

Pinned sources:

- ESP-IDF: `d445e947156f276dfe0db5804e4edf6ae90fe6fb`
- Bouffalo SDK: `237ed937856bf435f4f3642ea4728294723e1d87`
- FreeRTOS+TCP: `f755b4ff3097bb320dff72ea0ec3ace0338839d6`

`bouffalo_hci.c` adapts asynchronous controller command/event/ACL traffic;
`esp_compat.c` implements the required FreeRTOS, timer, logging and hardware RNG
interfaces. `nvs_easyflash.c` persists bonding blobs through EasyFlash/LittleFS.
`pan_freertos_tcp.c` bridges Ethernet RX/TX, DHCP endpoint lifecycle, congestion
and connection generations. ESP-IDF and FreeRTOS+TCP are kept unmodified. The SDK has a small net_al
integration patch recorded in `component/bouffalo_wifi_tcp_config`.

Add the FreeRTOS+TCP component before this directory to expose the
`bluedroid_pan_tcp` target. Forward PAN callbacks to
`bluedroid_pan_tcp_event()` and register the endpoint before calling
`FreeRTOS_IPInit_Multi()`. See `project/bl618dg_pan` for a complete application.

The current image enables Classic Bluetooth and PANU only. BLE profiles,
SCO/audio, NAP, shutdown/reinitialization and sustained-load
qualification are outside this first validation. The board's shared antenna
switch must select Bluetooth: **GPIO16 high** (2.4 GHz Wi-Fi uses low). The BT-only
example holds this level throughout operation. The opt-in coexistence build
uses the upstream BSP RF/SPDT and Wi-Fi/MACSW coexistence implementation, with
`component/bouffalo_wifi_tcp_config` replacing its network adaptation layer.
Standalone RF calibration is currently blocked on the attached board; see the
validation project README before enabling it. The example uses Just Works
pairing with automatic confirmation and remains discoverable; product pairing
policy should be implemented before deployment.

The standalone validation now uses `platform/bouffalo_sdk_bl618dg` and
`component/bluedroid_tcp_config`. Existing Ameba and Bouffalo projects keep
their original SDK and shared TCP configuration.
