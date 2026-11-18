param(
 [Parameter(Mandatory=$true)]
 [ValidateSet('Serve','Voltage','On','Off','Metadata','Sample','Stop')][string]$Action,
 [string]$Port,
 [string]$StateDirectory = "$env:LOCALAPPDATA\Codex\blu-session",
 [ValidateRange(500,5000)][int]$Millivolts = 3300,
 [ValidateRange(0.1,60)][double]$Seconds = 5,
 [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8

function Get-CommandBytes([string]$Kind, [int]$Mv) {
 switch ($Kind) {
  'Voltage' { return [byte[]]@(0x0D,($Mv -shr 8),($Mv -band 255)) }
  'On' { return [byte[]]@(0x0C,0x01) }
  'Off' { return [byte[]]@(0x0C,0x00) }
  'Metadata' { return [byte[]]@(0x19) }
  'Sample' { return [byte[]]@(0x06) }
  'Stop' { return [byte[]]@() }
  'Serve' { return [byte[]]@() }
  default { throw "Unknown action: $Kind" }
 }
}
function Write-JsonAtomic($Value, [string]$Path) {
 $tmp = "$Path.$PID.tmp"
 $Value | ConvertTo-Json -Depth 6 -Compress | Set-Content -Encoding UTF8 $tmp
 Move-Item -LiteralPath $tmp -Destination $Path -Force
}

if ($DryRun) {
 $bytes = @(Get-CommandBytes $Action $Millivolts)
 @{action=$Action;hex=(($bytes | ForEach-Object {'{0:X2}' -f $_}) -join ' ');millivolts=$Millivolts;seconds=$Seconds;dryRun=$true} | ConvertTo-Json -Compress
 exit 0
}

if ($Action -ne 'Serve') {
 $statePath = Join-Path $StateDirectory 'status.json'
 if (-not (Test-Path $statePath)) { throw 'No session. Start Serve in a persistent terminal first.' }
 $state = Get-Content -Raw $statePath | ConvertFrom-Json
 if (-not $state.open -or -not (Get-Process -Id $state.pid -ErrorAction SilentlyContinue)) {
  throw 'Session is not running. Inspect state before restarting.'
 }
 $id = [Guid]::NewGuid().ToString('N')
 $req = @{id=$id;action=$Action;millivolts=$Millivolts;seconds=$Seconds}
 $requestPath = Join-Path $StateDirectory "$id.request.json"
 $responsePath = Join-Path $StateDirectory "$id.response.json"
 Write-JsonAtomic $req $requestPath
 $timer = [Diagnostics.Stopwatch]::StartNew()
 while (-not (Test-Path $responsePath)) {
  if ($timer.Elapsed.TotalSeconds -gt ($Seconds + 15)) {
   throw "Request timed out; it may still execute. Inspect $requestPath and $responsePath before retrying."
  }
  Start-Sleep -Milliseconds 100
 }
 $response = Get-Content -Raw $responsePath | ConvertFrom-Json
 $response | ConvertTo-Json -Depth 6 -Compress
 if (-not $response.ok) { exit 1 }
 exit 0
}

New-Item -ItemType Directory -Force $StateDirectory | Out-Null
$lockStream = $null
$p = $null
$statusPath = Join-Path $StateDirectory 'status.json'
try {
 # Exclusive OS file lock prevents two servers from opening the same session.
 $lockStream = [IO.File]::Open((Join-Path $StateDirectory 'session.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 if (@(Get-ChildItem $StateDirectory -Filter '*.request.json').Count -gt 0) {
  throw 'Unprocessed requests exist. Inspect and archive them before starting; they will not be replayed automatically.'
 }
 if (-not $Port) {
  $matches = @(Get-CimInstance Win32_SerialPort | Where-Object {$_.PNPDeviceID -like 'USB\VID_15A2&PID_300A\*'})
  if ($matches.Count -ne 1) { throw 'Specify -Port: expected exactly one BLU device.' }
  $Port = $matches[0].DeviceID
 }
 $p = New-Object System.IO.Ports.SerialPort
 $p.PortName=$Port; $p.BaudRate=1000000; $p.DataBits=8
 $p.Parity=[System.IO.Ports.Parity]::None
 $p.StopBits=[System.IO.Ports.StopBits]::One
 $p.Handshake=[System.IO.Ports.Handshake]::None
 $p.DtrEnable=$true; $p.RtsEnable=$true
 $p.ReadTimeout=200; $p.WriteTimeout=2000
 $p.Open()
 $running=$true
 $lastStatus=[DateTime]::MinValue
 Write-Output "SESSION OPEN $Port; no device commands sent"
 while ($running) {
  if (((Get-Date)-$lastStatus).TotalSeconds -ge 1) {
   Write-JsonAtomic @{pid=$PID;port=$Port;open=$p.IsOpen;dtr=$p.DtrEnable;rts=$p.RtsEnable;updated=(Get-Date -Format o)} $statusPath
   $lastStatus=Get-Date
  }
  $requests=@(Get-ChildItem $StateDirectory -Filter '*.request.json' | Sort-Object CreationTime)
  foreach ($file in $requests) {
   $responsePath=$file.FullName.Replace('.request.json','.response.json')
   $result=@{ok=$false;port=$Port;updated=(Get-Date -Format o)}
   try {
    $req=Get-Content -Raw $file.FullName | ConvertFrom-Json
    $result.action=$req.action
    if ($req.action -eq 'Voltage' -and ([int]$req.millivolts -lt 500 -or [int]$req.millivolts -gt 5000)) {throw 'Voltage outside SDK range'}
    if ($req.action -eq 'Sample' -and ([double]$req.seconds -lt 0.1 -or [double]$req.seconds -gt 60)) {throw 'Duration outside 0.1..60 seconds'}
    if ($req.action -in @('Metadata','Sample')) {$p.DiscardInBuffer()}
    $bytes=[byte[]]@(Get-CommandBytes $req.action ([int]$req.millivolts))
    if ($bytes.Length) {$p.Write($bytes,0,$bytes.Length)}
    $result.sentHex=(($bytes | ForEach-Object {'{0:X2}' -f $_}) -join ' ')
    if ($req.action -in @('Metadata','Sample')) {
     $memory=New-Object IO.MemoryStream
     try {
      $buffer=New-Object byte[] 65536
      $timer=[Diagnostics.Stopwatch]::StartNew()
      $duration=if ($req.action -eq 'Metadata') {4.0} else {[double]$req.seconds}
      try {
       while ($timer.Elapsed.TotalSeconds -lt $duration) {
        $count=[Math]::Min($p.BytesToRead,$buffer.Length)
        if ($count -gt 0) {
         $n=$p.Read($buffer,0,$count)
         $memory.Write($buffer,0,$n)
         if ($req.action -eq 'Metadata' -and [Text.Encoding]::UTF8.GetString($memory.ToArray()).Contains('END')) {break}
        } else {Start-Sleep -Milliseconds 1}
       }
      } finally {
       if ($req.action -eq 'Sample') {$p.Write([byte[]]@(0x07),0,1)}
      }
      $extension=if ($req.action -eq 'Metadata') {'txt'} else {'bin'}
      $outPath=Join-Path $StateDirectory ($req.id+'.'+$extension)
      [IO.File]::WriteAllBytes($outPath,$memory.ToArray())
      $result.path=$outPath; $result.bytes=$memory.Length
      $result.elapsedSeconds=$timer.Elapsed.TotalSeconds
      if ($req.action -eq 'Metadata' -and -not [Text.Encoding]::UTF8.GetString($memory.ToArray()).Contains('END')) {throw 'Incomplete metadata; do not apply it as calibration'}
      if ($req.action -eq 'Sample' -and $memory.Length -eq 0) {throw 'No current samples received'}
     } finally {$memory.Dispose()}
    }
    if ($req.action -eq 'Off') {Start-Sleep -Milliseconds 200; $running=$false}
    if ($req.action -eq 'Stop') {$running=$false}
    $result.ok=$true
   } catch {$result.error=$_.Exception.Message}
   Write-JsonAtomic $result $responsePath
   Remove-Item -LiteralPath $file.FullName
   if (-not $running) {break}
  }
  Start-Sleep -Milliseconds 100
 }
} finally {
 # A competing server that failed the lock must not overwrite active status.
 if ($lockStream) {
  if ($p) {if ($p.IsOpen) {$p.Close()}; $p.Dispose()}
  Write-JsonAtomic @{pid=$PID;port=$Port;open=$false;updated=(Get-Date -Format o)} $statusPath
  $lockStream.Dispose()
 }
}
