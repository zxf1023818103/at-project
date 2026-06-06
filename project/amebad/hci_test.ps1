#requires -version 5
<#
  Drive BLE DTM HCI commands over the AmebaD HCI bridge (COM29).

  The BT-DTM image (AMEBAD_ENABLE_HCI=ON) brings up the BT controller and then
  opens a transparent HCI bridge on the LOGUART (set_hci_uart_out), so this raw-HCI
  sender reaches the controller's UART1. Boot time varies (KAT + wolfCrypt bench +
  wifi_on + bte_init ~30 s), so instead of a fixed wait we SYNC on the marker line
  "entering HCI bridge" that bt_dtm_enter_bridge() prints right before the LOGUART
  turns into a raw HCI pipe. After that marker, COM29 carries raw HCI only.

  Usage: hci_test.ps1 [-Port COM29] [-Baud 1500000] [-SyncTimeoutSec 60]
#>
[CmdletBinding()]
param(
    [string]$Port           = 'COM29',
    [int]   $Baud           = 1500000,
    [int]   $SyncTimeoutSec = 60
)
$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout  = 200
$sp.WriteTimeout = 500
$sp.RtsEnable = $false   # CHIP_EN high (powered)
$sp.DtrEnable = $false   # bootstrap released -> normal boot
$sp.Open()

function Send-Hci($name, [byte[]]$cmd) {
    $hex = ($cmd | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    Write-Host ("`n>> {0,-22} {1}" -f $name, $hex)
    $sp.DiscardInBuffer()
    $sp.Write($cmd, 0, $cmd.Length)
    # Poll up to ~1.2 s for an event (controller replies fast, but give margin).
    $deadline = (Get-Date).AddMilliseconds(1200)
    $bytes = New-Object System.Collections.Generic.List[byte]
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 60
        $n = $sp.BytesToRead
        if ($n -gt 0) {
            $buf = New-Object byte[] $n
            $sp.Read($buf, 0, $n) | Out-Null
            $bytes.AddRange($buf)
            $deadline = (Get-Date).AddMilliseconds(250)   # got data; grab the tail then stop
        }
    }
    if ($bytes.Count -gt 0) {
        Write-Host ("<< {0}" -f (($bytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' '))
    } else {
        Write-Host "<< (no reply)"
    }
}

try {
    # Reset (pulse CHIP_EN via RTS); bootstrap released = normal boot.
    Write-Host "[bt_hci] reset $Port @ $Baud ; waiting for the BT bridge to open ..."
    $sp.RtsEnable = $true;  Start-Sleep -Milliseconds 120
    $sp.RtsEnable = $false

    # Sync: read boot text until the bridge-open marker appears.
    $sb = New-Object System.Text.StringBuilder
    $deadline = (Get-Date).AddSeconds($SyncTimeoutSec)
    $synced = $false
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $sp.ReadLine()
        } catch [TimeoutException] { continue }
        if ($line) {
            [void]$sb.AppendLine($line)
            if ($line -match 'bte_init OK' -or $line -match 'BT INIT success' -or $line -match 'Start upperStack') {
                Write-Host ("   .. {0}" -f $line.Trim())
            }
            if ($line -match 'entering HCI bridge') {
                Write-Host ("   .. {0}" -f $line.Trim())
                $synced = $true
                break
            }
        }
    }
    if (-not $synced) {
        Write-Host "[bt_hci] WARNING: never saw 'entering HCI bridge' marker within ${SyncTimeoutSec}s."
        Write-Host "[bt_hci] last boot text:"
        Write-Host ($sb.ToString())
    }
    # Let bt_uart_bridge_open() finish (ClearRxFifo + IRQ register + set_hci_uart_out).
    Start-Sleep -Milliseconds 600
    $sp.DiscardInBuffer()

    # DTM HCI commands (Bluetooth Core HCI). Reply = HCI event (04 0E .. = Command Complete).
    Send-Hci "HCI Reset"           ([byte[]](0x01,0x03,0x0C,0x00))
    Send-Hci "Read Local Version"  ([byte[]](0x01,0x01,0x10,0x00))
    Send-Hci "Read BD_ADDR"        ([byte[]](0x01,0x09,0x10,0x00))
    # LE Transmitter Test v1: ch=0, len=0x25(37), payload=0 (PRBS9). CC status @ [6].
    Send-Hci "LE Transmitter Test" ([byte[]](0x01,0x1E,0x20,0x03,0x00,0x25,0x00))
    Start-Sleep -Milliseconds 400
    # LE Test End -> CC with 2-byte packet count (TX test -> 0).
    Send-Hci "LE Test End"         ([byte[]](0x01,0x1F,0x20,0x00))
    Write-Host "`n[bt_hci] done. Expected per cmd: 04 0E 04 <op_lo> <op_hi> 00 (status 0x00)."
}
finally {
    $sp.Close(); $sp.Dispose()
}
