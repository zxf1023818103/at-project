$ErrorActionPreference = 'Continue'
$tool = 'D:\MCU_Tools\bl602_flash_download_tool\bflb_iot_tool.exe'
$fw   = 'D:\MCU_Tools\bflb-cklink\fw_bl602\at_bl602_whole_flash_data.bin'
$outfile = 'C:\Users\Public\bl602_uartflash.txt'
Remove-Item $outfile,"$outfile.err" -ErrorAction SilentlyContinue

function Reset-Module {
    # COM19 RTS: pull down (assert reset) then pull up (release) -> module reboots
    try {
        $p = New-Object System.IO.Ports.SerialPort('COM19',2000000,'None',8,'One')
        $p.Open(); $p.DtrEnable=$false
        $p.RtsEnable=$true; Start-Sleep -Milliseconds 200; $p.RtsEnable=$false
        $p.Close(); Write-Host '  [COM19 RTS reset pulsed]'
    } catch { Write-Host ('  [RTS FAIL: ' + $_.Exception.Message + ']') }
}

$a = "--interface uart --port COM18 --single --firmware $fw --chipname bl602 --baudrate 921600"
$proc = Start-Process -FilePath $tool -ArgumentList $a -WorkingDirectory 'D:\MCU_Tools\bl602_flash_download_tool' `
        -RedirectStandardOutput $outfile -RedirectStandardError "$outfile.err" -PassThru -WindowStyle Hidden

$reset = $false
for ($i=0; $i -lt 300; $i++) {          # up to ~30s
    Start-Sleep -Milliseconds 100
    $c = Get-Content $outfile -Raw -ErrorAction SilentlyContinue
    if (-not $reset -and ($c -match 'Press Reset|press reset|Reset Key')) {
        Start-Sleep -Milliseconds 300
        Reset-Module
        $reset = $true
    }
    if ($c -match 'Program Finished|All Success|Success|Verify success|Fail|Error|error') { Start-Sleep -Milliseconds 500; break }
    if ($proc.HasExited) { break }
}
if (-not $proc.HasExited) { Start-Sleep -Seconds 8; if (-not $proc.HasExited) { $proc.Kill() } }

Write-Host '=== flash tail ==='
Get-Content $outfile -ErrorAction SilentlyContinue | Select-Object -Last 20 | ForEach-Object { $_.ToString() }
if (Test-Path "$outfile.err") { $e = Get-Content "$outfile.err" -ErrorAction SilentlyContinue; if ($e) { Write-Host '=== stderr ==='; $e | Select-Object -Last 8 } }
