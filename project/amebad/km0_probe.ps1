#requires -version 5
# Reset via COM14, wait, then halt KM0 over OpenOCD telnet and dump scheduler state.
param([double]$WaitSec = 3.0, [string]$OocdHost='127.0.0.1', [int]$OocdPort=4444)
$ErrorActionPreference = 'Stop'
$enc = [System.Text.Encoding]::ASCII
$tcp = New-Object System.Net.Sockets.TcpClient
$tcp.Connect($OocdHost, $OocdPort)
$ns = $tcp.GetStream()
function ORead([int]$ms=500){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($ms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 4096;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}}else{Start-Sleep -Milliseconds 12} }; return $sb.ToString() }
function O([string]$c,[int]$ms=500){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORead $ms }
[void](ORead 300)

# reset
$b = New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$b.Open(); $b.RtsEnable=$true; Start-Sleep -Milliseconds 120; $b.RtsEnable=$false; $b.Close()
Start-Sleep -Seconds $WaitSec

Write-Host (O 'targets ameba.km0' 300)
Write-Host (O 'halt' 800)
$pc = O 'reg pc' 400; Write-Host $pc
Write-Host (O 'reg lr' 400)
Write-Host (O 'reg sp' 400)
Write-Host "--- pxCurrentTCB / pxDelayedTaskList / pxOverflowDelayedTaskList / (852d4=xTickCount) ---"
Write-Host (O 'mdw 0x85160 3' 400)
Write-Host "--- xSchedulerRunning(852a8) uxCurrentNumberOfTasks(85248) uxSchedulerSuspended(85250) ---"
Write-Host (O 'mdw 0x852a8 1' 300)
Write-Host (O 'mdw 0x85248 1' 300)
Write-Host (O 'mdw 0x85250 1' 300)
Write-Host "--- xTickCount(852d4) xNextTaskUnblockTime(85288) xPendedTicks(85290) ---"
Write-Host (O 'mdw 0x852d4 1' 300)
Write-Host (O 'mdw 0x85288 1' 300)
Write-Host (O 'mdw 0x85290 1' 300)
# read pxDelayedTaskList pointer value, then dump the List_t it points at (5 words)
$dl = O 'mdw 0x85164 1' 300; Write-Host "pxDelayedTaskList ptr: $dl"
if ($dl -match '0x85164:\s+([0-9a-fA-F]{8})') {
    $p = $Matches[1]
    Write-Host "--- *pxDelayedTaskList (List_t @ 0x$p): uxItems, pxIndex, xListEnd{val,next,prev} ---"
    Write-Host (O ("mdw 0x$p 6") 400)
}
Write-Host (O 'resume' 200)
$ns.Close(); $tcp.Close()
