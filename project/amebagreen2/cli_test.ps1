#requires -version 5
[CmdletBinding()]
param([string]$Port='COM15', [string]$BootPort='COM19', [int]$Baud=1500000, [string]$Log)
$ErrorActionPreference='Stop'
if (-not $Log) { $Log=Join-Path $PSScriptRoot '..\..\build\amebagreen2\cli-test.log' }
$Log=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Log)
$serial=Join-Path $PSScriptRoot 'serial_cmd.ps1'
$failure='ASSERT |Assert Error|STACK OVERFLOW|Crash Dump|HardFault|Usage Fault|Bus Fault|IMG Invalid|BOOT FAIL'
$first= & $serial -Port $Port -BootPort $BootPort -Baud $Baud -Cmds @('version','heap','tasks','df','setenv GREEN2_SMOKE verified','getenv GREEN2_SMOKE','date','ip6') -GapMs 700 -PostRead 1
$first=$first -join "`n"
if ($first -match $failure -or $first -notmatch 'at-project Green2-KM4TZ - FreeRTOS' -or
    $first -notmatch 'setenv GREEN2_SMOKE=verified -> OK' -or $first -notmatch 'free heap: [1-9]') {
    [IO.File]::WriteAllText($Log,$first)
    throw "Console smoke test failed: $Log"
}
$second= & $serial -Port $Port -BootPort $BootPort -Baud $Baud -Reset -PreRead 8 -Cmds @('getenv GREEN2_SMOKE','unsetenv GREEN2_SMOKE','heap','echo CLI_PASS') -GapMs 700 -PostRead 1
$second=$second -join "`n"
[IO.File]::WriteAllText($Log,$first+"`n--- reset and persistence ---`n"+$second)
if ($second -match $failure -or $second -notmatch 'GREEN2_SMOKE=verified' -or $second -notmatch 'application ready') {
    throw "Reset/persistence test failed: $Log"
}
Write-Host "PASS: console, heap, file-backed env, reset and persistence. Log: $Log"
