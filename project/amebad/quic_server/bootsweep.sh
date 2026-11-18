#!/usr/bin/env bash
# Sweep the XIP boot-hang pad for the QUIC image: build with AMEBAD_XIP_PAD=$1,
# flash, reset, and report whether the KM4 app reaches main() ([KM4] banner).
# Usage: bootsweep.sh <pad-bytes>
set -e
PAD="${1:?pad bytes}"
REPO=/home/zxf10/at-project
UNC='\\wsl.localhost\Ubuntu\home\zxf10\at-project'

echo "===== PAD=$PAD : configure + build ====="
cmake -S "$REPO" -B "$REPO/build/km4" -G Ninja -DAMEBAD_CHIP=amebad-km4 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/amebad-km4.cmake -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DAT_ENABLE_QUIC=ON -DAMEBAD_WOLFSSL_ARMASM=ON -DAMEBAD_XIP_PAD="$PAD" >/dev/null 2>&1
ninja -C "$REPO/build/km4" target_img2_km4 >/dev/null 2>&1
python3 "$REPO/component/amebad/image_tool/build_image_all.py" \
  --out "$REPO/build/amebad/Image_All.bin" >/dev/null 2>&1
echo "built $(stat -c%s "$REPO/build/amebad/Image_All.bin") bytes"

echo "===== PAD=$PAD : flash ====="
timeout 180 powershell.exe -NoProfile -ExecutionPolicy Bypass \
  -File "$UNC\\project\\amebad\\flash.ps1" -BootPort COM14 -FlashPort COM3 \
  -Image "$UNC\\build\\amebad\\Image_All.bin" 2>&1 | tr -d '\r' \
  | grep -iE "SUCCESS|FAIL|Download Complete|refused" | tail -3

echo "===== PAD=$PAD : reset + capture (14s) ====="
OUT=$(timeout 26 powershell.exe -NoProfile -Command "\$r=New-Object System.IO.Ports.SerialPort 'COM3',1500000,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One); \$r.Open(); \$b=New-Object System.IO.Ports.SerialPort 'COM14',115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One); \$b.Open(); \$b.RtsEnable=\$true; Start-Sleep -Milliseconds 150; \$b.RtsEnable=\$false; \$end=(Get-Date).AddSeconds(14); \$o=''; while((Get-Date) -lt \$end){ \$c=\$r.ReadExisting(); if(\$c){\$o+=\$c}; Start-Sleep -Milliseconds 25 }; \$r.Close(); \$b.Close(); Write-Output \$o" 2>&1 | tr -d '\r')

if echo "$OUT" | grep -q "\[KM4\]"; then
    echo ">>> PAD=$PAD : BOOT OK (reached main)"
    echo "$OUT" | grep -iE "KM4|QUIC|network|DHCP|wifi|hand|====" | head -20
else
    echo ">>> PAD=$PAD : HANG (no [KM4]); last console bytes:"
    echo "$OUT" | tail -4
fi
