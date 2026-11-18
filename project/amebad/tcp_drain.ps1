$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Any, 5001)
$listener.Server.ReceiveBufferSize = 1048576   # 1 MB -> large advertised window
$listener.Start()
Write-Output "tcp drain: listening 0.0.0.0:5001 (RX buf 1MB)"
if (-not $listener.Server.Poll(60000000, [System.Net.Sockets.SelectMode]::SelectRead)) { Write-Output "NO CONN"; exit 1 }
$client = $listener.AcceptTcpClient()
$client.ReceiveBufferSize = 1048576
$stream = $client.GetStream(); $client.ReceiveTimeout = 3000
$buf = New-Object byte[] 262144; $total = 0L; $sw = $null
try { while (($n = $stream.Read($buf,0,$buf.Length)) -gt 0) { if(-not $sw){$sw=[System.Diagnostics.Stopwatch]::StartNew()}; $total += $n } } catch {}
if ($sw) { $sw.Stop(); Write-Output ("DRAIN {0} bytes in {1:N2}s = {2:N2} Mbit/s" -f $total,$sw.Elapsed.TotalSeconds,($total*8/$sw.Elapsed.TotalSeconds/1e6)) } else { Write-Output "no data" }
$listener.Stop()
