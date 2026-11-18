param(
    [string]$Port = 'COM19',
    [string]$BdtDirectory = 'D:\MCU_Tools\BDT\release_v5.9.5',
    [string]$Firmware = (Join-Path $PSScriptRoot '..\..\build\tl3238\firmware\tl3238_freertos.bin'),
    [switch]$Flash,
    [int]$SoakSeconds = 0,
    [ValidateRange(0, 65000)][int]$LongIdleMs = 0
)
$ErrorActionPreference = 'Stop'
$Firmware = (Resolve-Path $Firmware).Path
$outputDirectory = Split-Path (Split-Path $Firmware)
$serial = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,One
$serial.Handshake = [System.IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.WriteTimeout = 3000
$serial.ReadTimeout = 500
$transcript = [System.Text.StringBuilder]::new()
$results = [System.Collections.Generic.List[string]]::new()
$clock = [System.Diagnostics.Stopwatch]::StartNew()

function Read-Prompt([int]$TimeoutMs = 5000, [string]$RequiredPattern = '', [switch]$AllowBootLog) {
    $response = [System.Text.StringBuilder]::new()
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.ElapsedMilliseconds -lt $TimeoutMs) {
        $chunk = $serial.ReadExisting()
        if ($chunk) {
            $null = $response.Append($chunk)
            $null = $transcript.Append($chunk)
            if ($response.ToString() -match 'FATAL:') { throw $response.ToString() }
            # NimBLE initializes asynchronously and can print just after the
            # initial CLI prompt. Normal command responses still require the
            # prompt at the end so an earlier prompt cannot mask lost bytes.
            if (($response.ToString().EndsWith('tl3238> ') -or
                 ($AllowBootLog -and $response.ToString().Contains('tl3238> '))) -and
                (!$RequiredPattern -or $response.ToString() -cmatch $RequiredPattern)) {
                return $response.ToString()
            }
        }
        Start-Sleep -Milliseconds 10
    }
    throw "Timed out waiting for CLI prompt; received: $response"
}

function Check-Command([string]$Command, [string]$Pattern, [string]$Label = $Command, [int]$TimeoutMs = 5000) {
    $null = $transcript.AppendLine(("`r`n[{0:F3}s] SEND {1}" -f $clock.Elapsed.TotalSeconds, $Label))
    $serial.Write($Command + "`r")
    $response = Read-Prompt -TimeoutMs $TimeoutMs -RequiredPattern $Pattern
    if ($response -cnotmatch $Pattern) { throw "Unexpected response to ${Label}: $response" }
    $results.Add($Label)
    Write-Host "PASS $Label"
    return $response
}

function Get-Counter([string]$Response, [string]$Name) {
    $match = [regex]::Match($Response, "\b$Name=(\d+)")
    if (!$match.Success) { throw "Missing $Name in $Response" }
    return [long]$match.Groups[1].Value
}

function Check-IdleTiming([string]$Response, [int]$RequestedMs) {
    $ticks = Get-Counter $Response 'elapsed_ticks'
    $hardwareMs = Get-Counter $Response 'hardware_ms'
    if ($ticks -lt $RequestedMs -or $ticks -gt ($RequestedMs + 5) -or
        $hardwareMs -lt ($RequestedMs - 2) -or $hardwareMs -gt ($RequestedMs + 10)) {
        throw "Tickless compensation drift: $Response"
    }
}

try {
    $serial.Open()
    if ($Flash) {
        & (Join-Path $PSScriptRoot '..\tl3238_hello\flash.ps1') -BdtDirectory $BdtDirectory `
            -Firmware $Firmware -Port $Port -SkipSerialVerify
        $boot = Read-Prompt -TimeoutMs 10000 -AllowBootLog
        Write-Host $boot
        if ($boot -notmatch 'FreeRTOS V11\.1\.0 ready') { throw "Missing scheduler startup: $boot" }
        # OTA hashes the running image before starting the host event loop.
        # Wait for asynchronous BLE initialization before sending CLI tests.
        $nativeImagePath = (Get-Item -LiteralPath $Firmware).FullName
        $imageText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($nativeImagePath))
        if ($imageText.Contains('NimBLE ready:')) {
            $readyTimer = [System.Diagnostics.Stopwatch]::StartNew()
            while ($boot -notmatch 'NimBLE ready:' -and $readyTimer.ElapsedMilliseconds -lt 10000) {
                $chunk = $serial.ReadExisting()
                $boot += $chunk
                $null = $transcript.Append($chunk)
                if ($boot -match 'FATAL:') { throw $boot }
                Start-Sleep -Milliseconds 10
            }
            if ($boot -notmatch 'NimBLE ready:') { throw 'BLE startup did not complete' }
            $serial.Write("`r")
            $null = Read-Prompt
        }
    } else {
        $serial.DiscardInBuffer()
        $serial.Write("`r")
        $null = Read-Prompt
    }
    $null = Check-Command 'version' 'TL3238 / FreeRTOS V11\.1\.0 / FreeRTOS-Plus-CLI'
    $help = Check-Command 'help' 'selftest:'
    $hasBle = $help -match 'ble \[disconnect\]:'
    if ($hasBle) { $null = Check-Command 'ble' 'sync=1.*resets=0' }
    $hasCoc = $help -match 'coc \[selftest\]:'
    if ($hasCoc) {
        $null = Check-Command 'coc' 'CoC ready=1.*open=0'
        $null = Check-Command 'coc selftest' 'COC SELFTEST PASS synthetic=1 rc=0'
        $null = Check-Command 'coc invalid' 'Usage: coc'
        $null = Check-Command 'coc selftest extra' 'Usage: coc'
    }
    foreach ($command in @('version', 'hello', 'echo', 'uptime', 'tasks', 'heap', 'stats', 'tickless', 'idle')) {
        if ($help -notmatch "$command[ :]") { throw "Missing help for $command" }
    }
    $null = Check-Command 'hello' "`r`nhello world`r`n"
    $null = Check-Command 'echo FreeRTOS CLI RX TX works' "`r`nFreeRTOS CLI RX TX works`r`n"
    $tasks = Check-Command 'tasks' 'stack_free_bytes'
    foreach ($name in @('cli', 'producer', 'consumer', 'IDLE', 'Tmr Svc')) {
        if ($tasks -notmatch [regex]::Escape($name)) { throw "Missing task $name" }
    }
    if ($hasBle -and ($tasks -notmatch 'nimble' -or $tasks -notmatch 'ble_ll')) {
        throw 'Missing NimBLE/controller tasks'
    }
    $first = Check-Command 'uptime' 'ticks=(\d+) uptime_ms=(\d+)'
    $firstTicks = [long]([regex]::Match($first, 'ticks=(\d+)').Groups[1].Value)
    $measure = [System.Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 2
    $second = Check-Command 'uptime' 'ticks=(\d+) uptime_ms=(\d+)'
    $seconds = $measure.Elapsed.TotalSeconds
    $secondTicks = [long]([regex]::Match($second, 'ticks=(\d+)').Groups[1].Value)
    $tickRate = ($secondTicks - $firstTicks) / $seconds
    if ($tickRate -lt 900 -or $tickRate -gt 1100) { throw "Unexpected tick rate: $tickRate Hz" }
    Write-Host ('Measured tick rate: {0:F1} Hz' -f $tickRate)
    $null = Check-Command 'selftest' 'SELFTEST PASS'
    $baseline = Check-Command 'heap' 'heap_total=\d+ free=\d+'
    $beforeHeap = [regex]::Match($baseline, 'free=(\d+)').Groups[1].Value
    $null = Check-Command 'does-not-exist' 'Command not recognised'
    $null = Check-Command 'hello extra' 'Incorrect command parameter'
    $null = Check-Command (('x' * 180)) 'ERROR: command too long' 'overlong line rejected'
    $null = Check-Command ("hellx" + [char]8 + 'o') "`r`nhello world`r`n" 'backspace editing'
    $null = Check-Command ("discard" + [char]3 + 'hello') "`r`nhello world`r`n" 'Ctrl-C editing'
    $null = Check-Command "hello`r`n" "`r`nhello world`r`n" 'CRLF input'
    # Drain the extra empty CR from the generic helper before the next test.
    Start-Sleep -Milliseconds 100
    $null = $transcript.Append($serial.ReadExisting())
    for ($i = 0; $i -lt 40; $i++) {
        $payload = "packet-$i-" + ('0123456789' * 9)
        $null = Check-Command ("echo " + $payload) ([regex]::Escape("`r`n$payload`r`n")) "burst echo $i"
    }

    # Measure actual timer ISR entries with the same 10 ms producer workload.
    $offStart = Check-Command 'tickless off' 'tickless=0 mode=WFI'
    Start-Sleep -Seconds 2
    $offEnd = Check-Command 'tickless stats' 'tickless=0 mode=WFI'
    $offIrqs = (Get-Counter $offEnd 'tick_irqs') - (Get-Counter $offStart 'tick_irqs')
    if ($offIrqs -lt 1800 -or $offIrqs -gt 2300) { throw "Periodic tick IRQ count: $offIrqs" }
    $onStart = Check-Command 'tickless on' 'tickless=1 mode=WFI'
    Start-Sleep -Seconds 2
    $onEnd = Check-Command 'tickless stats' 'tickless=1 mode=WFI'
    $onIrqs = (Get-Counter $onEnd 'tick_irqs') - (Get-Counter $onStart 'tick_irqs')
    $skipped = (Get-Counter $onEnd 'skipped_ticks') - (Get-Counter $onStart 'skipped_ticks')
    if ($onIrqs -gt ($offIrqs * 0.35) -or $skipped -lt 1000) {
        throw "Insufficient tick suppression: periodic=$offIrqs tickless=$onIrqs skipped=$skipped"
    }
    Write-Host "Tick IRQs over 2 seconds: periodic=$offIrqs tickless=$onIrqs skipped=$skipped"

    foreach ($duration in @(2, 3, 17, 101, 1200)) {
        $idle = Check-Command "idle $duration" "IDLE requested_ms=$duration elapsed_ticks=\d+ hardware_ms=\d+"
        Check-IdleTiming $idle $duration
    }

    # The CLI blocks for 1.2 s. Deliver a complete command while the CPU is in
    # WFI; it must wake early, preserve RX bytes, and keep the original deadline.
    $wakeBefore = Check-Command 'tickless stats' 'tickless=1 mode=WFI'
    $null = $transcript.AppendLine("`r`nSEND idle 1200; after 173 ms SEND echo wake-from-uart")
    $serial.Write("idle 1200`r")
    Start-Sleep -Milliseconds 173
    $serial.Write("echo wake-from-uart`r")
    $wakeResponse = Read-Prompt -RequiredPattern 'IDLE requested_ms=1200[\s\S]*\r\nwake-from-uart\r\n'
    Check-IdleTiming $wakeResponse 1200
    $results.Add('UART wake during idle')
    Write-Host 'PASS UART wake during idle (all command bytes retained)'
    $wakeAfter = Check-Command 'tickless stats' 'tickless=1 mode=WFI'
    if ((Get-Counter $wakeAfter 'uart_wakes') -le (Get-Counter $wakeBefore 'uart_wakes')) {
        throw 'No UART wake recorded'
    }
    $null = Check-Command 'tickless invalid' 'Usage: tickless'
    $null = Check-Command 'tickless off extra' 'Usage: tickless'
    $null = Check-Command 'idle 0' 'Usage: idle'
    if ($LongIdleMs -gt 0) {
        $idle = Check-Command "idle $LongIdleMs" "IDLE requested_ms=$LongIdleMs elapsed_ticks=\d+ hardware_ms=\d+" `
            -TimeoutMs ($LongIdleMs + 5000)
        Check-IdleTiming $idle $LongIdleMs
    }
    $deadline = $clock.Elapsed.TotalSeconds + $SoakSeconds
    $lastQueue = 0L
    $lastTimer = 0L
    while ($clock.Elapsed.TotalSeconds -lt $deadline) {
        Start-Sleep -Seconds 5
        $soak = Check-Command 'selftest' 'SELFTEST PASS' 'soak selftest'
        $queue = [long]([regex]::Match($soak, 'queue=(\d+)').Groups[1].Value)
        $timer = [long]([regex]::Match($soak, 'timer=(\d+)').Groups[1].Value)
        if ($queue -le $lastQueue -or $timer -le $lastTimer) { throw "Background tasks stopped: $soak" }
        $lastQueue = $queue
        $lastTimer = $timer
    }
    $uptime = Check-Command 'uptime' 'ticks=(\d+) uptime_ms=(\d+)'
    $finalTicks = [long]([regex]::Match($uptime, 'ticks=(\d+)').Groups[1].Value)
    $stats = Check-Command 'stats' 'sequence_errors=0 timer_callbacks=\d+ fpu_errors=0'
    if ($stats -notmatch 'rx_dropped=0 rx_errors=0 tx_errors=0') { throw "UART errors: $stats" }
    $heap = Check-Command 'heap' 'heap_total=\d+ free=\d+'
    if ((Get-Counter $heap 'heap_total') -ne (Get-Counter $baseline 'heap_total')) {
        throw 'Heap capacity changed during verification'
    }
    $afterHeap = [regex]::Match($heap, 'free=(\d+)').Groups[1].Value
    if ($beforeHeap -ne $afterHeap) { throw "Heap changed: $beforeHeap -> $afterHeap" }
    $null = Check-Command 'selftest' 'SELFTEST PASS'
    $tickless = Check-Command 'tickless stats' 'tickless=1 mode=WFI'
    # Active advertising/connections wake WFI before long RTOS deadlines.
    $minimumIdle = if ($hasBle) { 10 } else { 1000 }
    if ((Get-Counter $tickless 'max_idle_ticks') -lt $minimumIdle -or
        (Get-Counter $tickless 'timer_wakes') -eq 0 -or (Get-Counter $tickless 'uart_wakes') -eq 0) {
        throw "Missing tickless wake coverage: $tickless"
    }
    if (!$hasBle -and $LongIdleMs -gt 60000 -and (Get-Counter $tickless 'max_idle_ticks') -lt 59999) {
        throw "Maximum sleep cap was not exercised: $tickless"
    }
    $verification = [ordered]@{
        result = 'PASS'; port = $Port; baud = 115200; tx = 'PA1'; rx = 'PA2'
        firmware_sha256 = (Get-FileHash $Firmware -Algorithm SHA256).Hash.ToLower()
        checks = $results.Count; tick_rate_hz = [math]::Round($tickRate, 1)
        ble_enabled = $hasBle
        coc_enabled = $hasCoc
        initial_ticks = $firstTicks; final_ticks = $finalTicks
        heap_free_before = $beforeHeap; heap_free_after = $afterHeap
        soak_seconds = $SoakSeconds; elapsed_seconds = [math]::Round($clock.Elapsed.TotalSeconds, 1)
        completed_at = [DateTimeOffset]::Now.ToString('o'); stats = $stats
        tickless = [ordered]@{
            mode = 'WFI'; periodic_irqs_2s = $offIrqs; tickless_irqs_2s = $onIrqs
            suppressed_ticks_2s = $skipped; long_idle_ms = $LongIdleMs
            final_stats = $tickless
        }
    }
    $verification | ConvertTo-Json | Set-Content (Join-Path $outputDirectory 'verification.json') -Encoding UTF8
    Write-Host ("PASS: {0} checks, heap stable at {1} bytes, UART errors=0." -f $results.Count, $afterHeap)
} catch {
    [ordered]@{result = 'FAIL'; error = $_.ToString(); port = $Port} | ConvertTo-Json |
        Set-Content (Join-Path $outputDirectory 'verification.json') -Encoding UTF8
    throw
} finally {
    $transcript.ToString() | Set-Content (Join-Path $outputDirectory "cli-$Port.log") -Encoding UTF8
    if ($serial.IsOpen) { $serial.Close() }
    $serial.Dispose()
}
