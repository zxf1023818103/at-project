param([int]$Rounds=10,[int]$Bytes=2097152,[double]$MinMbps=1.6)
$shared=Join-Path $PSScriptRoot '..\..\bl616_usb_ncm\tools\ncm_test.ps1'
& $shared -HostAddress 192.168.79.1 -Rounds $Rounds -Bytes $Bytes -MinMbps $MinMbps
