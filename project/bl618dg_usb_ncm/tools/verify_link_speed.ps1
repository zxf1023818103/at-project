param([int]$Cycles=2,[string]$DataPort='COM15',[string]$ControlPort='COM23')
$shared=Join-Path $PSScriptRoot '..\..\bl616_usb_ncm\tools\verify_link_speed.ps1'
$out=Join-Path $PSScriptRoot '..\build\validation\speed'
& $shared -Cycles $Cycles -DataPort $DataPort -ControlPort $ControlPort -HostAddress 192.168.79.1 `
  -PeerMac '02-61-8D-00-00-02' -ResultDirectory $out
