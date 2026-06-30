#requires -version 5
param([int]$Tries = 8, [double]$Wait = 9)
$ErrorActionPreference = 'Stop'
for ($i = 1; $i -le $Tries; $i++) {
    $c = New-Object System.IO.Ports.SerialPort COM3,1500000,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $c.RtsEnable = $false; $c.DtrEnable = $false; $c.Open()
    $b = New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $b.Open(); $b.RtsEnable = $true; Start-Sleep -Milliseconds 120; $b.RtsEnable = $false; $b.Close()
    $sb = New-Object System.Text.StringBuilder
    $e = (Get-Date).AddSeconds($Wait)
    while ((Get-Date) -lt $e) { if ($c.BytesToRead -gt 0) { [void]$sb.Append($c.ReadExisting()) } else { Start-Sleep -Milliseconds 15 } }
    $c.Close()
    $t = $sb.ToString()
    if ($t -match 'at-project image2') {
        Write-Host "===== KM4-OK boot on try $i ====="
        Write-Host $t
        return
    }
    Write-Host "[try $i] hang (bytes=$($t.Length)) last='$(($t -split "`n" | Where-Object {$_.Trim() -ne ''} | Select-Object -Last 1))'"
}
Write-Host "no KM4-OK boot in $Tries tries"
