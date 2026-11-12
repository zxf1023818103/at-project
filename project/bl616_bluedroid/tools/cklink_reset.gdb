set pagination off
set confirm off
set remotetimeout 15
set architecture riscv:rv32
target remote 127.0.0.1:1025
set $boot = *(unsigned int *)0x2000f108
set {unsigned int}0x2000f108 = ($boot & 0xf3ffffff)
x/wx 0x2000f108
set $rst = *(unsigned int *)0x20000548
set {unsigned int}0x20000548 = ($rst & 0xfffffffe)
set {unsigned int}0x20000548 = ($rst | 1)
detach
quit
