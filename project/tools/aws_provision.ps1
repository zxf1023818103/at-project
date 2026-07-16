#requires -version 5
<#
aws_provision.ps1 - drive the shared `aws` console command over the device UART to provision AWS
IoT credentials (and optionally start telemetry). One parameterized script for BOTH platforms
(replaces project/bouffalo/aws_provision.ps1 + project/amebad/aws_provision.ps1, which are now thin
wrappers over this):

  aws endpoint <host>    -> env "endpoint"
  aws ca   <base64-DER>  -> LittleFS aws_ca.der    (server trust; bouffalo flow)
  aws cert <base64/PEM>  -> LittleFS aws_cert.der  (device certificate)
  aws key  <base64/PEM>  -> LittleFS DER file, or TF-M ITS on an amebad STORAGE=ON image
  aws start              -> start the telemetry task

Credential files may be PEM or bare-base64 (.b64) - auto-detected per file (Send-CredFile).

Two invocation styles:
  # batch, from an aws_setup.sh output dir (endpoint.txt + ca/cert/key.b64) - the bouffalo flow:
  pwsh project/tools/aws_provision.ps1 -Port COM19 -Baud 2000000 -CredsDir build_bl602/aws
  # piecemeal - the amebad flow:
  pwsh project/tools/aws_provision.ps1 -Port COM3 -Baud 1500000 -Endpoint a1...-ats....amazonaws.com
  pwsh project/tools/aws_provision.ps1 -Port COM3 -Baud 1500000 -Cert cert.pem -Key key.pem

Optionally sets WiFi creds first (-Ssid/-Password -> env WIFI_SSID/WIFI_PASSWORD) and reboots so
the device reconnects before AWS. -Start runs `aws start` + watches telemetry for 40 s.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [int]   $Baud      = 2000000,
    [int]   $SettleSec = 20,          # wait for boot + WiFi + DHCP before provisioning
    [string]$CredsDir,                # batch mode: endpoint.txt + ca/cert/key.b64 from aws_setup.sh
    [string]$Endpoint,                # piecemeal mode: any subset of these four
    [string]$Ca,
    [string]$Cert,
    [string]$Key,
    [string]$Ssid,
    [string]$Password,
    [switch]$Start                    # `aws start` + watch the telemetry log after provisioning
)
$ErrorActionPreference = 'Stop'

. "$PSScriptRoot/serial_common.ps1"

if ($CredsDir) {
    if (-not $Endpoint) { $Endpoint = (Get-Content "$CredsDir/endpoint.txt" -Raw).Trim() }
    if (-not $Ca)       { $Ca       = "$CredsDir/ca.b64" }
    if (-not $Cert)     { $Cert     = "$CredsDir/cert.b64" }
    if (-not $Key)      { $Key      = "$CredsDir/key.b64" }
    Write-Host "[prov] creds    = $CredsDir"
}
if ($Endpoint) { Write-Host "[prov] endpoint = $Endpoint" }

$sp = Open-Serial $Port $Baud
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

    if ($Endpoint) {
        Write-Host "[prov] aws endpoint $Endpoint"
        [void](Send-Paced $sp "aws endpoint $Endpoint")
        Read-Drain $sp 4 -Echo
    }
    if ($Ca)   { Send-CredFile $sp 'ca'   $Ca }
    if ($Cert) { Send-CredFile $sp 'cert' $Cert }
    if ($Key)  { Send-CredFile $sp 'key'  $Key }

    [void](Send-Paced $sp "aws"); Read-Drain $sp 3 -Echo    # show provisioning status

    if ($Start) {
        Write-Host "[prov] aws start"
        [void](Send-Paced $sp "aws start")
        Write-Host "[prov] watching telemetry log for 40s ..."
        Read-Drain $sp 40 -Echo
    }
}
finally { $sp.Close(); $sp.Dispose() }
Write-Host "[prov] done."
