[CmdletBinding()]
param([string]$BootPort = 'COM14')
$ErrorActionPreference = 'Stop'
$sp = New-Object System.IO.Ports.SerialPort $BootPort, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.Open()
try {
    $sp.RtsEnable = $true   # CHIP_EN low
    Start-Sleep -Milliseconds 120
    $sp.RtsEnable = $false  # release -> chip boots normally
} finally { $sp.Close(); $sp.Dispose() }
Write-Host "[reset] done via $BootPort"
