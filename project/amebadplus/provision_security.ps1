#requires -version 5
# Explicit, staged OTP provisioning. Never called by an ordinary build/flash.
[CmdletBinding()]
param([string]$Port='COM42', [string]$Keys, [string]$PublicHash,
      [ValidateSet('Keys','Sboot','Rsip','Protect')][string]$Stage,
      [switch]$ConfirmBurn, [string]$Log)
$ErrorActionPreference='Stop'
if (-not $ConfirmBurn -or -not $Stage) { throw 'Specify -Stage and -ConfirmBurn for irreversible OTP writes' }
if (-not $Keys) { $Keys=Join-Path $PSScriptRoot 'keys\com42.json' }
$k=Get-Content -Raw $Keys | ConvertFrom-Json
if ($PublicHash -notmatch '^[0-9a-fA-F]{64}$') { throw 'PublicHash must be the independently verified root public-key SHA256' }
foreach ($name in @('rsip_data','rsip_tweak')) {
    if ($k.$name -notmatch '^[0-9a-fA-F]{64}$') { throw "Invalid $name" }
}
$trace=New-Object Text.StringBuilder
$p=New-Object IO.Ports.SerialPort $Port,1500000,'None',8,'One'
$p.Handshake='None'; $p.RtsEnable=$false; $p.DtrEnable=$false
function Redact([string]$text) {
    foreach ($property in $k.PSObject.Properties) {
        $text=$text -replace [regex]::Escape([string]$property.Value),'[REDACTED]'
    }
    return $text
}
function Command([string]$cmd,[int]$address,[int]$length,[bool]$write=$false) {
    $p.DiscardInBuffer()
    $p.Write("`r"); Start-Sleep -Milliseconds 100
    foreach ($c in $cmd.ToCharArray()) { $p.Write([string]$c); Start-Sleep -Milliseconds 8 }
    $p.Write("`r")
    $reply=New-Object Text.StringBuilder
    $deadline=(Get-Date).AddSeconds(8)
    $pattern=('efuse physical 0x{0:x3}: ([0-9a-f]{{{1}}})' -f $address,($length*2))
    if ($write) { $pattern+=' \(verified\)' }
    while ((Get-Date) -lt $deadline) {
        [void]$reply.Append($p.ReadExisting())
        if ($reply.ToString() -match $pattern) {
            $value=$Matches[1]
            $shown=if ($address -in @(0x2c0,0x2e0)) { '[REDACTED]' } else { $value }
            [void]$trace.AppendLine(('OTP 0x{0:x3}: {1}; write={2}; verified' -f $address,$shown,$write))
            return $value
        }
        if ($reply.ToString() -match 'rejected/failed') { break }
        Start-Sleep -Milliseconds 20
    }
    [void]$trace.AppendLine(('OTP 0x{0:x3}: no verified response; stopped' -f $address))
    throw ('No verified OTP response at 0x{0:x3}; stopped' -f $address)
}
function ReadOtp([int]$address,[int]$length) {
    return Command ('efuse_read {0:x} {1}' -f $address,$length) $address $length
}
function WriteOtp([int]$address,[string]$value) {
    $got=Command ('efuse_burn {0:x} {1} CONFIRM' -f $address,$value) $address ($value.Length/2) $true
    if ($got -ne $value -or (ReadOtp $address ($value.Length/2)) -ne $value) { throw 'OTP readback mismatch; stopped' }
    Write-Host ('OTP 0x{0:x3}: programmed and verified ({1} bytes)' -f $address,($value.Length/2))
}
function ClearBits([int]$address,[int]$mask) {
    $before=[Convert]::ToInt32((ReadOtp $address 1),16)
    $after=$before -band (255 -bxor $mask)
    WriteOtp $address ('{0:x2}' -f $after)
}
try {
    $p.Open()
    $entries=@(@(0x320,$PublicHash.ToLower()),@(0x2c0,$k.rsip_data.ToLower()),@(0x2e0,$k.rsip_tweak.ToLower()))
    # Check every key before writing anything; never merge a different key into OTP.
    foreach ($entry in $entries) {
        $got=ReadOtp $entry[0] 32
        if ($got -ne $entry[1] -and ($Stage -ne 'Keys' -or $got -ne ('f'*64))) {
            throw ('OTP 0x{0:x3} is neither the expected key/hash nor an allowed blank slot' -f $entry[0])
        }
    }
    switch ($Stage) {
        'Keys' { foreach ($entry in $entries) { WriteOtp $entry[0] $entry[1] } }
        'Sboot' { ClearBits 0x368 8 }
        'Rsip' {
            if (([Convert]::ToInt32((ReadOtp 0x368 1),16) -band 8) -ne 0) { throw 'Verify SBOOT first' }
            if (([Convert]::ToInt32((ReadOtp 0x369 1),16) -band 3) -notin @(0,3)) { throw 'OTP mode is not XTS' }
            ClearBits 0x368 4
        }
        'Protect' {
            if (([Convert]::ToInt32((ReadOtp 0x368 1),16) -band 12) -ne 0) { throw 'SBOOT and RSIP must both be enabled first' }
            ClearBits 0x365 2 # root public-key hash write protection
            ClearBits 0x366 128 # RSIP data key read protection
            ClearBits 0x367 15 # both key write protection, tweak read protection, mode lock
        }
    }
    [void]$trace.AppendLine("Stage $Stage completed; reset is required for OTP settings to take effect.")
    Write-Host "Stage $Stage completed."
} finally {
    if ($p.IsOpen) { $p.Close() }; $p.Dispose()
    if ($Log) { [IO.File]::WriteAllText($Log,(Redact $trace.ToString())) }
}
