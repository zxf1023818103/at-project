param(
    [string]$Port = 'COM19',
    [int]$Seconds = 7,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\..\build\tl3238_hello')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$serial = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,One
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 1500
$serial.NewLine = "`n"
$capture = [System.Collections.Generic.List[string]]::new()
$helloCount = 0
try {
    $serial.Open()
    $serial.DiscardInBuffer()
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        try {
            $line = $serial.ReadLine().TrimEnd([char]13)
            $record = '{0:F3}s {1}' -f $clock.Elapsed.TotalSeconds, $line
            $capture.Add($record)
            Write-Host $record
            if ($line -eq 'hello world') { $helloCount++ }
        } catch [System.TimeoutException] { }
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    $capture | Set-Content (Join-Path $OutputDirectory "uart-$Port.log") -Encoding UTF8
}
if ($helloCount -lt 3) { throw "UART verification failed: $helloCount complete hello world lines" }
Write-Host "PASS: $Port 115200 8N1, $helloCount hello world lines received."
