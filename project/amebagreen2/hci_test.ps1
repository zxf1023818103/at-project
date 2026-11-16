#requires -version 5
[CmdletBinding()]
param([string]$Port='COM15', [string]$BootPort='COM19', [int]$Baud=1500000)
$ErrorActionPreference='Stop'
$p=New-Object System.IO.Ports.SerialPort $Port,$Baud,'None',8,'One'
$p.Handshake='None'; $p.RtsEnable=$false; $p.DtrEnable=$false; $p.ReadTimeout=200
$p.Open()
function Read-Exact([int]$Count) {
    $data=New-Object byte[] $Count
    $end=(Get-Date).AddSeconds(5)
    for ($i=0; $i -lt $Count;) {
        try { $data[$i]=[byte]$p.ReadByte(); $i++ }
        catch [TimeoutException] { if ((Get-Date) -gt $end) { throw 'HCI response timeout' } }
    }
    return ,$data
}
function Command([int]$Opcode,[byte[]]$Parameters=@()) {
    [byte[]]$request=@(1,($Opcode -band 255),($Opcode -shr 8),$Parameters.Length)+$Parameters
    $p.Write($request,0,$request.Length)
    $end=(Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $end) {
        [byte[]]$type=Read-Exact 1
        if ($type[0] -ne 4) { throw ('Unexpected byte 0x{0:x2} in H4 event stream' -f $type[0]) }
        [byte[]]$head=Read-Exact 2
        [byte[]]$body=Read-Exact $head[1]
        if ($head[0] -eq 14 -and $body.Length -ge 4 -and
            ([int]$body[1] -bor ([int]$body[2] -shl 8)) -eq $Opcode) {
            if ($body[3] -ne 0) { throw ('HCI opcode {0:x4} status {1:x2}' -f $Opcode,$body[3]) }
            Write-Host ('PASS opcode 0x{0:x4}: {1}' -f $Opcode,([BitConverter]::ToString($body)))
            return
        }
    }
    throw ('HCI completion missing for 0x{0:x4}' -f $Opcode)
}
try {
    $control=New-Object System.IO.Ports.SerialPort $BootPort,115200,'None',8,'One'
    $control.RtsEnable=$false; $control.DtrEnable=$false; $control.Open()
    try { $control.RtsEnable=$true; Start-Sleep -Milliseconds 150; $control.RtsEnable=$false }
    finally { $control.Close(); $control.Dispose() }
    $boot=''; $end=(Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $end -and $boot -notmatch 'entering HCI bridge') {
        $boot+=$p.ReadExisting(); Start-Sleep -Milliseconds 30
    }
    if ($boot -notmatch 'entering HCI bridge') { throw "HCI bridge did not start: $boot" }
    Start-Sleep -Milliseconds 150
    $p.DiscardInBuffer()
    Command 0x0c03  # reset
    Command 0x1001  # local controller version
    Command 0x1002  # supported commands (64-byte binary payload)
    Command 0x1009  # Bluetooth address
    Command 0x2002  # LE buffer size
    Command 0x2003  # LE supported features
    Command 0x201d ([byte[]]@(0))  # LE receiver test, channel 0
    Command 0x201f  # end DTM
    Write-Host 'PASS: H4 bridge, controller commands and LE DTM receive/end'
} finally { $p.Close(); $p.Dispose() }
