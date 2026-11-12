param([string]$Command='',[int]$Seconds=20,[switch]$Reset,
      [string]$DataPort='COM15',[string]$ControlPort='COM23')
$shared=Join-Path $PSScriptRoot '..\..\bl616_usb_ncm\tools\console.ps1'
& $shared -DataPort $DataPort -ControlPort $ControlPort -Command $Command -Seconds $Seconds -Reset:$Reset
