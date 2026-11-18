#requires -version 5
[CmdletBinding()]
param([string]$Port='COM42', [int]$Baud=1500000, [string]$Log)
$ErrorActionPreference='Stop'
if (-not $Log) { $Log=Join-Path $PSScriptRoot '..\..\build\amebadplus\cli-test.log' }
$Log=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Log)
$serial=Join-Path $PSScriptRoot 'serial_cmd.ps1'
$failure='ASSERT |Assert Error|STACK OVERFLOW|Crash Dump|HardFault|Usage Fault|Bus Fault|IMG Invalid|BOOT FAIL'
$first= & $serial -Port $Port -Baud $Baud -Cmds @('version','heap','tasks','df','setenv DP_SMOKE verified','getenv DP_SMOKE','date','ip6') -GapMs 700 -PostRead 1
$first=$first -join "`n"
if ($first -match $failure -or $first -notmatch 'at-project Dplus-KM4 - FreeRTOS' -or
    $first -notmatch 'setenv DP_SMOKE=verified -> OK' -or $first -notmatch 'free heap: [1-9]') {
    [IO.File]::WriteAllText($Log,$first)
    throw "Console smoke test failed: $Log"
}
$second= & $serial -Port $Port -Baud $Baud -Reset -PreRead 8 -Cmds @('getenv DP_SMOKE','unsetenv DP_SMOKE','heap','echo CLI_PASS') -GapMs 700 -PostRead 1
$second=$second -join "`n"
[IO.File]::WriteAllText($Log,$first+"`n--- reset and persistence ---`n"+$second)
if ($second -match $failure -or $second -notmatch 'DP_SMOKE=verified' -or $second -notmatch 'application ready') {
    throw "Reset/persistence test failed: $Log"
}
Write-Host "PASS: console, heap, file-backed env, reset and persistence. Log: $Log"
