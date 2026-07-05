$u = [System.Net.Sockets.UdpClient]::new(5001)
$u.Client.ReceiveTimeout = 45000
$ep = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
$total=0L; $n=0L; $sw=$null
try { while ($true) { $d=$u.Receive([ref]$ep); if(-not $sw){$sw=[System.Diagnostics.Stopwatch]::StartNew(); $u.Client.ReceiveTimeout=4000}; $total+=$d.Length; $n++ } } catch {}
$u.Close()
if ($sw) { $sw.Stop(); Write-Output ("RECV {0} dgrams / {1} bytes in {2:N2}s = {3:N2} Mbit/s (delivered)" -f $n,$total,$sw.Elapsed.TotalSeconds,($total*8/$sw.Elapsed.TotalSeconds/1e6)) } else { Write-Output "RECV nothing (firewall blocked inbound)" }
