param([int]$Cycles=2,[long]$ExpectedBitsPerSecond=100000000,
      [string]$DataPort='COM22',[string]$ControlPort='',
      [string]$HostAddress='192.168.78.1',[string]$PeerMac='02-61-60-00-00-02',
      [string]$ResultDirectory='')
$ErrorActionPreference='Stop'
$base=Split-Path $PSScriptRoot -Parent
$out=if($ResultDirectory){$ResultDirectory}else{Join-Path $base 'build\validation\speed'}
New-Item -ItemType Directory -Force -Path $out | Out-Null
for($cycle=1;$cycle -le $Cycles;$cycle++){
 $resetArgs=@('-DataPort',$DataPort,'-Seconds','15')
 if($ControlPort){$resetArgs+=@('-ControlPort',$ControlPort,'-Reset')}
 else{$resetArgs+=@('-Command','reboot')}
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\console.ps1" `
   @resetArgs | Out-File -Encoding utf8 "$out\reboot-$cycle.log"
 if($LASTEXITCODE -ne 0){throw "Board restart failed in cycle $cycle"}
 $adapter=@(Get-NetAdapter -IncludeHidden | Where-Object {$_.MacAddress -eq $PeerMac})
 if($adapter.Count -ne 1){throw 'Expected exactly one matching USB NCM adapter'}
 $adapter | Select-Object Status,LinkSpeed,TransmitLinkSpeed,ReceiveLinkSpeed |
   ConvertTo-Json | Out-File -Encoding utf8 "$out\rate-$cycle.json"
 if($adapter[0].Status -ne 'Up' -or
    $adapter[0].TransmitLinkSpeed -ne $ExpectedBitsPerSecond -or
    $adapter[0].ReceiveLinkSpeed -ne $ExpectedBitsPerSecond){
  throw "Incorrect link speed after reboot $cycle"
 }
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot\ncm_test.ps1" `
   -HostAddress $HostAddress -Rounds 1 | Out-File -Encoding utf8 "$out\reconnect-$cycle.jsonl"
 if($LASTEXITCODE -ne 0){throw "Transfer failed after reboot $cycle"}
 @{cycle=$cycle;bits_per_second=$ExpectedBitsPerSecond;ok=$true} | ConvertTo-Json -Compress
}
