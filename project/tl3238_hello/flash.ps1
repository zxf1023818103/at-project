param(
    [string]$BdtDirectory = 'D:\MCU_Tools\BDT\release_v5.9.5',
    [string]$Port = 'COM19',
    [string]$Firmware = (Join-Path $PSScriptRoot '..\..\build\tl3238_hello\component-firmware\tl3238_hello.bin'),
    [switch]$SkipSerialVerify
)
$ErrorActionPreference = 'Stop'
$Firmware = (Resolve-Path $Firmware).Path
$outputDirectory = Split-Path (Split-Path $Firmware)
$stage = Join-Path $env:TEMP 'tl3238_hello'
New-Item -ItemType Directory -Force $stage | Out-Null
$nativeFirmware = Join-Path $stage 'tl3238_hello.bin'
$nativeReadback = Join-Path $stage 'readback.bin'
Copy-Item $Firmware $nativeFirmware -Force
$log = Join-Path $outputDirectory 'flash.log'
Set-Content $log '' -Encoding UTF8
$tool = Join-Path $BdtDirectory 'config\Cmd_download_tool.exe'
if (!(Test-Path $tool)) { throw "BDT executable missing: $tool" }

function Invoke-Bdt([string[]]$BdtArgs) {
    # This BDT 5.9.5 CLI drops the first character of -o path arguments.
    # Prefix a disposable character; keep literal quotes for paths with spaces.
    $arguments = [System.Collections.Generic.List[string]]::new()
    $arguments.Add('1')
    $arguments.Add('TL323X')
    $quoteNext = $false
    $previousArgument = ''
    foreach ($argument in $BdtArgs) {
        if ($quoteNext) {
            $prefix = if ($previousArgument -eq '-o') { '_' } else { '' }
            $arguments.Add('"' + $prefix + $argument + '"')
        }
        else { $arguments.Add($argument) }
        $quoteNext = $argument -in @('-i', '-o')
        $previousArgument = $argument
    }
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $tool
    $start.Arguments = $arguments -join ' '
    $start.WorkingDirectory = $BdtDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($start)
    $text = $process.StandardOutput.ReadToEnd() + $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    $code = $process.ExitCode
    $process.Dispose()
    Add-Content $log ("BDT " + ($BdtArgs -join ' ') + "`n" + $text) -Encoding UTF8
    Write-Host $text
    if ($code -ne 0) { throw "BDT failed with exit code $code" }
    return $text
}

$serial = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,One
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 1500
$serial.NewLine = "`n"
$needsReset = $false
Push-Location $BdtDirectory
try {
    if (!$SkipSerialVerify) { $serial.Open() }
    $null = Invoke-Bdt @('sws', 'b0', '05', 'b0', '10')
    $chip = Invoke-Bdt @('rc', '8014083e', '-s', '2')
    if ($chip -notmatch '8014083e:\s*87 56') { throw 'Unexpected chip ID; expected TL323x (0x5687)' }
    $needsReset = $true
    $null = Invoke-Bdt @('lf', '0', '0')
    $null = Invoke-Bdt @('wf', '0', '-i', $nativeFirmware)
    $size = (Get-Item $nativeFirmware).Length
    $readSize = [int]([math]::Ceiling($size / 4096.0) * 4096)
    $null = Invoke-Bdt @('rf', '0', '-s', "$readSize", '-o', $nativeReadback)
    $expected = [System.IO.File]::ReadAllBytes($nativeFirmware)
    $actual = [System.IO.File]::ReadAllBytes($nativeReadback)
    if ($actual.Length -lt $expected.Length) { throw 'Incomplete flash readback' }
    for ($i = 0; $i -lt $expected.Length; $i++) {
        if ($expected[$i] -ne $actual[$i]) { throw "Flash readback mismatch at offset $i" }
    }
    Copy-Item $nativeReadback (Join-Path $outputDirectory 'readback.bin') -Force
    Write-Host "Flash verified: $size bytes match."
    Add-Content $log "Flash verified: $size bytes match." -Encoding UTF8
    if (!$SkipSerialVerify) { $serial.DiscardInBuffer() }
    $null = Invoke-Bdt @('rst', '-f')
    $needsReset = $false
    if ($SkipSerialVerify) {
        Write-Host 'Flash verified and MCU reset; serial capture skipped.'
        return
    }

    $capture = [System.Collections.Generic.List[string]]::new()
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $helloCount = 0
    while ($clock.Elapsed.TotalSeconds -lt 7) {
        try {
            $line = $serial.ReadLine().TrimEnd([char]13)
            $record = '{0:F3}s {1}' -f $clock.Elapsed.TotalSeconds, $line
            $capture.Add($record)
            Write-Host $record
            if ($line -eq 'hello world') { $helloCount++ }
        } catch [System.TimeoutException] { }
    }
    $capture | Set-Content (Join-Path $outputDirectory "uart-$Port.log") -Encoding UTF8
    if ($helloCount -lt 3) { throw "UART verification failed: $helloCount complete hello world lines" }
    Write-Host "PASS: $Port 115200 8N1, $helloCount hello world lines received."
} finally {
    if ($needsReset) { try { $null = Invoke-Bdt @('rst', '-f') } catch { Write-Warning $_ } }
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
    Pop-Location
}
