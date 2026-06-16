#requires -version 5
<#
Open the AmebaD LOGUART console, optionally wait for boot, send one or more
commands, and capture everything for a fixed window. Output is collected and
printed at exit (powershell.exe over WSL buffers stdout until the process ends).

  -Port        console COM port (default COM3)
  -Baud        baud (default 1500000)
  -PreRead     seconds to read boot log before sending (default 10)
  -PostRead    seconds to keep reading after the last command (default 8)
  -Cmds        commands to send, each followed by CR; spaced -GapMs apart
  -GapMs       ms to wait (while reading) between commands (default 1500)
#>
[CmdletBinding()]
param(
    [string]   $Port     = 'COM3',
    [int]      $Baud     = 1500000,
    [double]   $PreRead  = 10,
    [double]   $PostRead = 8,
    [string[]] $Cmds     = @(),
    [int]      $GapMs    = 1500
)

$ErrorActionPreference = 'Stop'
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.Handshake = [System.IO.Ports.Handshake]::None
$sp.ReadTimeout = 100
$sp.NewLine = "`r`n"
$sp.RtsEnable = $false
$sp.DtrEnable = $false
$sp.Open()
$script:total = 0

$sb = New-Object System.Text.StringBuilder
function Pump([double]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $n = $sp.BytesToRead
            if ($n -gt 0) {
                $s = $sp.ReadExisting()
                [void]$sb.Append($s)
                $script:total += $s.Length
            } else {
                Start-Sleep -Milliseconds 20
            }
        } catch { Start-Sleep -Milliseconds 20 }
    }
}

try {
    Pump $PreRead
    foreach ($c in $Cmds) {
        $sp.Write($c + "`r")
        [void]$sb.Append("`n>>> SENT: $c`n")
        Pump ($GapMs / 1000.0)
    }
    Pump $PostRead
}
finally {
    $sp.Close(); $sp.Dispose()
}
Write-Host $sb.ToString()
Write-Host "[debug] total bytes read = $script:total"
