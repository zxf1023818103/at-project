#requires -version 5
<#
  Reset AmebaD on the data COM, wait for the one-time boot crypto-benchmark spam to
  go QUIET, then drive the FreeRTOS+CLI: send commands and capture each reply.
#>
[CmdletBinding()]
param(
    [string]$Port = 'COM3',
    [int]   $Baud = 1500000,
    [string]$Out  = 'cli_test.out'
)
$ErrorActionPreference = 'Stop'

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

# Drain until no bytes arrive for $quietMs, or $maxMs elapses.
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

try {
    Write-Host "===== reset (RTS pulse on $Port) ====="
    $sp.RtsEnable = $true; Start-Sleep -Milliseconds 120; $sp.RtsEnable = $false
    $sp.DiscardInBuffer()

    Write-Host "===== draining boot + benchmark spam until quiet (max 90s) ====="
    DrainUntilQuiet 5000 100000
    Write-Host "===== console quiet; driving CLI ====="

    $sp.Write("`r")                       # wake a fresh prompt
    Start-Sleep -Milliseconds 200
    [void]$log.Append("`n--- after CR ---`n"); [void]$log.Append((Drain 400))

    foreach ($cmd in @('help', 'version', 'heap', 'echo hello-cli', 'tasks', 'boguscmd')) {
        [void]$log.Append("`n--- send: $cmd ---`n")
        $sp.Write($cmd + "`r")
        Drain 700 | Out-Null
    }
}
finally { $sp.Close() }

$log.ToString() | Set-Content -Path $Out -Encoding ASCII
Write-Host "`n========== CAPTURE ($Out) =========="
Write-Host $log.ToString()
