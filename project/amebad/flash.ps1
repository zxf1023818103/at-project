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
    [switch]$SkipModeCheck
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
        $deadline = (Get-Date).AddSeconds(2)
        $bytes = New-Object 'System.Collections.Generic.List[byte]'
        while ((Get-Date) -lt $deadline -and $bytes.Count -lt 16) {
            try {
                $b = $sp.ReadByte()
                if ($b -ge 0) { $bytes.Add([byte]$b) }
            } catch [TimeoutException] { }
        }
        if ($bytes.Count -eq 0) {
            Write-Warning "[chk] no bytes on $Port — chip might not be in download mode"
            return $false
        }
        $hex = ($bytes | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
        Write-Host "[chk] got: $hex"
        return ($bytes -contains 0x15)
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

# sniff 之后 COM25 关闭会拨动 RTS/DTR -> 芯片可能复位掉出 download mode；这里再进一次保险
Enter-DownloadMode -Port $BootPort

Write-Host "[flash] launching ImageTool ..."
Push-Location $ToolDir
try {
    $toolOut = & .\Ameba_1-N_MP_ImageTool.exe `
        -c $Chip `
        -i $Image `
        -b $Baudrate `
        -g false `
        -v true `
        -s true `
        -d $FlashPort 2>&1
    $rc = $LASTEXITCODE
}
finally {
    Pop-Location
}
$toolOut | ForEach-Object { Write-Host "  $_" }
if ($rc -ne 0) { throw "ImageTool exited with code $rc" }
if ($toolOut -match 'FAIL') { throw "ImageTool reported FAIL" }
if (-not ($toolOut -match 'PASS|Download Complete')) {
    Write-Warning "ImageTool output missing PASS marker"
}

# 烧完后用 COM10 复位芯片，让 app 跑起来
Write-Host "[post] reset chip via $BootPort ..."
$sp = New-Object System.IO.Ports.SerialPort $BootPort, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.Open()
try {
    $sp.RtsEnable = $true   # CHIP_EN low
    Start-Sleep -Milliseconds 100
    $sp.RtsEnable = $false  # release -> chip boots normally (LOG_TX not held low)
}
finally {
    $sp.Close(); $sp.Dispose()
}

Write-Host "[done] flash success"
