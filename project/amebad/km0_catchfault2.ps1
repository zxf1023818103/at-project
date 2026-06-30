#requires -version 5
# Catch the delayed-task-list PROCESSING path (xTaskIncrementTick + 0x56 = 0xc0006f6, taken only
# when tick >= xNextTaskUnblockTime, i.e. the first task unblock ~tick 50). Run KM0 ~full speed to
# it, then dump pxDelayedTaskList's List_t + head item chain looking for 0xa5a5a5a5.
param([int]$Resets=16,[string]$OocdHost='127.0.0.1',[int]$OocdPort=4444)
$ErrorActionPreference='Stop'
$enc=[System.Text.Encoding]::ASCII
$tcp=New-Object System.Net.Sockets.TcpClient; $tcp.Connect($OocdHost,$OocdPort); $ns=$tcp.GetStream()
function ORP([int]$maxms=1500){ $sb=New-Object System.Text.StringBuilder; $d=(Get-Date).AddMilliseconds($maxms); while((Get-Date)-lt $d){ if($ns.DataAvailable){$b=New-Object byte[] 4096;$n=$ns.Read($b,0,$b.Length); if($n){[void]$sb.Append($enc.GetString($b,0,$n))}; if($sb.ToString().TrimEnd().EndsWith('>')){break} } else {Start-Sleep -Milliseconds 8} }; return $sb.ToString() }
function O([string]$c,[int]$ms=1500){ $by=$enc.GetBytes($c+"`n"); $ns.Write($by,0,$by.Length); $ns.Flush(); return ORP $ms }
function W([string]$resp,[string]$addr){ if($resp -match ("(?i)0x0*"+$addr.TrimStart('0')+":\s+([0-9a-fA-F]{8})")){ return $Matches[1] } return '????????' }
[void](ORP 400); [void](O 'targets ameba.km0' 400)

for($r=1;$r -le $Resets;$r++){
    [void](O 'reset halt' 2500)
    [void](O 'rbp all' 300)
    [void](O 'bp 0xc0006f6 2 hw' 400)
    [void](O 'resume' 250)
    $wh = O 'wait_halt 4000' 4300
    if($wh -match 'timed out'){ Write-Host ("[r{0}] no unblock-path hit in 4s (good/hung)" -f $r); continue }
    $pcv = if((O 'reg pc' 300) -match '0x([0-9a-fA-F]{8})'){$Matches[1]}else{'????????'}
    $dlp = W (O 'mdw 0x85164 1' 600) '85164'     # pxDelayedTaskList pointer
    Write-Host ("[r{0}] HIT pc=0x{1} pxDelayedTaskList=0x{2}" -f $r,$pcv,$dlp)
    if($dlp -match '^[0-9a-fA-F]{8}$' -and $dlp -ne '????????'){
        $lt = O ("mdw 0x$dlp 5") 700              # List_t: uxItems, pxIndex, xListEnd{val,next,prev}
        Write-Host ("   List_t: " + $lt.Trim())
        # follow xListEnd.pxNext (4th word, offset 12) = head item; dump 5 words
        $nx = W $lt ([Convert]::ToString([Convert]::ToInt32($dlp,16)+12,16))
        if($nx -match '^000(8|9)' ){ Write-Host ("   head item @0x{0}: {1}" -f $nx,(O ("mdw 0x$nx 5") 600).Trim()) }
    }
    if($lt -match 'a5a5a5a5' -or $dlp -match 'a5a5a5a5'){ Write-Host "   >>> 0xa5a5a5a5 in delayed list — FAULT precursor caught"; break }
}
$ns.Close(); $tcp.Close()
