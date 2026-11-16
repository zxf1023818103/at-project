# BL618DG CherryUSB host/device benchmark

```
make -C project/bl618dg_usb_bench whole ROLE=host -j8
make -C project/bl618dg_usb_bench whole ROLE=device -j8
```

Output is `build/usb-throughput/bl618dg-{host,device}/whole_flash_data.bin`.
This AP-only fixture starts neither radio nor NP. It shares the existing USB
fixture's FreeRTOS configuration and the SDK's CherryUSB v1.6.1 stack.

Current bench: COM37 programs and controls BL618DG, COM15 programs Green2,
and COM19 controls Green2. The native USB cable connects the modules directly.
These port assignments replace the old NCM fixture's bench defaults.

Use the **SDK's** `tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand.exe` with
`--chipname bl618dg --port COM37 --baudrate 500000`, the selected firmware,
`--addr 0x0 --reset`. The older tool in `D:/MCU_Tools` does not recognize this
board's ROM. Programming must finish with successful verification. The UART
console runs at 500 kbaud for reliable capture through this CH340 adapter.

See `component/cherryusb_config/benchmark/README.md` for the `usbbench` command,
payload validation, result fields and timing definition.

The host build generates a fixture-local EHCI source through
`app/ehci_completion.cmake`; the SDK worktree remains unchanged. On this board,
512-byte transfers could complete in hardware while the host's completion wait
timed out: the qTD was inactive with zero remaining bytes, and the device
reported one more received packet than the host. The workaround checks completed
control/bulk descriptors after a 1 ms semaphore wait. It retains the IRQ path,
does not resend data, and counts the polling cost in the reported elapsed time.
The cause of the missing completion notification is not yet isolated below this
driver boundary. This is a synchronous benchmark workaround, not an asynchronous
host-stack fix. The CMake patch verifies its source anchor and fails if the
pinned SDK driver changes incompatibly. CMake 3.18 or newer is required.
