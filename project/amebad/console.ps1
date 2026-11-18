#requires -version 5
<#
console.ps1 — open the AmebaD LOGUART console, optionally send a few commands (paced so the
polled RX FIFO doesn't overrun at high baud), and stream the raw output for a fixed window. Handy
for inspecting runtime state (env, getenv, blemac) and watching the AWS connect/telemetry log
without a separate terminal GUI holding the port.

Reads RAW bytes (not ReadLine): the device mixes line endings — the monitor/CLI uses CR-LF but the
app's printf/DiagPrintf use LF-CR ("\n\r"), so a fixed-NewLine ReadLine would never split the app
log. Multiple `-Send` commands are one string split on ';'.

Usage (from WSL):
  pwsh project/amebad/console.ps1 -Port COM3 -Seconds 20
  pwsh project/amebad/console.ps1 -Port COM3 -Send 'getenv endpoint; getenv cert; env' -Seconds 25
#>
[CmdletBinding()]
param(
    [string]$Port      = 'COM3',
    [int]   $Baud      = 1500000,
    [string]$Send      = '',
    [int]   $Seconds   = 20,
    [string]$ResetPort = ''        # if set, pulse CHIP_EN (RTS) on this boot port to reboot first
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/serial_common.ps1"

function Reset-Chip([string]$port) {
    Write-Host "[console] reset via $port (CHIP_EN pulse) ..."
    $b = New-Object System.IO.Ports.SerialPort $port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $b.Open()
    try {
        $b.RtsEnable = $true     # CHIP_EN low (chip off)
        Start-Sleep -Milliseconds 120
        $b.RtsEnable = $false    # release -> boot normally (DTR/LOG_TX not held low)
    } finally { $b.Close(); $b.Dispose() }
}

# Like the shared Send-Paced, but deliberately does NOT DiscardInBuffer (keeps any log bytes that
# arrived between commands) and returns nothing.
function Send-PacedNoDiscard([System.IO.Ports.SerialPort]$sp, [string]$line) {
    # Pace the write (16 B / 6 ms): the device CLI polls the LOGUART, so a full-rate blast at
    # 1.5 Mbps overruns the small RX FIFO and drops chars.
    $bytes = [System.Text.Encoding]::ASCII.GetBytes("$line`r")
    for ($i = 0; $i -lt $bytes.Length; $i += 16) {
        $n = [Math]::Min(16, $bytes.Length - $i)
        $sp.Write($bytes, $i, $n)
        Start-Sleep -Milliseconds 6
    }
}

# Drain raw bytes for $sec, writing them straight through (the terminal renders CR/LF either way).
function Read-Raw([System.IO.Ports.SerialPort]$sp, [int]$sec) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $chunk = $sp.ReadExisting()
        if ($chunk) { [Console]::Out.Write($chunk) } else { Start-Sleep -Milliseconds 50 }
    }
}

if ($ResetPort) { Reset-Chip $ResetPort }

$sp = Open-Serial $Port $Baud 200
try {
    $sp.DiscardInBuffer()
    if ($Send) {
        $sp.Write("`r"); Start-Sleep -Milliseconds 300; [void]$sp.ReadExisting()   # nudge a prompt
        foreach ($cmd in ($Send -split ';')) {
            $cmd = $cmd.Trim()
            if (-not $cmd) { continue }
            Write-Host "`n[console] > $cmd"
            Send-PacedNoDiscard $sp $cmd
            Read-Raw $sp 3
        }
    }
    Write-Host "`n[console] draining ${Seconds}s ..."
    Read-Raw $sp $Seconds
}
finally { $sp.Close(); $sp.Dispose() }
Write-Host "`n[console] done."
