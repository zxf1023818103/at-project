#requires -version 5
# Reset the board, capture boot+AWS, then send `set key AAAA` twice and capture the result.
[CmdletBinding()]
param(
    [string]$Port = 'COM3',
    [int]   $Baud = 1500000,
    [string]$BootPort = 'COM14',
    [double]$BootWait = 15,
    [string[]]$Send = @('set key AAAA', 'set key AAAA')
)
$ErrorActionPreference = 'Stop'
$c = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$c.Handshake = [System.IO.Ports.Handshake]::None
$c.RtsEnable = $false; $c.DtrEnable = $false
$c.Open()
# reset via boot port RTS
$b = New-Object System.IO.Ports.SerialPort $BootPort, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$b.Open(); $b.RtsEnable = $true; Start-Sleep -Milliseconds 120; $b.RtsEnable = $false; $b.Close()

$sb = New-Object System.Text.StringBuilder
function Pump([double]$sec) {
    $e = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $e) {
        if ($c.BytesToRead -gt 0) { [void]$sb.Append($c.ReadExisting()) } else { Start-Sleep -Milliseconds 15 }
    }
}
try {
    Pump $BootWait
    $i = 1
    foreach ($cmd in $Send) {
        [void]$sb.Append("`n##### SENT[$i]: $cmd #####`n")
        $c.Write($cmd + "`r")
        Pump 3
        $i++
    }
} finally { $c.Close() }
Write-Host $sb.ToString()
