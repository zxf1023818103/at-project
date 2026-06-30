#requires -version 5
# Set a HW breakpoint at xTaskIncrementTick on KM0, reset, catch the first hits, and
# inspect pxDelayedTaskList + step toward the faulting deref while memory is still readable.
param([string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORead([int]$ms=500){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($ms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 4096;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}}else{Start-Sleep -Milliseconds 12} }; return $sb.ToString() }
function O([string]$c,[int]$ms=500){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORead $ms }
[void](ORead 300)

Write-Host (O 'targets ameba.km0' 300)
Write-Host (O 'halt' 800)
Write-Host (O 'rbp all' 300)
Write-Host (O 'bp 0xc0006a0 2 hw' 300)   # xTaskIncrementTick entry

# reset+halt at the reset vector so the bp is armed before ANY FreeRTOS code runs
Write-Host (O 'reset halt' 2000)
Write-Host (O 'targets ameba.km0' 300)
Write-Host (O 'rbp all' 200)
Write-Host (O 'bp 0xc0006a0 2 hw' 300)
Write-Host (O 'resume' 300)

# poll for the breakpoint, inspect on each of the first few hits
for ($h=1; $h -le 6; $h++) {
    $halted=$false
    for ($w=0; $w -lt 20; $w++) {
        $st = O 'targets ameba.km0' 150
        if ($st -match 'halted') { $halted=$true; break }
        Start-Sleep -Milliseconds 150
    }
    if (-not $halted) { Write-Host "[hit $h] no halt (booted past / hung gated)"; Write-Host (O 'reg pc' 300); break }
    $pc = O 'reg pc' 250
    $tcb = O 'mdw 0x85160 3' 250     # pxCurrentTCB, pxDelayedTaskList, pxOverflowDelayedTaskList
    $sr  = O 'mdw 0x852a8 1' 200     # xSchedulerRunning
    $nt  = O 'mdw 0x85248 1' 200     # uxCurrentNumberOfTasks
    Write-Host "[hit $h] $($pc.Trim()) | TCB/DL/ODL: $($tcb.Trim()) | sched=$($sr.Trim()) ntasks=$($nt.Trim())"
    O 'resume' 150 | Out-Null
}
Write-Host (O 'rbp all' 200)
$ns.Close(); $tcp.Close()
