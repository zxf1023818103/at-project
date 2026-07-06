#requires -version 5
<#
aws_provision.ps1 - drive the bl602 `aws` console command over uart0 (COM19 @ 2 Mbps) to provision
AWS IoT credentials + start the telemetry client, fully from the files aws_setup.sh produced.

  aws endpoint <host>   -> env "endpoint"
  aws ca   <base64-DER> -> LittleFS aws_ca.der   (server trust: Amazon Root CA 1)
  aws cert <base64-DER> -> LittleFS aws_cert.der  (device certificate)
  aws key  <base64-DER> -> LittleFS aws_key.der   (device private key)
  aws start             -> start the telemetry task

The creds come from the .b64 files under -CredsDir (default build_bl602/aws), each already the bare
base64 body of the DER (aws_setup.sh did the PEM->DER->base64). Every console write is paced at
16 B / 6 ms: the bl602 CLI POLLS the uart0 RX (no RX IRQ), so a full-rate 2 Mbps blast overruns the
FIFO and drops chars, silently corrupting the base64 -> decode failure. The device's line buffer is
2560 B (bl602 override), enough for the ~1.6 KB key body.

Optionally sets WiFi creds first (-Ssid/-Password -> env WIFI_SSID/WIFI_PASSWORD) and reboots so the
device reconnects before AWS. Skip those if the board already has WiFi provisioned.

Usage (from WSL):
  pwsh project/bouffalo/aws_provision.ps1                       # provision + start from build_bl602/aws
  pwsh project/bouffalo/aws_provision.ps1 -Ssid MyAP -Password secret
  pwsh project/bouffalo/aws_provision.ps1 -Port COM19 -CredsDir /path/to/aws
#>
[CmdletBinding()]
param(
    [string]$Port      = 'COM19',
    [int]   $Baud      = 2000000,
    [string]$CredsDir  = "$PSScriptRoot/build_bl602/aws",
    [int]   $SettleSec = 20,          # wait for boot + WiFi + DHCP before provisioning
    [string]$Ssid,
    [string]$Password,
    [switch]$NoStart                  # provision only, don't `aws start`
)
$ErrorActionPreference = 'Stop'

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

function Send-B64([System.IO.Ports.SerialPort]$sp, [string]$sub, [string]$file) {
    if (-not (Test-Path $file)) { throw "creds file not found: $file (run tools/aws_setup.sh first)" }
    $b64 = ((Get-Content $file -Raw) -replace '\s', '')
    Write-Host "[prov] aws $sub  ($($b64.Length) base64 chars)"
    [void](Send-Paced $sp "aws $sub $b64")
    Read-Drain $sp 8 -Echo
}

$endpoint = (Get-Content "$CredsDir/endpoint.txt" -Raw).Trim()
Write-Host "[prov] endpoint = $endpoint"
Write-Host "[prov] creds    = $CredsDir"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout  = 1000
$sp.WriteTimeout = 2000
$sp.NewLine = "`r`n"
$sp.Open()
try {
    if ($Ssid) {
        Write-Host "[prov] setting WiFi creds + reboot ..."
        [void](Send-Paced $sp "setenv WIFI_SSID $Ssid");         Read-Drain $sp 2 -Echo
        if ($Password) { [void](Send-Paced $sp "setenv WIFI_PASSWORD $Password"); Read-Drain $sp 2 -Echo }
        [void](Send-Paced $sp "reboot");                          Read-Drain $sp 3
    }

    Write-Host "[prov] settling ${SettleSec}s (boot / WiFi / DHCP) ..."
    Read-Drain $sp $SettleSec
    $sp.WriteLine("")                 # nudge a fresh prompt
    Start-Sleep -Milliseconds 300
    Read-Drain $sp 1 -Echo

    [void](Send-Paced $sp "aws endpoint $endpoint"); Read-Drain $sp 4 -Echo
    Send-B64 $sp 'ca'   "$CredsDir/ca.b64"
    Send-B64 $sp 'cert' "$CredsDir/cert.b64"
    Send-B64 $sp 'key'  "$CredsDir/key.b64"

    [void](Send-Paced $sp "aws"); Read-Drain $sp 3 -Echo    # show provisioning status

    if (-not $NoStart) {
        Write-Host "[prov] aws start"
        [void](Send-Paced $sp "aws start")
        Write-Host "[prov] watching telemetry log for 40s ..."
        Read-Drain $sp 40 -Echo
    }
}
finally { $sp.Close(); $sp.Dispose() }
Write-Host "[prov] done."
