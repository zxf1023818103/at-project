#requires -version 5
<#
Thin wrapper: the real provisioning flow is the shared project/tools/aws_provision.ps1 (one script
for both platforms). This keeps the historical amebad invocation working with its old defaults
(COM3 @ 1.5M, piecemeal -Endpoint/-Cert/-Key, no auto-start).

  pwsh project/amebad/aws_provision.ps1 -Port COM3 -Key build/aws-bw20/key.pem
  pwsh project/amebad/aws_provision.ps1 -Port COM3 -Endpoint a1...-ats.iot....amazonaws.com
#>
[CmdletBinding()]
param(
    [string]$Port      = 'COM3',
    [int]   $Baud      = 1500000,
    [int]   $SettleSec = 25,
    [string]$Endpoint,
    [string]$Cert,
    [string]$Key
)
$fwd = @{ Port = $Port; Baud = $Baud; SettleSec = $SettleSec }
if ($Endpoint) { $fwd.Endpoint = $Endpoint }
if ($Cert)     { $fwd.Cert = (Resolve-Path $Cert).Path }
if ($Key)      { $fwd.Key  = (Resolve-Path $Key).Path }
& "$PSScriptRoot/../tools/aws_provision.ps1" @fwd
