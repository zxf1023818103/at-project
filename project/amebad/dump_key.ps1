#requires -version 5
param([double]$WaitSec=18,[string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORP([int]$maxms=4000){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($maxms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 8192;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}; if($sb.ToString().TrimEnd().EndsWith('>')){break} } else {Start-Sleep -Milliseconds 8} }; return $sb.ToString() }
function O([string]$c,[int]$ms=4000){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORP $ms }
[void](ORP 300)
$b=New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$b.Open(); $b.RtsEnable=$true; Start-Sleep -Milliseconds 120; $b.RtsEnable=$false; $b.Close()
Start-Sleep -Seconds $WaitSec
Write-Host (O 'targets ameba.km4' 500)
Write-Host (O 'halt' 1500)
$p='C:/Users/zxf10/keydump.bin'
Write-Host (O ("dump_image $p 0x10010e70 1193") 4000)
Write-Host (O 'resume' 400)
$ns.Close(); $tcp.Close()
