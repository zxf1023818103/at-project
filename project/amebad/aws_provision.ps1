#requires -version 5
<#
aws_provision.ps1 — drive the AmebaD AWS=ON `set` console commands over the LOGUART
to provision AWS IoT credentials at runtime:

  set endpoint <ats-endpoint>   -> TF-M Protected Storage
  set cert      (streams a PEM) -> TF-M Protected Storage
  set key       (streams a PEM) -> TF-M Protected Storage   (the secret)

The `set cert` / `set key` handlers (component/amebad/app_km4/aws_cli.c) switch to RAW
byte streaming because the CLI line editor caps at 80 bytes — far short of a ~1.7 KB PEM.
This script mirrors that: send `set key`, wait for the `>>> paste ...` prompt, stream the
PEM lines, then a lone `EOF`, then read back the `set key: N bytes -> PS OK` result.

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

function Send-Pem([System.IO.Ports.SerialPort]$sp, [string]$sub, [string]$file) {
    if (-not (Test-Path $file)) { throw "PEM not found: $file" }
    # Strip the PEM armor (the BEGIN/END lines have spaces -> the CLI would split them) + all
    # newlines, leaving the bare base64 body, and send it as ONE ordinary CLI line:
    #   set <sub> <base64>
    # The device parses it with FreeRTOS_CLIGetParameter and mbedtls_base64_decode (no custom
    # streaming/ACK). Pace the write: the CLI line editor polls the LOGUART, so a full-rate blast
    # would overrun the small RX FIFO and drop chars (-> the base64 decode fails, visibly).
    $b64 = (Get-Content $file | Where-Object { $_ -notmatch '^\s*-----' -and $_.Trim() -ne '' }) -join ''
    Write-Host "[prov] set $sub  ($($b64.Length) base64 chars from $file)"
    $sp.DiscardInBuffer()
    $bytes = [System.Text.Encoding]::ASCII.GetBytes("set $sub $b64`r")
    for ($i = 0; $i -lt $bytes.Length; $i += 16) {
        $n = [Math]::Min(16, $bytes.Length - $i)
        $sp.Write($bytes, $i, $n)
        Start-Sleep -Milliseconds 6
    }
    Write-Host "[prov] sent $($bytes.Length) B; awaiting result ..."
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
        $sp.WriteLine("set endpoint $Endpoint")
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
