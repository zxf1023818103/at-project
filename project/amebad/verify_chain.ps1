#requires -version 5
param([int]$Tries = 10, [double]$Wait = 22)
$ErrorActionPreference = 'Stop'
for ($i = 1; $i -le $Tries; $i++) {
    $c = New-Object System.IO.Ports.SerialPort COM3,1500000,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $c.RtsEnable = $false; $c.DtrEnable = $false; $c.Open()
    $b = New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $b.Open(); $b.RtsEnable = $true; Start-Sleep -Milliseconds 120; $b.RtsEnable = $false; $b.Close()
    $sb = New-Object System.Text.StringBuilder
    $e = (Get-Date).AddSeconds($Wait)
    $connected = $false
    while ((Get-Date) -lt $e) {
        if ($c.BytesToRead -gt 0) { [void]$sb.Append($c.ReadExisting()) } else { Start-Sleep -Milliseconds 15 }
        if (-not $connected -and $sb.ToString() -match 'MQTT connected') { $connected = $true; Start-Sleep -Milliseconds 500; break }
    }
    $c.Close()
    $t = $sb.ToString()
    if ($t -match 'at-project image2') {
        Write-Host "===== KM4-OK boot on try $i ====="
        foreach ($line in ($t -split "`n")) {
            if ($line -match '\[AWS\]|secure storage|embedded fallback|MQTT connected|pk_parse|key from|TLS|FAIL') {
                Write-Host ("  " + $line.Trim())
            }
        }
        return
    }
    Write-Host "[try $i] hang"
}
Write-Host "no KM4-OK boot in $Tries tries"
