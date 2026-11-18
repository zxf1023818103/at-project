param([string]$HostAddress='192.168.78.1',[int]$Port=8080,[int]$Rounds=10,
      [int]$Bytes=2097152,[double]$MinMbps=1.6)
$ErrorActionPreference='Stop'
[Net.ServicePointManager]::Expect100Continue=$false
$payload=[byte[]]::new($Bytes)
for($i=0;$i -lt $Bytes;$i++){$payload[$i]=[byte]($i % 251)}
$sha=[Security.Cryptography.SHA256]::Create()
$expected=[BitConverter]::ToString($sha.ComputeHash($payload))
function Request([string]$Method,[string]$Path,[byte[]]$Body=$null){
 $request=[Net.HttpWebRequest]::Create("http://${HostAddress}:${Port}${Path}")
 $request.Proxy=$null;$request.Method=$Method;$request.KeepAlive=$false
 $request.Timeout=30000;$request.ReadWriteTimeout=30000
 $request.ServicePoint.UseNagleAlgorithm=$false
 if($null -ne $Body){
  $request.ContentLength=$Body.Length
  $writer=$request.GetRequestStream()
  try{$writer.Write($Body,0,$Body.Length)}finally{$writer.Dispose()}
 }
 $response=$request.GetResponse()
 try{
  $memory=[IO.MemoryStream]::new()
  try{$response.GetResponseStream().CopyTo($memory);return ,$memory.ToArray()}
  finally{$memory.Dispose()}
 }finally{$response.Dispose()}
}
function Status{
 $data=Request GET '/status'
 return ([Text.Encoding]::UTF8.GetString($data) | ConvertFrom-Json)
}
try{
 @{event='start';bytes=$Bytes;sha256=$expected;status=(Status)} | ConvertTo-Json -Compress
 $failures=0
 for($round=0;$round -lt $Rounds;$round++){
  foreach($direction in @('download','upload')){
   $clock=[Diagnostics.Stopwatch]::StartNew()
   if($direction -eq 'download'){
    $data=Request GET "/file?bytes=$Bytes"
    $clock.Stop()
    if($data.Length -ne $Bytes -or [BitConverter]::ToString($sha.ComputeHash($data)) -ne $expected){throw 'Download length/SHA256 mismatch'}
   }else{
    $data=Request POST '/upload' $payload
    $clock.Stop()
    $result=[Text.Encoding]::UTF8.GetString($data) | ConvertFrom-Json
    if($result.received -ne $Bytes -or $result.bad -ne 0){throw 'Upload validation failed'}
   }
   $rate=$Bytes*8/$clock.Elapsed.TotalSeconds/1000000
   if($rate -lt $MinMbps){$failures++}
   @{event='transfer';round=$round;direction=$direction;bytes=$Bytes;
     seconds=$clock.Elapsed.TotalSeconds;mbps=$rate;ok=$true;rate_ok=($rate -ge $MinMbps)} | ConvertTo-Json -Compress
  }
 }
 @{event='finish';failures=$failures;status=(Status)} | ConvertTo-Json -Compress
 if($failures){exit 1}
}finally{$sha.Dispose()}
