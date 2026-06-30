#requires -version 5
<#
单口接线下复位 AmebaD 并抓 LOG UART。
  RTS -> CHIP_EN, DTR -> bootstrap(LOG_TX strap), RXD -> LOG_TX  全在同一个 COM。
复位时 bootstrap 释放(DTR 高)=> 正常启动(不是 download mode)。
#>
[CmdletBinding()]
param(
    [string]$Port    = 'COM29',
    [int]   $Baud    = 1500000,
    [int]   $Seconds = 5,
    [switch]$NoReset,
    [string]$Send
)
$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 200
$sp.RtsEnable = $false   # CHIP_EN 高(上电)
$sp.DtrEnable = $false   # bootstrap 释放(高)=> 正常启动
$sp.Open()
try {
    if (-not $NoReset) {
        Write-Host "[reset] pulse CHIP_EN via $Port RTS (bootstrap released = normal boot) ..."
        $sp.RtsEnable = $true     # CHIP_EN 低
        Start-Sleep -Milliseconds 120
        $sp.RtsEnable = $false    # CHIP_EN 高 -> 启动
    }
    if ($Send) {
        Start-Sleep -Milliseconds 300
        Write-Host "[send] '$Send' + CRLF"
        $sp.Write($Send + "`r`n")
    }
    Write-Host "[log] reading $Port @ $Baud for ${Seconds}s ..."
    $buf = New-Object 'System.Collections.Generic.List[byte]'
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        try { $b = $sp.ReadByte(); if ($b -ge 0) { $buf.Add([byte]$b) } }
        catch [TimeoutException] { }
    }
    $sb = New-Object System.Text.StringBuilder
    foreach ($x in $buf) {
        if (($x -ge 32 -and $x -lt 127) -or $x -eq 10 -or $x -eq 13 -or $x -eq 9) {
            [void]$sb.Append([char]$x)
        } else {
            [void]$sb.Append('.')
        }
    }
    Write-Host ("----- LOG ({0} bytes @ {1}) -----" -f $buf.Count, $Baud)
    Write-Host $sb.ToString()
    $hex = ($buf | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    Write-Host "----- HEX -----"
    Write-Host $hex
    Write-Host "----- end -----"
}
finally {
    $sp.Close(); $sp.Dispose()
}
