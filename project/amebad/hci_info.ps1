#requires -version 5
<#
  Query the AmebaD BT controller's version + features over the HCI bridge (COM29).
  The BT-DTM image (AMEBAD_ENABLE_HCI=ON) opens a transparent HCI bridge on the
  LOGUART; this resets the device, syncs on the "entering HCI bridge" marker, then
  sends the HCI Informational / LE read commands and hex-dumps the replies.

  Usage: hci_info.ps1 [-Port COM29] [-Baud 1500000] [-SyncTimeoutSec 60]
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
$sp.RtsEnable = $false
$sp.DtrEnable = $false
$sp.Open()

function Send-Hci($name, [byte[]]$cmd) {
    $hex = ($cmd | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    Write-Host ("`n>> {0,-28} {1}" -f $name, $hex)
    $sp.DiscardInBuffer()
    $sp.Write($cmd, 0, $cmd.Length)
    $deadline = (Get-Date).AddMilliseconds(1500)
    $bytes = New-Object System.Collections.Generic.List[byte]
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 60
        $n = $sp.BytesToRead
        if ($n -gt 0) {
            $buf = New-Object byte[] $n
            $sp.Read($buf, 0, $n) | Out-Null
            $bytes.AddRange($buf)
            $deadline = (Get-Date).AddMilliseconds(300)
        }
    }
    if ($bytes.Count -gt 0) {
        Write-Host ("<< {0}" -f (($bytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' '))
    } else {
        Write-Host "<< (no reply)"
    }
}

try {
    Write-Host "[bt_hci_info] reset $Port @ $Baud ; waiting for the BT bridge ..."
    $sp.RtsEnable = $true;  Start-Sleep -Milliseconds 120
    $sp.RtsEnable = $false
    $deadline = (Get-Date).AddSeconds($SyncTimeoutSec)
    $synced = $false
    while ((Get-Date) -lt $deadline) {
        try { $line = $sp.ReadLine() } catch [TimeoutException] { continue }
        if ($line -and $line -match 'entering HCI bridge') { $synced = $true; break }
    }
    if (-not $synced) { Write-Host "[bt_hci_info] WARNING: bridge marker not seen in ${SyncTimeoutSec}s" }
    Start-Sleep -Milliseconds 600
    $sp.DiscardInBuffer()

    Send-Hci "HCI Reset"                    ([byte[]](0x01,0x03,0x0C,0x00))
    Send-Hci "Read Local Version Info"      ([byte[]](0x01,0x01,0x10,0x00))  # 0x1001
    Send-Hci "Read Local Supported Cmds"    ([byte[]](0x01,0x02,0x10,0x00))  # 0x1002 (64-byte map)
    Send-Hci "Read Local Supported Feats"   ([byte[]](0x01,0x03,0x10,0x00))  # 0x1003 (BR/EDR LMP feats)
    Send-Hci "LE Read Local Supported Feats"([byte[]](0x01,0x03,0x20,0x00))  # 0x2003 (LE feats)
    Send-Hci "LE Read Supported States"     ([byte[]](0x01,0x1C,0x20,0x00))  # 0x201C
    Send-Hci "LE Read Buffer Size"          ([byte[]](0x01,0x02,0x20,0x00))  # 0x2002
    Write-Host "`n[bt_hci_info] done."
}
finally {
    $sp.Close(); $sp.Dispose()
}
