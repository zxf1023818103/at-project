$ErrorActionPreference = 'Continue'
$tool = 'D:\MCU_Tools\bl602_flash_download_tool\bflb_iot_tool.exe'
$pt   = 'D:\MCU_Tools\bflb-cklink\fw_bl602\partition_cfg_4M.toml'
$fw   = 'D:\MCU_Tools\bflb-cklink\fw_bl602\at_bl602.bin'
$outfile = 'C:\Users\Public\bl602_ptfwflash.txt'
Remove-Item $outfile,"$outfile.err" -ErrorAction SilentlyContinue

function Reset-Module {
    try {
        $p = New-Object System.IO.Ports.SerialPort('COM19',2000000,'None',8,'One')
        $p.Open(); $p.DtrEnable=$false
        $p.RtsEnable=$true; Start-Sleep -Milliseconds 200; $p.RtsEnable=$false
        $p.Close(); Write-Host '  [COM19 RTS reset pulsed]'
    } catch { Write-Host ('  [RTS FAIL: ' + $_.Exception.Message + ']') }
}

# NO --single: flashes ONLY partition table + firmware at their offsets -> LittleFS @0x300000 untouched
$a = "--interface uart --port COM18 --pt $pt --firmware $fw --chipname bl602 --baudrate 921600"
$proc = Start-Process -FilePath $tool -ArgumentList $a -WorkingDirectory 'D:\MCU_Tools\bl602_flash_download_tool' `
        -RedirectStandardOutput $outfile -RedirectStandardError "$outfile.err" -PassThru -WindowStyle Hidden

$reset = $false
for ($i=0; $i -lt 400; $i++) {
    Start-Sleep -Milliseconds 100
    $c = Get-Content $outfile -Raw -ErrorAction SilentlyContinue
    if (-not $reset -and ($c -match 'Press Reset|press reset|Reset Key')) {
        Start-Sleep -Milliseconds 300; Reset-Module; $reset = $true
    }
    if ($c -match 'Program Finished|All Success|\[All Success\]') { Start-Sleep -Milliseconds 500; break }
    if ($c -match 'Program Fail|Verify fail|Fail Error|Traceback') { Start-Sleep -Milliseconds 500; break }
    if ($proc.HasExited) { break }
}
if (-not $proc.HasExited) { Start-Sleep -Seconds 8; if (-not $proc.HasExited) { $proc.Kill() } }

Write-Host '=== flash tail ==='
Get-Content $outfile -ErrorAction SilentlyContinue | Select-Object -Last 18 | ForEach-Object { $_.ToString() }
