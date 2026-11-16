# NAN USB radio transport

CherryUSB High-Speed vendor interface `ffff:b018`, class/subclass/protocol
`ff:42:02`, OUT EP5 / IN EP4. This is distinct from the Bulk throughput fixture.

Each host exchange sends and receives one 16 KiB frame. A 16-byte header holds
magic, version, sequence and used length. Bounded, four-byte-aligned records carry
RPCs, replies, radio events or complete Ethernet packets. Up to ten full Ethernet
frames fit in an exchange. MCU pointers never cross the wire. Both MCUs are
32-bit little-endian; startup also checks the shared vendor-structure sizes.

The Green2 interrupt path only notifies a worker. That worker parses validated
records, calls WHC IPC, builds the response and submits IN. IN completion arms
the next OUT. The host has a separate transport task and correlates bounded RPC
replies by ID; the NAN task never blocks the USB receive path. Control records
use a separate queue from data. The host sleeps for one tick only when neither
direction carries records. DMA buffers remain in internal noncacheable RAM.

Frame size/version/record lengths/types and response sequence are checked before
dispatch. An exchange failure stops traffic rather than retransmitting a command
whose execution status is unknown. Queue drops, malformed frames and USB errors
are exposed by `nan_usb_status()`. RPC payloads are never logged; freed message
buffers are cleared. USB is the trusted physical link between the two modules.

`nan_usb_wire.c` has native boundary/malformed-frame tests in the shared NAN test
suite. The hardware host additionally verifies 12 echo RPCs spanning lengths
1, 511, 512, 1514, 2048 and 6144 bytes before publishing the service.

See [BL618DG NAN](../../../project/bl618dg_nan/README.md) for build and App use.
