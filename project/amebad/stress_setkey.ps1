#requires -version 5
<#
stress_setkey.ps1 - hammer the AmebaD AWS `set key` console command to characterize the runtime
TF-M ITS write path (it has shown an intermittent "stored a bad key" race). Each iteration sends
the real key's base64 body as one paced CLI line and classifies the device's result line:

  set key: <N> b64 -> OK     its_store wrote + read-back-verified the DER  (good)
  set key: <N> b64 -> FAIL   base64 decode or psa_its_set / verify failed  (bad)
  <none within timeout>      device wedged on the secure flash write       (hang)

Run while the device is CONNECTED to also stress the ITS write against live TLS/MQTT crypto (the
running aws task keeps using its cached key, so the connection is undisturbed).

Usage (from WSL):
  pwsh project/amebad/stress_setkey.ps1 -Port COM3 -Key build/aws-bw20/key.pem -N 50
#>
[CmdletBinding()]
param(
    [string]$Port = 'COM3',
    [int]   $Baud = 1500000,
    [int]   $N    = 50,
    [string]$Key  = 'build/aws-bw20/key.pem',
    [int]   $ResultTimeoutSec = 10
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/serial_common.ps1"

$Key = (Resolve-Path $Key).Path
$b64 = (Get-Content $Key | Where-Object { $_ -notmatch '^\s*-----' -and $_.Trim() -ne '' }) -join ''
Write-Host "[stress] key = $Key  ($($b64.Length) base64 chars)  iterations = $N"

# Read raw bytes until the `set key: ... -> OK|FAIL` result appears, or timeout. Raw (not ReadLine)
# because the app log uses LF-CR and interleaves [AWS] TX telemetry lines.
function Wait-Result([System.IO.Ports.SerialPort]$sp, [int]$sec) {
    $buf = ''
    $deadline = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $deadline) {
        $buf += $sp.ReadExisting()
        if ($buf -match 'set key:\s*\d+\s*b64\s*->\s*(OK|FAIL)') { return $Matches[1] }
        Start-Sleep -Milliseconds 25
    }
    return 'HANG'
}

$sp = Open-Serial $Port $Baud 200 3000
$ok = 0; $fail = 0; $hang = 0
try {
    $sp.WriteLine(''); Start-Sleep -Milliseconds 300; [void]$sp.ReadExisting()   # nudge a prompt
    for ($i = 1; $i -le $N; $i++) {
        [void](Send-Paced $sp "set key $b64")
        $r = Wait-Result $sp $ResultTimeoutSec
        switch ($r) {
            'OK'   { $ok++ }
            'FAIL' { $fail++ }
            default { $hang++ }
        }
        $rate = [math]::Round(100.0 * $ok / $i, 1)
        Write-Host ("[{0,3}/{1}] {2,-4}   running: OK={3} FAIL={4} HANG={5}  ({6}% ok)" -f $i, $N, $r, $ok, $fail, $hang, $rate)
        if ($r -eq 'HANG') {
            Write-Host "[stress] no result within ${ResultTimeoutSec}s - device likely wedged on the secure write; stopping."
            break
        }
        Start-Sleep -Milliseconds 200   # let the CLI settle before the next line
    }
}
finally { $sp.Close(); $sp.Dispose() }
Write-Host ""
Write-Host ("[stress] DONE: {0} OK / {1} FAIL / {2} HANG out of {3}" -f $ok, $fail, $hang, ($ok + $fail + $hang))
