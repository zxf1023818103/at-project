#requires -version 5
<#
aws_provision.ps1 — drive the AmebaD AWS=ON `set` console commands over the LOGUART
to provision AWS IoT credentials at runtime, each to its storage by secrecy:

  set endpoint <ats-endpoint>  -> env var "endpoint" (plain LittleFS KV/endpoint)
  set cert <base64-body>       -> plain LittleFS DER file (aws_cert.der); path = env var "cert"
  set key  <base64-body>       -> TF-M Internal Trusted Storage (the only secret)

The host strips the PEM armor + newlines (Send-Pem) and sends the bare base64 body as ONE
ordinary CLI line (`set cert <b64>` / `set key <b64>`); the device parses it with
FreeRTOS_CLIGetParameter + mbedtls_base64_decode. The write is paced at 16 B / 6 ms so the
polled LOGUART RX FIFO doesn't overrun (a dropped char would fail the base64 decode).

Usage (from WSL):
  pwsh project/amebad/aws_provision.ps1 -Port COM3 -Key build/aws-bw20/key.pem
  pwsh project/amebad/aws_provision.ps1 -Port COM3 -Endpoint a1...-ats.iot.ap-southeast-1.amazonaws.com
  pwsh project/amebad/aws_provision.ps1 -Port COM3 -Cert build/aws-bw20/cert.pem
#>
[CmdletBinding()]
param(
    [string]$Port     = 'COM3',
    [int]   $Baud     = 1500000,
    [int]   $SettleSec = 25,         # wait for boot + WiFi + AWS to settle before sending
    [string]$Endpoint,
    [string]$Cert,
    [string]$Key
)
$ErrorActionPreference = 'Stop'

function Read-Drain([System.IO.Ports.SerialPort]$sp, [int]$sec, [switch]$Echo) {
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        try { $l = $sp.ReadLine(); if ($Echo) { Write-Host "  | $l" } }
        catch [TimeoutException] { }
    }
}

# Pace EVERY console write at 16 B / 6 ms. The device CLI POLLS the LOGUART RX (no RX IRQ), so a
# full-rate blast at 1.5 Mbps overruns the small RX FIFO and drops chars — which silently corrupts
# whatever was sent (a mangled endpoint, or a base64 body that then fails to decode). This applies
# to the short `set endpoint <host>` line too, not just the ~1.6 KB cert/key bodies.
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

function Send-Pem([System.IO.Ports.SerialPort]$sp, [string]$sub, [string]$file) {
    if (-not (Test-Path $file)) { throw "PEM not found: $file" }
    # Strip the PEM armor (the BEGIN/END lines have spaces -> the CLI would split them) + all
    # newlines, leaving the bare base64 body, and send it as ONE ordinary CLI line:
    #   set <sub> <base64>
    # The device parses it with FreeRTOS_CLIGetParameter and mbedtls_base64_decode (no custom
    # streaming/ACK).
    $b64 = (Get-Content $file | Where-Object { $_ -notmatch '^\s*-----' -and $_.Trim() -ne '' }) -join ''
    Write-Host "[prov] set $sub  ($($b64.Length) base64 chars from $file)"
    $sent = Send-Paced $sp "set $sub $b64"
    Write-Host "[prov] sent $sent B; awaiting result ..."
    Read-Drain $sp 12 -Echo
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout  = 1000
$sp.WriteTimeout = 2000
$sp.NewLine = "`r`n"
$sp.Open()
try {
    Write-Host "[prov] settling ${SettleSec}s (boot/WiFi/AWS) ..."
    Read-Drain $sp $SettleSec
    $sp.WriteLine("")                   # nudge a fresh prompt
    Start-Sleep -Milliseconds 300
    Read-Drain $sp 1 -Echo

    if ($Endpoint) {
        Write-Host "[prov] set endpoint $Endpoint"
        [void](Send-Paced $sp "set endpoint $Endpoint")   # paced: a full-rate line corrupts on the polled RX
        Read-Drain $sp 4 -Echo
    }
    if ($Cert) { Send-Pem $sp 'cert' (Resolve-Path $Cert) }
    if ($Key)  { Send-Pem $sp 'key'  (Resolve-Path $Key) }

    if (-not ($Endpoint -or $Cert -or $Key)) {
        Write-Host "[prov] nothing to do (pass -Endpoint/-Cert/-Key). Draining 5s:"
        Read-Drain $sp 5 -Echo
    }
}
finally { $sp.Close(); $sp.Dispose() }
Write-Host "[prov] done."
