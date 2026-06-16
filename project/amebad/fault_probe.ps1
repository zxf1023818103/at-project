#requires -version 5
# Trigger `set key AAAA` over the console, then halt KM4 via OpenOCD telnet (4444)
# and dump the stacked exception frame + fault status registers to pin the faulting PC.
[CmdletBinding()]
param(
    [string]$Port = 'COM3',
    [int]   $Baud = 1500000,
    [string]$Cmd  = 'set key AAAA',
    [string]$OocdHost = '127.0.0.1',
    [int]   $OocdPort = 4444
)
$ErrorActionPreference = 'Stop'

# --- OpenOCD telnet helper -------------------------------------------------
$tcp = New-Object System.Net.Sockets.TcpClient
$tcp.Connect($OocdHost, $OocdPort)
$ns = $tcp.GetStream()
$enc = [System.Text.Encoding]::ASCII
function OocdRead([int]$ms = 600) {
    $sb = New-Object System.Text.StringBuilder
    $deadline = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $deadline) {
        if ($ns.DataAvailable) {
            $buf = New-Object byte[] 4096
            $n = $ns.Read($buf, 0, $buf.Length)
            if ($n -gt 0) { [void]$sb.Append($enc.GetString($buf, 0, $n)) }
        } else { Start-Sleep -Milliseconds 15 }
    }
    return $sb.ToString()
}
function Oocd([string]$c, [int]$ms = 600) {
    $bytes = $enc.GetBytes($c + "`n")
    $ns.Write($bytes, 0, $bytes.Length)
    $ns.Flush()
    return OocdRead $ms
}
[void](OocdRead 400)   # drain banner

# --- send the console command to trigger the fault -------------------------
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.Handshake = [System.IO.Ports.Handshake]::None
$sp.RtsEnable = $false; $sp.DtrEnable = $false
$sp.Open()
$consoleSb = New-Object System.Text.StringBuilder
try {
    $sp.Write($Cmd + "`r")
    $end = (Get-Date).AddMilliseconds(900)
    while ((Get-Date) -lt $end) {
        if ($sp.BytesToRead -gt 0) { [void]$consoleSb.Append($sp.ReadExisting()) } else { Start-Sleep -Milliseconds 15 }
    }
} finally { $sp.Close(); $sp.Dispose() }

Write-Host "===== CONSOLE after '$Cmd' ====="
Write-Host $consoleSb.ToString()

# --- halt and inspect ------------------------------------------------------
Write-Host "===== OPENOCD halt / registers ====="
Write-Host (Oocd 'halt' 800)
Write-Host (Oocd 'reg pc' 400)
Write-Host (Oocd 'reg lr' 400)
Write-Host (Oocd 'reg sp' 400)
$mspOut = Oocd 'reg msp' 400; Write-Host $mspOut
$pspOut = Oocd 'reg psp' 400; Write-Host $pspOut
Write-Host (Oocd 'reg xPSR' 400)
function HexAddr([string]$s) { if ($s -match '0x[0-9a-fA-F]{8}') { return $Matches[0] } return $null }
$mspA = HexAddr $mspOut
$pspA = HexAddr $pspOut
# Secure + Non-secure fault status
Write-Host "--- CFSR_S=0xE000ED28 HFSR_S=0xE000ED2C SFSR=0xE000EDE4 CFSR_NS=0xE002ED28 ---"
Write-Host (Oocd 'mdw 0xE000ED28 1' 400)
Write-Host (Oocd 'mdw 0xE000ED2C 1' 400)
Write-Host (Oocd 'mdw 0xE000EDE4 1' 400)
Write-Host (Oocd 'mdw 0xE002ED28 1' 400)
Write-Host (Oocd 'mdw 0xE002ED2C 1' 400)
# Stack dumps (find stacked PC at frame+0x18)
if ($mspA) { Write-Host "--- stack @ MSP $mspA (frame: R0,R1,R2,R3,R12,LR,PC,xPSR) ---"; Write-Host (Oocd ("mdw $mspA 16") 500) }
if ($pspA) { Write-Host "--- stack @ PSP $pspA ---"; Write-Host (Oocd ("mdw $pspA 16") 500) }
Oocd 'resume' 300 | Out-Null
$ns.Close(); $tcp.Close()
