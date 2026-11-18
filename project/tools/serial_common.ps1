#requires -version 5
<#
serial_common.ps1 - shared serial helpers for the bench .ps1 scripts (dot-source me):

  . "$PSScriptRoot/../tools/serial_common.ps1"     # from project/amebad or project/bouffalo

  $sp = Open-Serial COM3 1500000
  Read-Drain $sp 5 -Echo
  Send-Paced $sp "aws endpoint $ep"
  Send-CredFile $sp 'cert' build/aws/cert.pem      # auto-detects PEM vs bare-base64

Every console write is paced at 16 B / 6 ms: the device CLIs POLL their UART RX (no RX IRQ), so a
full-rate blast at 1.5-2 Mbps overruns the small RX FIFO and drops chars - which silently corrupts
whatever was sent (a mangled endpoint, or a base64 body that then fails to decode). This applies to
short lines too, not just the ~1.6 KB cert/key bodies.
#>

function Open-Serial([string]$Port, [int]$Baud,
                     [int]$ReadTimeoutMs = 1000, [int]$WriteTimeoutMs = 2000) {
    $sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.ReadTimeout  = $ReadTimeoutMs
    $sp.WriteTimeout = $WriteTimeoutMs
    $sp.NewLine = "`r`n"
    $sp.Open()
    return $sp
}

# Read + discard (or echo) device output for $sec seconds.
function Read-Drain([System.IO.Ports.SerialPort]$sp, [int]$sec, [switch]$Echo) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        try { $l = $sp.ReadLine(); if ($Echo -and $l) { Write-Host "  | $l" } }
        catch [TimeoutException] { }
    }
}

# Pace every write at 16 B / 6 ms (polled RX FIFO on the device would overrun otherwise).
function Send-Paced([System.IO.Ports.SerialPort]$sp, [string]$line) {
    $sp.DiscardInBuffer()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes("$line`r")
    for ($i = 0; $i -lt $bytes.Length; $i += 16) {
        $n = [Math]::Min(16, $bytes.Length - $i)
        $sp.Write($bytes, $i, $n)
        Start-Sleep -Milliseconds 6
    }
    return $bytes.Length
}

# Send `aws <sub> <base64>` from a credentials file: a PEM (armor + newlines stripped, bare base64
# body kept) or an already-bare .b64 file - auto-detected. The device parses the one CLI line with
# FreeRTOS_CLIGetParameter + mbedtls_base64_decode (no custom streaming/ACK).
function Send-CredFile([System.IO.Ports.SerialPort]$sp, [string]$sub, [string]$file,
                       [int]$DrainSec = 10) {
    if (-not (Test-Path $file)) { throw "creds file not found: $file" }
    $raw = Get-Content $file -Raw
    if ($raw -match '-----BEGIN') {
        $b64 = ((Get-Content $file | Where-Object { $_ -notmatch '^\s*-----' -and $_.Trim() -ne '' }) -join '')
    } else {
        $b64 = ($raw -replace '\s', '')
    }
    Write-Host "[prov] aws $sub  ($($b64.Length) base64 chars from $file)"
    $sent = Send-Paced $sp "aws $sub $b64"
    Write-Host "[prov] sent $sent B; awaiting result ..."
    Read-Drain $sp $DrainSec -Echo
}
