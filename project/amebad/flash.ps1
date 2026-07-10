#requires -version 5
<#
通过 COM10 进入 Download Mode，再用 Ameba_1-N_MP_ImageTool 在 COM25 上烧 Image_All.bin。

接线 (见 doc/ameba_image_download.md):
  COM10 RTS -> CHIP_EN, COM10 DTR -> LOG_RX  (仅做时序控制)
  COM25 TXD -> LOG_TX,  COM25 RXD -> LOG_RX  (烧录数据)
#>

[CmdletBinding()]
param(
    [string]$BootPort  = 'COM10',
    [string]$FlashPort = 'COM25',
    [string]$Chip      = 'AmebaD',
    [int]   $Baudrate  = 1500000,
    [string]$Image,
    [string]$ToolDir,
    [switch]$SkipModeCheck,
    [switch]$Erase,         # -e true: full chip erase before write (wipes TF-M ITS/PS at 0x3F0000,
                            # which sits above the image so a normal flash leaves it stale/corrupt)

    # The app write-protects the firmware region (bottom 3 MB) at boot, so the ImageTool can't
    # overwrite image2 while it's locked (verify FAIL). By default we first send `flash unlock` over
    # the console ($FlashPort @ $ConsoleBaud) so the region is writable when we drop into download mode
    # (download mode doesn't re-run the app, so the SR stays unlocked). Best-effort: if the app isn't
    # running (already halted / bricked) the send just times out and we continue. -NoUnlock skips it.
    [switch]$NoUnlock,
    [int]   $ConsoleBaud = 1500000,

    # Post-flash: after the reset the script records the boot log to $BootLog (read it to analyze the
    # boot instead of re-capturing the one-shot burst). $LogSecs = how long to listen (breaks early on
    # network-up). -NoBootLog skips it.
    [string]$BootLog = 'C:\Users\Public\amebad_bootlog.txt',
    [int]   $LogSecs = 15,
    [switch]$NoBootLog,

    # AWS IoT provisioning (AWS=ON image): after the post-flash reset, drive the sibling
    # aws_provision.ps1 over the console ($FlashPort) to provision credentials by their storage:
    #   -Endpoint <ats-host>  -> env var "endpoint" (plain LittleFS KV/endpoint)
    #   -Cert <cert.pem>      -> plain LittleFS DER file (aws_cert.der); path = env var "cert"
    #   -Key  <key.pem>       -> TF-M Internal Trusted Storage (the only secret)
    # Any subset may be given; omit all three to skip provisioning. The device idle-retries until
    # provisioned, so it connects without a second flash.
    [string]$Endpoint,
    [string]$Cert,
    [string]$Key
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot 在 param 默认值里被某些 host 提前展开为空，挪到这里再补默认值。
$here = $PSScriptRoot
if (-not $here) { $here = Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $Image)   { $Image   = Join-Path $here 'out\Image_All.bin' }
if (-not $ToolDir) { $ToolDir = Join-Path $here '..\..\platform\ameba-rtos\tools\ameba\MP_ImageTool_1_N' }

function Enter-DownloadMode {
    param([string]$Port)
    Write-Host "[boot] open $Port to drive CHIP_EN/LOG_TX ..."
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.Handshake = [System.IO.Ports.Handshake]::None
    # 默认 RTS/DTR 在 .NET 里是 false=拉高(逻辑)，但物理上 RS232 false→负电平→晶体管拉低 CHIP_EN。
    # 这里跟着 doc 的语义直接做：Enable=true 表示"拉低"输出引脚（适配本板上的 NPN 反相）。
    $sp.RtsEnable = $false
    $sp.DtrEnable = $false
    $sp.Open()
    try {
        # 1) CHIP_EN 低 (RTS 低) -> 关芯片
        Write-Host "[boot] CHIP_EN low"
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 50
        # 2) LOG_TX 低 (DTR 低)
        Write-Host "[boot] LOG_TX low"
        $sp.DtrEnable = $true
        Start-Sleep -Milliseconds 50
        # 3) CHIP_EN 高 (RTS 高) -> 上电，此时采样到 LOG_TX 低 -> 进入下载模式
        Write-Host "[boot] CHIP_EN high (power on into download mode)"
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 100
        # 4) LOG_TX 释放 (DTR 高)
        Write-Host "[boot] release LOG_TX"
        $sp.DtrEnable = $false
        Start-Sleep -Milliseconds 50
    }
    finally {
        $sp.Close()
        $sp.Dispose()
    }
}

function Test-DownloadMode {
    param([string]$Port)
    Write-Host "[chk] sniff $Port @ 115200 for 0x15 ..."
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $sp.ReadTimeout = 500
    # 不要碰 RTS/DTR (doc 警告: 别下拉 COM25 的 RTS/DTR)
    $sp.RtsEnable = $false
    $sp.DtrEnable = $false
    $sp.Open()
    try {
        # Break the MOMENT 0x15 arrives (the download-mode ready byte) -> flash starts immediately,
        # no waiting out the full 2 s / 16-byte budget.
        $deadline = (Get-Date).AddSeconds(2)
        $bytes = New-Object 'System.Collections.Generic.List[byte]'
        $got15 = $false
        while ((Get-Date) -lt $deadline) {
            try {
                $b = $sp.ReadByte()
                if ($b -ge 0) { $bytes.Add([byte]$b); if ($b -eq 0x15) { $got15 = $true; break } }
            } catch [TimeoutException] { }
        }
        if ($bytes.Count -eq 0) {
            Write-Warning "[chk] no bytes on $Port — chip might not be in download mode"
            return $false
        }
        $hex = ($bytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
        Write-Host "[chk] got: $hex  (0x15=$got15)"
        return $got15
    }
    finally {
        $sp.Close()
        $sp.Dispose()
    }
}

# --- 实际流程 ---
Write-Host "PSScriptRoot=$PSScriptRoot"
Write-Host "Image(raw)  =$Image"
Write-Host "ToolDir(raw)=$ToolDir"
function Resolve-WslPath {
    param([string]$p)
    $p = $p -replace '/', '\'
    if ($p.StartsWith('\\')) {
        # UNC: \\server\share\rest...  preserve \\server\share, normalize rest
        $stripped = $p.Substring(2)
        $parts = $stripped.Split('\')
        if ($parts.Length -lt 2) { return $p }
        $head = '\\' + $parts[0] + '\' + $parts[1]
        $tail = @()
        if ($parts.Length -ge 3) { $tail = $parts[2..($parts.Length - 1)] }
    } else {
        $head = ''
        $tail = $p.Split('\')
    }
    $out = New-Object 'System.Collections.Generic.List[string]'
    foreach ($s in $tail) {
        if ($s -eq '' -or $s -eq '.') { continue }
        if ($s -eq '..') {
            if ($out.Count -gt 0) { $out.RemoveAt($out.Count - 1) }
            continue
        }
        $out.Add($s) | Out-Null
    }
    if ($head) { return $head + '\' + ($out -join '\') }
    return ($out -join '\')
}
$Image   = Resolve-WslPath $Image
$ToolDir = Resolve-WslPath $ToolDir
Write-Host "Image       =$Image"
Write-Host "ToolDir     =$ToolDir"
if (-not (Test-Path $Image)) { throw "image not found: $Image" }
$exe = Join-Path $ToolDir 'Ameba_1-N_MP_ImageTool.exe'
if (-not (Test-Path $exe)) { throw "tool not found: $exe" }

Write-Host "image : $Image"
Write-Host "tool  : $exe"
Write-Host "chip  : $Chip"
Write-Host "boot  : $BootPort"
Write-Host "flash : $FlashPort @ $Baudrate"

# Pre-flash: unlock the firmware-region write-protect over the console, so the ImageTool can overwrite
# image2 (the app re-locks the bottom 3 MB every boot). Best-effort; a running AT-project app answers
# with "flash unlock: ... OK". No app / already halted -> times out and we continue.
if (-not $NoUnlock) {
    Write-Host "[unlock] flash unlock on $FlashPort @ $ConsoleBaud (pre-flash; -NoUnlock to skip) ..."
    try {
        $usp = New-Object System.IO.Ports.SerialPort $FlashPort, $ConsoleBaud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
        $usp.ReadTimeout = 400
        $usp.Open()
        Start-Sleep -Milliseconds 200
        $usp.DiscardInBuffer()
        foreach ($ch in 'flash unlock'.ToCharArray()) { $usp.Write([string]$ch); Start-Sleep -Milliseconds 8 }
        $usp.Write("`r")
        $usb = New-Object System.Text.StringBuilder
        $udl = (Get-Date).AddSeconds(2)
        while ((Get-Date) -lt $udl) { try { $uc = $usp.ReadExisting(); if ($uc) { [void]$usb.Append($uc) } } catch {} Start-Sleep -Milliseconds 50 }
        $usp.Close()
        $uresp = ($usb.ToString() -replace '\s+', ' ').Trim()
        if ($uresp -match 'unlock') { Write-Host "[unlock] $uresp" }
        else { Write-Host "[unlock] no response (app not running / already in download mode) - continuing" }
    } catch {
        Write-Host "[unlock] skipped ($($_.Exception.Message))"
    }
}

Enter-DownloadMode -Port $BootPort

if (-not $SkipModeCheck) {
    if (-not (Test-DownloadMode -Port $FlashPort)) {
        Write-Warning "未检测到 0x15，重试一次进入 download mode..."
        Enter-DownloadMode -Port $BootPort
        if (-not (Test-DownloadMode -Port $FlashPort)) {
            throw "chip refused to enter download mode after 2 tries"
        }
    }
}

# 0x15 confirmed -> launch the ImageTool IMMEDIATELY (no redundant re-enter, saves time). Safety
# net: if the first attempt fails/incomplete (e.g. the sniff's COM close knocked the chip out of
# download mode on some boards), re-enter download mode ONCE and retry.
$eraseArg = if ($Erase) { 'true' } else { 'false' }
if ($Erase) { Write-Host "[flash] CHIP ERASE enabled (-e true): wipes whole flash incl. ITS/PS" }
function Invoke-ImageTool {
    Push-Location $ToolDir
    try {
        $out = & .\Ameba_1-N_MP_ImageTool.exe -c $Chip -i $Image -b $Baudrate -e $eraseArg -g false -v true -s true -d $FlashPort 2>&1
        return @{ rc = $LASTEXITCODE; out = $out }
    } finally { Pop-Location }
}
Write-Host "[flash] launching ImageTool ..."
$res = Invoke-ImageTool
if ($res.rc -ne 0 -or ($res.out -match 'FAIL') -or -not ($res.out -match 'PASS|Download Complete')) {
    Write-Warning "[flash] first attempt failed/incomplete -> re-enter download mode + retry once"
    Enter-DownloadMode -Port $BootPort
    $res = Invoke-ImageTool
}
$toolOut = $res.out
$rc      = $res.rc
$toolOut | ForEach-Object { Write-Host "  $_" }
if ($rc -ne 0) { throw "ImageTool exited with code $rc" }
if ($toolOut -match 'FAIL') { throw "ImageTool reported FAIL" }
if (-not ($toolOut -match 'PASS|Download Complete')) {
    Write-Warning "ImageTool output missing PASS marker"
}

# 烧完后用 $BootPort 复位芯片让 app 跑起来，并把启动日志录到 $BootLog 供直接读取分析。
# 先开好 console ($FlashPort @ $ConsoleBaud，RTS/DTR 保持 false 不扰动 CHIP_EN)，再拨 $BootPort 复位，
# 这样能抓到复位后的整段启动 burst。
Write-Host "[post] reset chip via $BootPort ..."
$clog = $null
if (-not $NoBootLog) {
    try {
        $clog = New-Object System.IO.Ports.SerialPort $FlashPort, $ConsoleBaud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
        $clog.ReadTimeout = 300; $clog.RtsEnable = $false; $clog.DtrEnable = $false
        $clog.Open()
    } catch { Write-Host "[post] console open failed ($($_.Exception.Message)) - skipping boot log"; $clog = $null }
}
$sp = New-Object System.IO.Ports.SerialPort $BootPort, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.Open()
try {
    $sp.RtsEnable = $true   # CHIP_EN low
    Start-Sleep -Milliseconds 100
    $sp.RtsEnable = $false  # release -> chip boots normally (LOG_TX not held low)
    if ($clog) {
        Write-Host "[post] capturing boot log ($FlashPort @ $ConsoleBaud, up to ${LogSecs}s) -> $BootLog ..."
        $sw = [System.Diagnostics.Stopwatch]::StartNew(); $buf = ''
        while ($sw.Elapsed.TotalSeconds -lt $LogSecs) {
            try { $buf += $clog.ReadExisting() } catch {}
            Start-Sleep -Milliseconds 60
            if ($buf -match 'network UP|GOT_IP|IPOK|====\s*network') { Start-Sleep -Seconds 2; break }
        }
        $buf | Out-File $BootLog -Encoding ASCII
        Write-Host "[post] boot log saved: $BootLog"
    }
}
finally {
    $sp.Close(); $sp.Dispose()
    if ($clog) { $clog.Close(); $clog.Dispose() }
}

Write-Host "[done] flash success"

# AWS IoT provisioning: if any credential was given, drive the console `set ...` commands over
# $FlashPort (which is also the LOGUART console after boot). The sibling aws_provision.ps1 waits
# for boot/WiFi/AWS to settle, then sends `set endpoint/cert/key`. The device persists endpoint +
# cert to LittleFS and the key to TF-M ITS, so subsequent boots reconnect with no re-flash.
if ($Endpoint -or $Cert -or $Key) {
    $prov = Join-Path $here 'aws_provision.ps1'
    if (-not (Test-Path $prov)) { throw "aws_provision.ps1 not found: $prov" }
    Write-Host "[prov] provisioning AWS creds over $FlashPort @ $Baudrate ..."
    $pargs = @{ Port = $FlashPort; Baud = $Baudrate }
    if ($Endpoint) { $pargs.Endpoint = $Endpoint }
    if ($Cert)     { $pargs.Cert     = $Cert }
    if ($Key)      { $pargs.Key      = $Key }
    & $prov @pargs
    Write-Host "[prov] provisioning step done"
}
