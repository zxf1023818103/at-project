#requires -version 5
# Reset -> wait for a good boot (MQTT connected) -> send `set key <b64>` -> confirm result.
param(
    [string]$KeyFile = '\\wsl.localhost\Ubuntu\home\zxf10\at-project\build\aws-bw20\key.pem',
    [int]$Tries = 12,
    [double]$BootWait = 22
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../tools/serial_common.ps1"
$b64 = (Get-Content $KeyFile | Where-Object { $_ -notmatch '^\s*-----' -and $_.Trim() -ne '' }) -join ''
Write-Host "[prov] key base64 body = $($b64.Length) chars"

for ($i = 1; $i -le $Tries; $i++) {
    $c = New-Object System.IO.Ports.SerialPort COM3,1500000,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $c.RtsEnable = $false; $c.DtrEnable = $false; $c.Open()
    $b = New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $b.Open(); $b.RtsEnable = $true; Start-Sleep -Milliseconds 120; $b.RtsEnable = $false; $b.Close()

    $sb = New-Object System.Text.StringBuilder
    $e = (Get-Date).AddSeconds($BootWait)
    $ready = $false
    while ((Get-Date) -lt $e) {
        if ($c.BytesToRead -gt 0) { [void]$sb.Append($c.ReadExisting()) } else { Start-Sleep -Milliseconds 15 }
        if ($sb.ToString() -match 'MQTT connected') { $ready = $true; break }
    }
    if (-not $ready) { Write-Host "[try $i] no good boot (hang); retrying"; $c.Close(); continue }

    Write-Host "[try $i] good boot, CLI ready -> sending set key ($($b64.Length) chars)"
    Start-Sleep -Milliseconds 400
    [void](Send-Paced $c "set key $b64")
    # read result
    $res = New-Object System.Text.StringBuilder
    $e2 = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $e2) {
        if ($c.BytesToRead -gt 0) { [void]$res.Append($c.ReadExisting()) } else { Start-Sleep -Milliseconds 15 }
        if ($res.ToString() -match 'set key:') { Start-Sleep -Milliseconds 300; break }
    }
    $c.Close()
    Write-Host "===== set key result ====="
    foreach ($line in ($res.ToString() -split "`n")) {
        if ($line -match 'set key|AWS|FAIL|OK') { Write-Host ("  " + $line.Trim()) }
    }
    return
}
Write-Host "no good boot in $Tries tries — could not provision"
