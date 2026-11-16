#requires -version 5
[CmdletBinding()]
param([string]$Port='COM15', [string]$BootPort='COM19', [ValidateRange(1,1000)][int]$Count=10,
      [switch]$Storage, [switch]$Crypto, [switch]$Entropy,
      [string]$LogDir=(Join-Path $PSScriptRoot '..\..\build\amebagreen2\boot-soak'))
$ErrorActionPreference='Stop'
[void][IO.Directory]::CreateDirectory($LogDir)
$required=@('application ready')
if ($Storage) { $required+='TFM selftest=3' }
if ($Crypto) { $required+=@('SHA-256 PASS','AES-192/256, AAD, partial blocks, truncated tag, in-place PASS') }
if ($Entropy) { $required+='TFM selftest=3 entropy=65536' }
$cmds=@()
if ($Storage -or $Entropy) { $cmds=@('tfm_status') }
for ($i=1; $i -le $Count; $i++) {
    $log=Join-Path $LogDir ('reset-{0:000}.log' -f $i)
    # Query after boot; byte-level interleaving of KM4NS boot logs can split a
    # printed PASS marker even when the assertion-backed test completed.
    $capture=(& (Join-Path $PSScriptRoot 'serial_cmd.ps1') -Port $Port -BootPort $BootPort -Reset -PreRead 2 -Cmds $cmds -GapMs 100 -PostRead 2 -Log $log | Out-String)
    # Secure Flash operations mask interrupts; boot wall time can exceed its
    # printed RTOS uptime. If the first query preceded completion, query again
    # after the assertion-backed application-ready marker, without resetting.
    if (($Storage -or $Entropy) -and $capture -match 'application ready' -and
        $capture -notmatch 'TFM selftest=3') {
        $capture+=(& (Join-Path $PSScriptRoot 'serial_cmd.ps1') -Port $Port -BootPort $BootPort -PreRead 0 -Cmds @('tfm_status') -GapMs 100 -PostRead 1 | Out-String)
        [IO.File]::WriteAllText($log,$capture)
    }
    # Both cores share LOGUART; its CR insertion can split a printed word.
    # Preserve the raw evidence on disk, normalize CR only for matching.
    $check=$capture.Replace("`r",'')
    if ($check -match 'panic|ASSERT|HardFault|SecureFault|TRNG failed') { throw "Reset $i fault: $log" }
    foreach ($pattern in $required) {
        if ($check -notmatch $pattern) { throw "Reset $i missing '$pattern': $log" }
    }
    Write-Output "Reset $i/$Count PASS"
}
