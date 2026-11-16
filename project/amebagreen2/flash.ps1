#requires -version 5
[CmdletBinding()]
param([string]$Port='COM15', [string]$BootPort='COM19', [int]$Baudrate=1500000,
      [string]$Image, [string]$ToolDir, [string]$BootLog, [int]$LogSecs=20,
      [switch]$FirmwareOnly, [switch]$AllowEncrypted, [string]$Lfs,
      [string]$Backup, [switch]$VerifyOnly, [string]$Python)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
if (-not $Image -and -not $Backup) { $Image=Join-Path $repo 'build\amebagreen2\firmware.bin' }
if (-not $Python) {
    $candidate=Join-Path $env:USERPROFILE 'scoop\apps\python310\current\python.exe'
    $Python=if (Test-Path $candidate) { $candidate } else { 'python.exe' }
}
$argsList=@((Join-Path $PSScriptRoot 'flash_uart.py'),'--port',$Port,'--boot-port',$BootPort,
            '--baud',"$Baudrate",'--log-secs',"$LogSecs")
if ($Image) { $argsList+=@('--image',(Resolve-Path $Image).ProviderPath) }
if ($ToolDir) { $argsList+=@('--floader',(Join-Path $ToolDir 'Devices\Floaders\floader_amebagreen2.bin')) }
if ($BootLog) { $argsList+=@('--boot-log',$BootLog) }
if ($Lfs) { $argsList+=@('--lfs',(Resolve-Path $Lfs).ProviderPath) }
if ($Backup) { $argsList+=@('--backup',$Backup) }
if ($VerifyOnly) { $argsList+='--verify-only' }
if ($AllowEncrypted) { $argsList+='--allow-encrypted' }
& $Python @argsList
if ($LASTEXITCODE -ne 0) { throw "Green2 download/readback/startup failed (exit $LASTEXITCODE)" }
