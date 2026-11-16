# Native Ameba WHC USB on CherryUSB

Experimental host transport for the original local Ameba SDK's Green2 WHC USB
firmware. The device uses Realtek's original USB stack; BL618DG uses CherryUSB
and the same shared FreeRTOS NAN/PASN/TCP sources as the bridge profile.

Reference wire definitions and device handlers are in the local SDK's
`component/wifi/whc/whc_common/whc_def.h`, `whc_dev/whc_dev_api_path.c`,
`whc_dev/whc_dev_api.c`, `whc_dev/whc_dev_trx.c` and `whc_dev/usb/whc_usb_dev.c`.
The codec and host transport here are new implementations; no Linux driver
code is linked into the MCU image.

- Device `0bda:f851`, interface `ff:00:01`, USB High-Speed.
- Independent RX/TX tasks, IN `0x84`, OUT `0x05`. The device also advertises
  OUT `0x06` and `0x07`; this first host implementation uses one OUT pipe to
  maintain frame order. It is not the Linux driver's multi-URB scheduler.
- Variable-size messages, no 16 KiB polling or application-level bridge ACK.
  Exact multiples of the 512-byte maximum packet size terminate with a ZLP.
- Uncached internal-RAM DMA buffers; queued CPU-owned records use PSRAM.
- Host IN URBs have 2048-byte capacity; OUT messages retain a 1536-byte limit
  for compatibility with the original device configuration. The optional
  Green2 `CONFIG_WHC_USB_BULK_2048` enlarges its receive buffer and backing SKB
  pool. It does not change the 512-byte endpoint MPS, Ethernet MTU or enable
  frame aggregation. From the repo root, build that device variant with
  `python3 project/bl618dg_nan/build_native_green2.py --bulk-2048 --out build/native-whc/bulk2048`.
- 16-byte API/Ethernet headers, 8-byte custom-command headers. Interface 2 is
  NAN. Lengths and DMA padding are bounded before dispatch; unused full-MAC
  header bytes are ignored because native firmware does not initialize them.
- Serialized API calls. A timeout stops the transport because the native
  protocol cannot distinguish a late reply from a later call to the same API.
- Keep `wifi_user_config.cfg80211=1`: native NAN events are conditional on it.
  Calling the vendor's generic `SET_HOST_RTOS` API disables those events.

This profile selects full-MAC Ethernet firmware, not the optional `CONFIG_WHCH`
raw 802.11 transport/aggregation. Arbitrary disconnect/reconnect during an
active API call is not a validated use case; restart the host after reconnect.

Host codec verification:

```sh
mkdir -p build/native-whc
cc -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  component/cherryusb_config/whc_usb/{whc_wire,wire_test}.c \
  -o build/native-whc/wire-test
ASAN_OPTIONS=detect_leaks=0 build/native-whc/wire-test
```

Leak detection is disabled only for the sandbox's ptrace restriction; the
codec test performs no heap allocation. ASan and UBSan remain enabled.
