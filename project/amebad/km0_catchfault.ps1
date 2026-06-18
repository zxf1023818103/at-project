#requires -version 5
# Loop: reset-halt KM0, bp at xTaskIncrementTick entry, resume, and on each of the first ticks
# read pxDelayedTaskList / xNextTaskUnblockTime / xSchedulerRunning. Stop when a tick shows the
# corrupted list (pxDelayedTaskList not a valid 0x0008xxxx RAM ptr) — that's the fault precursor.
param([int]$Resets=12, [int]$TicksPerBoot=6, [string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function OReadPrompt([int]$maxms=1500){
    $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($maxms)
    while((Get-Date)-lt $d){
        if($ns.DataAvailable){ $b=New-Object byte[] 4096;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}
            if($sb.ToString().TrimEnd().EndsWith('>')){ break } }
        else { Start-Sleep -Milliseconds 8 }
    }
    return $sb.ToString()
}
function O([string]$c,[int]$ms=1500){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return OReadPrompt $ms }
function Word([string]$resp,[string]$addr){ if($resp -match ("(?i)0x0*"+$addr.TrimStart('0')+":\s+([0-9a-fA-F]{8})")){ return $Matches[1] } return '????????' }
[void](OReadPrompt 400)
[void](O 'targets ameba.km0' 400)

for($r=1; $r -le $Resets; $r++){
    [void](O 'reset halt' 2500)
    [void](O 'rbp all' 300)
    [void](O 'bp 0xc0006a0 2 hw' 400)
    $bad=$false
    for($t=1; $t -le $TicksPerBoot; $t++){
        [void](O 'resume' 250)
        $wh = O 'wait_halt 1500' 1800
        if($wh -match 'timed out'){ Write-Host ("[r{0}] tick {1}: no bp hit (good boot ran on / faulted+gated)" -f $r,$t); break }
        $dl  = Word (O 'mdw 0x85164 1' 500) '85164'
        $odl = Word (O 'mdw 0x85168 1' 500) '85168'
        $nut = Word (O 'mdw 0x85288 1' 500) '85288'
        $tc  = Word (O 'mdw 0x852d4 1' 500) '852d4'
        $sr  = Word (O 'mdw 0x852a8 1' 500) '852a8'
        Write-Host ("[r{0} t{1}] DL=0x{2} ODL=0x{3} nextUnblk=0x{4} tick=0x{5} sched=0x{6}" -f $r,$t,$dl,$odl,$nut,$tc,$sr)
        if($dl -notmatch '^000(8|9)[0-9a-fA-F]{4}$' -or $odl -notmatch '^000(8|9)[0-9a-fA-F]{4}$'){ Write-Host "  >>> CORRUPT LIST POINTER caught"; $bad=$true; break }
    }
    if($bad){ break }
}
$ns.Close(); $tcp.Close()
