#requires -version 5
param([double]$WaitSec=3.5,[string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORP([int]$maxms=1200){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($maxms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 8192;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}; if($sb.ToString().TrimEnd().EndsWith('>')){break} } else {Start-Sleep -Milliseconds 8} }; return $sb.ToString() }
function O([string]$c,[int]$ms=1200){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORP $ms }
[void](ORP 300)
# fresh reset -> native-timing hang
$b=New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$b.Open(); $b.RtsEnable=$true; Start-Sleep -Milliseconds 120; $b.RtsEnable=$false; $b.Close()
Start-Sleep -Seconds $WaitSec
Write-Host (O 'targets ameba.km4' 400)
Write-Host (O 'halt' 700)
Write-Host "--- FLOG header @0x10002100 (magic|count, progBytes, eraseN, progN) ---"
Write-Host (O 'mdw 0x10002100 4' 700)
Write-Host "--- FLOG ring @0x10002120 (48 x addr,info) ---"
Write-Host (O 'mdw 0x10002120 32' 900)
Write-Host (O 'mdw 0x100021a0 32' 900)
Write-Host (O 'mdw 0x10002220 32' 900)
# also where is KM0 stuck?
Write-Host (O 'targets ameba.km0' 300)
Write-Host (O 'halt' 600)
Write-Host "KM0 pc: $((O 'reg pc' 400))"
Write-Host (O 'resume' 200)
$ns.Close(); $tcp.Close()
