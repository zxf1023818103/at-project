#requires -version 5
param([string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORead([int]$ms=400){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($ms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 4096;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}}else{Start-Sleep -Milliseconds 10} }; return $sb.ToString() }
function O([string]$c,[int]$ms=400){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORead $ms }
[void](ORead 250)
Write-Host (O 'targets ameba.km0' 250)
Write-Host "PC: $((O 'reg pc' 250).Trim())"
Write-Host "literal@0xc0007b4 (which list ptr addr): $((O 'mdw 0x0c0007b4 2' 250).Trim())"
Write-Host "pxCurrentTCB/pxDelayedTaskList/pxOverflowDelayedTaskList @0x85160:"
Write-Host (O 'mdw 0x85160 3' 250)
Write-Host "xSchedulerRunning(852a8)/uxCurrentNumberOfTasks(85248)/uxSchedulerSuspended(85250):"
Write-Host (O 'mdw 0x852a8 1' 200); Write-Host (O 'mdw 0x85248 1' 200); Write-Host (O 'mdw 0x85250 1' 200)
Write-Host "xTickCount(852d4)/xNextTaskUnblockTime(85288):"
Write-Host (O 'mdw 0x852d4 1' 200); Write-Host (O 'mdw 0x85288 1' 200)
# single-step from entry to ~0xc000708 watching r3/ip
Write-Host "=== single-step through the deref (watch r3) ==="
for ($i=0; $i -lt 18; $i++) {
    $pcline = (O 'reg pc' 200)
    $r3 = (O 'reg r3' 150)
    $pcv = if ($pcline -match '0x([0-9a-fA-F]{8})') { $Matches[1] } else { '????????' }
    $r3v = if ($r3 -match 'r3 .*0x([0-9a-fA-F]{8})') { $Matches[1] } else { '?' }
    Write-Host ("  pc=0x{0} r3=0x{1}" -f $pcv, $r3v)
    if ($pcv -match 'e00.....' -or $pcv -eq '00000000') { Write-Host "  (left function / faulted)"; break }
    O 'step' 150 | Out-Null
}
Write-Host (O 'reg pc' 200)
$ns.Close(); $tcp.Close()
