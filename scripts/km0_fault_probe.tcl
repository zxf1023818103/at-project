init
targets ameba.km4
halt
echo "=== KM4 ==="
reg pc
reg msp
reg lr
echo "=== CFSR(0xe000ed28) BFAR(0xe000ed38) HFSR(0xe000ed2c) ==="
mdw 0xe000ed28
mdw 0xe000ed38
mdw 0xe000ed2c
echo "=== KM0 ==="
targets ameba.km0
halt
reg pc
reg sp
shutdown
