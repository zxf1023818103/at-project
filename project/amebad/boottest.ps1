#requires -version 5
param([int]$N = 5, [double]$Wait = 9)
$ErrorActionPreference = 'Stop'
for ($i = 1; $i -le $N; $i++) {
    $c = New-Object System.IO.Ports.SerialPort COM3,1500000,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $c.RtsEnable = $false; $c.DtrEnable = $false; $c.Open()
    $b = New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $b.Open(); $b.RtsEnable = $true; Start-Sleep -Milliseconds 120; $b.RtsEnable = $false; $b.Close()
    $sb = New-Object System.Text.StringBuilder
    $e = (Get-Date).AddSeconds($Wait)
    while ((Get-Date) -lt $e) { if ($c.BytesToRead -gt 0) { [void]$sb.Append($c.ReadExisting()) } else { Start-Sleep -Milliseconds 15 } }
    $c.Close()
    $t = $sb.ToString()
    $km4 = if ($t -match 'at-project image2') { 'KM4-OK' } else { 'KM4-HANG' }
    $hf  = if ($t -match 'Hard Fault') { 'HardFault' } else { '' }
    $dhcp = if ($t -match 'DHCP IP') { 'DHCP' } else { '' }
    $last = ($t -split "`n" | Where-Object { $_.Trim() -ne '' } | Select-Object -Last 1)
    Write-Host ("[boot {0}] {1} {2} {3} bytes={4} last='{5}'" -f $i, $km4, $hf, $dhcp, $t.Length, $last.Trim())
}
