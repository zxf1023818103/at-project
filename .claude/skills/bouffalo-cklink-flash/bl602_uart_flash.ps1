# bl602 UART-ISP flash (COM18) + synced COM19-RTS reset + post-flash boot-log capture.
#   -Mode whole : --single whole_flash_data.bin   (wipes LittleFS/KV)
#   -Mode ptfw  : --pt + --firmware at_bl602.bin   (PRESERVES LittleFS/KV)
# On the tool's "Please Press Reset Key!" it resets COM19 IMMEDIATELY (no fixed wait).
# After [All Success] it resets again and records the boot log to $BootLog for the caller to read.
param(
    [ValidateSet('whole','ptfw')][string]$Mode = 'whole',
    [string]$FwDir   = 'D:\MCU_Tools\bflb-cklink\fw_bl602',
    [string]$FlashPort = 'COM18',
    [string]$LogPort   = 'COM19',
    [int]   $LogBaud   = 2000000,
    [int]   $LogSecs   = 20
)
$ErrorActionPreference = 'Continue'
$tool    = 'D:\MCU_Tools\bl602_flash_download_tool\bflb_iot_tool.exe'
$outfile = 'C:\Users\Public\bl602_flashout.txt'
$BootLog = 'C:\Users\Public\bl602_bootlog.txt'
Remove-Item $outfile,"$outfile.err" -ErrorAction SilentlyContinue

if ($Mode -eq 'whole') {
    $a = "--interface uart --port $FlashPort --single --firmware $FwDir\at_bl602_whole_flash_data.bin --chipname bl602 --baudrate 921600"
} else {
    $a = "--interface uart --port $FlashPort --pt $FwDir\partition_cfg_4M.toml --firmware $FwDir\at_bl602.bin --chipname bl602 --baudrate 921600"
}

# COM19 RTS reset = pull-down (assert) then pull-up (release); optionally hold the port open and
# read the boot log into $BootLog.
function Reset-Module { param([int]$CaptureSecs = 0)
    try {
        $p = New-Object System.IO.Ports.SerialPort($LogPort,$LogBaud,'None',8,'One'); $p.ReadTimeout=300
        $p.Open(); $p.DtrEnable=$false
        $p.RtsEnable=$true; Start-Sleep -Milliseconds 200; $p.RtsEnable=$false
        if ($CaptureSecs -gt 0) {
            $sw=[System.Diagnostics.Stopwatch]::StartNew(); $buf=''
            while($sw.Elapsed.TotalSeconds -lt $CaptureSecs){
                try{ $buf += $p.ReadExisting() }catch{}; Start-Sleep -Milliseconds 60
                if($buf -match 'GOT_IP|network UP|IPOK'){ Start-Sleep -Seconds 2; break }
            }
            $buf | Out-File $BootLog -Encoding ASCII
        }
        $p.Close()
    } catch { Write-Host ('  [reset/capture FAIL: ' + $_.Exception.Message + ']') }
}

$proc = Start-Process -FilePath $tool -ArgumentList $a -WorkingDirectory (Split-Path $tool) `
        -RedirectStandardOutput $outfile -RedirectStandardError "$outfile.err" -PassThru -WindowStyle Hidden

$reset=$false; $ok=$false
for ($i=0; $i -lt 600; $i++) {
    Start-Sleep -Milliseconds 50
    $c = Get-Content $outfile -Raw -ErrorAction SilentlyContinue
    if (-not $reset -and ($c -match 'Press Reset|press reset|Reset Key')) { Reset-Module; $reset=$true }  # IMMEDIATE
    if ($c -match 'Program Finished|\[All Success\]') { $ok=$true; Start-Sleep -Milliseconds 300; break }
    if ($c -match 'Program Fail|Verify fail|Traceback') { break }
    if ($proc.HasExited) { Start-Sleep -Milliseconds 200; $c = Get-Content $outfile -Raw -ErrorAction SilentlyContinue; if ($c -match 'All Success|Program Finished'){$ok=$true}; break }
}
if (-not $proc.HasExited) { Start-Sleep -Seconds 6; if (-not $proc.HasExited) { $proc.Kill() } }

Write-Host '=== flash tail ==='
Get-Content $outfile -ErrorAction SilentlyContinue | Select-String -Pattern 'Sha caled|Verify|Program Finished|All Success|Fail|ErrorCode' | Select-Object -Last 8 | ForEach-Object { $_.ToString() }

if ($ok) {
    Write-Host "=== flash OK -> reset + capture boot log ($LogSecs s) -> $BootLog ==="
    Reset-Module -CaptureSecs $LogSecs
    Write-Host "boot log saved: $BootLog"
    if (Test-Path $BootLog) { Get-Content $BootLog | Select-Object -Last 6 | ForEach-Object { $_.TrimEnd() } }
} else {
    Write-Host '=== flash did NOT report success; skipping boot-log capture ==='
}
