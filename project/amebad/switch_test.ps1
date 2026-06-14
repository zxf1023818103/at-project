#requires -version 5
<#
  Test the dual-core FreeRTOS-Plus-CLI console + the KM0<->KM4 hand-off.
  Reset via the boot port (CHIP_EN), then drive the console on the data port:
  verify KM4 owns at boot, `>>` / `km0` hands to KM0, `km4` / `>>` hands back.
#>
[CmdletBinding()]
param(
    [string]$BootPort = 'COM14',
    [string]$Port     = 'COM3',
    [int]   $Baud     = 1500000,
    [string]$Out      = 'switch_test.out'
)
$ErrorActionPreference = 'Stop'

# ---- reset the chip via the boot port (RTS -> CHIP_EN low, release -> boot) ----
Write-Host "===== reset via $BootPort ====="
$boot = New-Object System.IO.Ports.SerialPort $BootPort, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$boot.Open()
try { $boot.RtsEnable = $true; Start-Sleep -Milliseconds 120; $boot.RtsEnable = $false }
finally { $boot.Close(); $boot.Dispose() }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 150
$sp.RtsEnable = $false
$sp.DtrEnable = $false
$sp.Open()
$log = New-Object System.Text.StringBuilder

function Drain([int]$ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    $sb = New-Object System.Text.StringBuilder
    while ((Get-Date) -lt $deadline) {
        try { $c = $sp.ReadByte(); if ($c -ge 0) { [void]$sb.Append([char]$c) } }
        catch [TimeoutException] { }
    }
    [void]$log.Append($sb.ToString())
    return $sb.ToString()
}
function DrainUntilQuiet([int]$quietMs, [int]$maxMs) {
    $hardStop = (Get-Date).AddMilliseconds($maxMs)
    $lastByte = Get-Date
    while ((Get-Date) -lt $hardStop) {
        $got = $false
        try { $c = $sp.ReadByte(); if ($c -ge 0) { [void]$log.Append([char]$c); $got = $true } }
        catch [TimeoutException] { }
        if ($got) { $lastByte = Get-Date }
        elseif (((Get-Date) - $lastByte).TotalMilliseconds -ge $quietMs) { return }
    }
}
function Send([string]$cmd, [int]$wait = 800) {
    [void]$log.Append("`n--- send: $cmd ---`n")
    Write-Host "send: $cmd"
    $sp.Write($cmd + "`r")
    Drain $wait | Out-Null
}

try {
    Write-Host "===== draining boot until quiet (max 40s) ====="
    DrainUntilQuiet 3000 40000
    Write-Host "===== console quiet; driving CLI ====="

    $sp.Write("`r"); Start-Sleep -Milliseconds 200
    [void]$log.Append("`n--- after CR ---`n"); [void]$log.Append((Drain 400))

    Send 'help'
    Send 'version'        # expect: AmebaD KM4
    Send 'km4'            # on KM4 already -> "already on KM4"
    Send '>>'             # hand console to KM0
    Start-Sleep -Milliseconds 300
    Drain 600 | Out-Null  # KM0 prints its banner/prompt
    Send 'version'        # expect: AmebaD KM0
    Send 'heap'
    Send 'km4'            # hand back to KM4
    Start-Sleep -Milliseconds 300
    Drain 600 | Out-Null
    Send 'version'        # expect: AmebaD KM4 again
}
finally { $sp.Close() }

$log.ToString() | Set-Content -Path $Out -Encoding ASCII
Write-Host "`n========== CAPTURE ($Out) =========="
Write-Host $log.ToString()
