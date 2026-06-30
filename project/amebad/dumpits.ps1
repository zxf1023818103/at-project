#requires -version 5
param([string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORP([int]$m=6000){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($m); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 8192;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}; if($sb.ToString().TrimEnd().EndsWith('>')){break} } else {Start-Sleep -Milliseconds 8} }; return $sb.ToString() }
function O([string]$c,[int]$m=6000){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORP $m }
[void](ORP 300)
[void](O 'targets ameba.km4' 400); [void](O 'halt' 800)
Write-Host (O 'dump_image C:/Users/zxf10/itsdump.bin 0x083f0000 0x4000' 8000)
[void](O 'resume' 300); $ns.Close(); $tcp.Close()
