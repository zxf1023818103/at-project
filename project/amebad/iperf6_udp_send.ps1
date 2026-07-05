$dev = "fe80::96c9:60ff:feb7:ed8%4"
$src = "fe80::bf1b:8ffd:df3e:93eb%4"
# TCP touch first: TCP works over v6 and makes the host's ND entry for the device Reachable.
try { $t=[System.Net.Sockets.TcpClient]::new([System.Net.Sockets.AddressFamily]::InterNetworkV6); $t.Connect([System.Net.IPAddress]::Parse($dev),5001); Start-Sleep -Milliseconds 400; $t.Close(); Write-Output "TCP touch ok" } catch { Write-Output "TCP touch FAILED: $_" }
# UDP blast, source bound to ifindex 4.
$u=[System.Net.Sockets.UdpClient]::new([System.Net.Sockets.AddressFamily]::InterNetworkV6)
$u.Client.Bind([System.Net.IPEndPoint]::new([System.Net.IPAddress]::Parse($src),0))
$ep=[System.Net.IPEndPoint]::new([System.Net.IPAddress]::Parse($dev),5001)
$buf=New-Object byte[] 1470
$sw=[System.Diagnostics.Stopwatch]::StartNew(); $n=0L
try { while ($sw.Elapsed.TotalSeconds -lt 15) { [void]$u.Send($buf,$buf.Length,$ep); $n++ } } catch { Write-Output "send err $_" }
$sw.Stop(); $u.Close()
Write-Output ("SENT {0} dgrams / {1} bytes in {2:N2}s ({3:N0} pps)" -f $n,($n*1470),$sw.Elapsed.TotalSeconds,($n/$sw.Elapsed.TotalSeconds))
