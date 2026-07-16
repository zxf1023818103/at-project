#requires -version 5
<#
Thin wrapper: the real provisioning flow is the shared project/tools/aws_provision.ps1 (one script
for both platforms). This keeps the historical bouffalo invocation working with its old defaults
(COM19 @ 2M, batch mode from build_bl602/aws, auto `aws start`).

  pwsh project/bouffalo/aws_provision.ps1 [-Port COM19] [-CredsDir <dir>] [-Ssid ... -Password ...] [-NoStart]
#>
[CmdletBinding()]
param(
    [string]$Port      = 'COM19',
    [int]   $Baud      = 2000000,
    [string]$CredsDir  = "$PSScriptRoot/build_bl602/aws",
    [int]   $SettleSec = 20,
    [string]$Ssid,
    [string]$Password,
    [switch]$NoStart                  # provision only, don't `aws start`
)
$fwd = @{ Port = $Port; Baud = $Baud; CredsDir = $CredsDir; SettleSec = $SettleSec }
if ($Ssid)     { $fwd.Ssid = $Ssid }
if ($Password) { $fwd.Password = $Password }
if (-not $NoStart) { $fwd.Start = $true }
& "$PSScriptRoot/../tools/aws_provision.ps1" @fwd
