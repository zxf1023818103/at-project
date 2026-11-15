#requires -version 5
[CmdletBinding()]
param(
    [string]$Port = 'COM42',
    [string]$BootPort,
    [int]$Baudrate = 1500000,
    [int]$ConsoleBaud = 1500000,
    [string]$Image,
    [string]$ToolDir,
    [string]$BootLog,
    [switch]$FirmwareOnly,
    [switch]$NoUnlock,
    [switch]$AllowEncrypted,
    [int]$LogSecs = 20
)
$ErrorActionPreference = 'Stop'
if (-not $BootPort) { $BootPort = $Port }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
if (-not $Image) {
    $filename = if ($FirmwareOnly) { 'firmware.bin' } else { 'Image_All.bin' }
    $Image = Join-Path $repo "build\amebadplus\$filename"
}
if (-not $ToolDir) {
    $signedTool=Join-Path $repo 'build\amebadplus\security-tools'
    $ToolDir=if (Test-Path "$signedTool\Ameba_1-N_MP_ImageTool.exe") { $signedTool } else {
        Join-Path $repo 'component\amebadplus\image_tool\MP_ImageTool_1_N'
    }
}
if (-not $BootLog) { $BootLog = Join-Path $repo 'build\amebadplus\boot.log' }
$Image = (Resolve-Path $Image).ProviderPath
$exe = Join-Path $ToolDir 'Ameba_1-N_MP_ImageTool.exe'
if (-not (Test-Path $exe)) { throw "ImageTool not found: $exe" }
$imageSize = (Get-Item $Image).Length
$bytes = [IO.File]::ReadAllBytes($Image)
if ($FirmwareOnly) {
    if ($imageSize -lt 0x12000 -or $imageSize -gt 0x180000) { throw 'Firmware-only image must contain boot + OTA1 and end before OTA2/LittleFS/storage' }
    if ([Text.Encoding]::ASCII.GetString($bytes,0x10000,8) -ne '81958711') { throw 'Missing OTA1 application certificate' }
} elseif ($imageSize -ne 0x400000) { throw 'Expected the merged 4 MiB Image_All.bin' }
# RSIP encrypts the XIP load headers. A development chip cannot boot that image
# until its matching RSIP material has been provisioned separately.
if (-not $AllowEncrypted -and (
    [BitConverter]::ToString($bytes,0x1000,8) -ne '99-99-96-96-3F-CC-66-FC' -or
    [Text.Encoding]::ASCII.GetString($bytes,0x12000,8) -ne '81958711')) {
    throw 'Encrypted or invalid XIP headers. Use a plaintext RSIP=OFF image; -AllowEncrypted is only for a device already provisioned with matching RSIP keys.'
}
function Open-Port([string]$Name, [int]$Baud) {
    $p = New-Object System.IO.Ports.SerialPort $Name,$Baud,'None',8,'One'
    $p.Handshake='None'; $p.RtsEnable=$false; $p.DtrEnable=$false; $p.ReadTimeout=200
    $p.Open(); return $p
}
function Enter-Download {
    $p = Open-Port $BootPort 115200
    try {
        $p.RtsEnable=$true; Start-Sleep -Milliseconds 100
        $p.DtrEnable=$true; Start-Sleep -Milliseconds 100
        $p.RtsEnable=$false; Start-Sleep -Milliseconds 100
        $p.DtrEnable=$false; Start-Sleep -Milliseconds 100
    } finally { $p.Close(); $p.Dispose() }
    $p = Open-Port $Port 115200
    try {
        $deadline=(Get-Date).AddSeconds(3)
        while ((Get-Date) -lt $deadline) {
            try { if ($p.ReadByte() -eq 0x15) { return } } catch [TimeoutException] {}
        }
        throw "No ROM download handshake on $Port"
    } finally { $p.Close(); $p.Dispose() }
}
function Unlock-Firmware {
    # ImageTool clears BP but does not clear CMP on this NOR. Like AmebaD,
    # ask the running application to clear both before entering ROM download.
    $p = Open-Port $Port $ConsoleBaud
    try {
        $p.DiscardInBuffer()
        $p.Write("`r")
        Start-Sleep -Milliseconds 100
        foreach ($c in 'flash unlock'.ToCharArray()) {
            $p.Write([string]$c); Start-Sleep -Milliseconds 8
        }
        $p.Write("`r")
        $reply = New-Object System.Text.StringBuilder
        $deadline = (Get-Date).AddSeconds(2)
        while ((Get-Date) -lt $deadline) {
            [void]$reply.Append($p.ReadExisting())
            if ($reply.ToString() -match 'flash unlock: [^\r\n]* (OK|FAIL)') {
                if ($Matches[1] -eq 'FAIL') { throw 'Application failed to unlock firmware' }
                Write-Host "[unlock] $($Matches[0])"
                return
            }
            Start-Sleep -Milliseconds 30
        }
        Write-Host '[unlock] no console reply; continuing for blank/ROM/HCI devices'
    } finally { $p.Close(); $p.Dispose() }
}
Write-Host "[flash] AmebaDplus $Port, image $Image"
if (-not $NoUnlock) { Unlock-Firmware }
Enter-Download
Push-Location $ToolDir
try {
    $result = & $exe -c AmebaDplus -i $Image -b $Baudrate -e false -g false -v true -s true -d $Port 2>&1
    $rc = $LASTEXITCODE
    $result | ForEach-Object { Write-Host $_ }
    if ($rc -ne 0 -or ($result -match 'FAIL') -or -not ($result -match 'PASS|Download Complete')) {
        throw "ImageTool did not verify successful download (exit $rc)"
    }
} finally { Pop-Location }
# On a single adapter, hold the same handle through reset and capture.
$p = Open-Port $Port $ConsoleBaud
$control = $p
try {
    if ($BootPort -ne $Port) { $control = Open-Port $BootPort 115200 }
    $control.RtsEnable=$true; Start-Sleep -Milliseconds 100; $control.RtsEnable=$false
    $log=New-Object System.Text.StringBuilder
    $deadline=(Get-Date).AddSeconds($LogSecs)
    while ((Get-Date) -lt $deadline) {
        [void]$log.Append($p.ReadExisting()); Start-Sleep -Milliseconds 30
    }
    [IO.File]::WriteAllText($BootLog,$log.ToString())
    Write-Host $log.ToString()
    Write-Host "[boot] saved to $BootLog"
    if ($log.ToString() -match "BOOT FAIL|IMG Invalid|HardFault|SecureFault|FATAL ERROR|panic caller|Usage Fault|Bus Fault|MemManage|Crash Dump|Assert Error|ASSERT |STACK OVERFLOW" -or $log.ToString() -notmatch "application ready") {
        throw "Firmware startup validation failed; see $BootLog"
    }
} finally {
    if ($control -ne $p) { $control.Close(); $control.Dispose() }
    $p.Close(); $p.Dispose()
}
