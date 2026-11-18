param([string]$Port='COM3',[int]$Settle=20,[string]$Cmds='pstest 32 1;pstest 32 1;pstest 48 1;pstest 64 1;pstest 96 1;pstest 128 1;pstest 160 1;pstest 192 1;pstest 224 1')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../tools/serial_common.ps1"
$sp = Open-Serial $Port 1500000 400
function Drain([int]$sec){
  $d=(Get-Date).AddSeconds($sec)
  while((Get-Date) -lt $d){
    try { $l=$sp.ReadLine(); if($l -match 'pstest'){ Write-Host $l } } catch [TimeoutException] { }
  }
}
try {
  Write-Host "[settle $Settle s]"; Drain $Settle
  $sp.WriteLine(''); Start-Sleep -Milliseconds 300; Drain 1
  foreach($c in ($Cmds -split ';')){
    Write-Host ">>> $c"
    $sp.WriteLine($c)
    Start-Sleep -Milliseconds 500
    Drain 4
  }
} finally { $sp.Close(); $sp.Dispose() }
