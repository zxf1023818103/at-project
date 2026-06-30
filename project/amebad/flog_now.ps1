#requires -version 5
# Halt KM4 (no reset) and dump the FLOG — used right after a set key to see the store's ProgramData calls.
param([string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORP([int]$maxms=1500){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($maxms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 8192;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}; if($sb.ToString().TrimEnd().EndsWith('>')){break} } else {Start-Sleep -Milliseconds 8} }; return $sb.ToString() }
function O([string]$c,[int]$ms=1500){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORP $ms }
[void](ORP 300)
Write-Host (O 'targets ameba.km4' 400)
Write-Host (O 'halt' 800)
Write-Host "--- FLOG header (magic|count, progBytes, eraseN, progN) ---"
Write-Host (O 'mdw 0x10002100 4' 700)
Write-Host "--- FLOG ring (48 x addr,info) ---"
Write-Host (O 'mdw 0x10002120 32' 900)
Write-Host (O 'mdw 0x100021a0 32' 900)
Write-Host (O 'mdw 0x10002220 32' 900)
Write-Host (O 'resume' 300)
$ns.Close(); $tcp.Close()
