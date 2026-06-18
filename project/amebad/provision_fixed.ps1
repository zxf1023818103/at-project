#requires -version 5
# Reset, wait a fixed time for the CLI, send `set key <b64>` paced. No MQTT dependency.
param([string]$KeyFile='\\wsl.localhost\Ubuntu\home\zxf10\at-project\build\aws-bw20\key.pem',[double]$W=16)
$ErrorActionPreference='Stop'
$b64=(Get-Content $KeyFile | Where-Object { $_ -notmatch '^\s*-----' -and $_.Trim() -ne '' }) -join ''
$c=New-Object System.IO.Ports.SerialPort COM3,1500000,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$c.RtsEnable=$false; $c.DtrEnable=$false; $c.Open()
$b=New-Object System.IO.Ports.SerialPort COM14,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$b.Open(); $b.RtsEnable=$true; Start-Sleep -Milliseconds 120; $b.RtsEnable=$false; $b.Close()
$sb=New-Object System.Text.StringBuilder
$e=(Get-Date).AddSeconds($W); while((Get-Date) -lt $e){ if($c.BytesToRead -gt 0){[void]$sb.Append($c.ReadExisting())}else{Start-Sleep -Milliseconds 15} }
Start-Sleep -Milliseconds 300; $c.DiscardInBuffer()
$bytes=[System.Text.Encoding]::ASCII.GetBytes("set key $b64`r")
for($k=0;$k -lt $bytes.Length;$k+=16){ $n=[Math]::Min(16,$bytes.Length-$k); $c.Write($bytes,$k,$n); Start-Sleep -Milliseconds 6 }
$r=New-Object System.Text.StringBuilder; $e2=(Get-Date).AddSeconds(6)
while((Get-Date) -lt $e2){ if($c.BytesToRead -gt 0){[void]$r.Append($c.ReadExisting())}else{Start-Sleep -Milliseconds 15}; if($r.ToString() -match 'set key:'){Start-Sleep -Milliseconds 300; break} }
$c.Close()
foreach($line in ($r.ToString() -split "`n")){ if($line -match 'set key|der_len|AWS'){ Write-Host $line.Trim() } }
