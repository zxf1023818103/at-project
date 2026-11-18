# Raw TCP sender -> device link-local :5001 for ~15s (outbound from host, no inbound firewall rule).
$dev = "fe80::96c9:60ff:feb7:ed8%4"
$c = [System.Net.Sockets.TcpClient]::new([System.Net.Sockets.AddressFamily]::InterNetworkV6)
try { $c.Connect([System.Net.IPAddress]::Parse($dev), 5001) }
catch { Write-Output "CONNECT FAILED: $_"; exit 1 }
Write-Output "connected to $dev :5001"
$s = $c.GetStream()
$buf = New-Object byte[] 8192
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$total = 0L
try { while ($sw.Elapsed.TotalSeconds -lt 15) { $s.Write($buf,0,$buf.Length); $total += $buf.Length } }
catch { Write-Output "SEND err: $_" }
$sw.Stop(); $s.Close(); $c.Close()
Write-Output ("SENT {0} bytes in {1:N2}s" -f $total, $sw.Elapsed.TotalSeconds)
