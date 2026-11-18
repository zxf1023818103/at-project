param(
  [int]$Baud = 2000000,                 # flash (bootrom ISP) baud on COM22
  [int]$LogBaud = 2000000,              # console-capture baud on COM22 (project bl616 AT console = 2M; some images differ, pass -LogBaud explicitly, no auto-fallback)
  [int]$LogSeconds = 8,                 # how long to capture the boot log
  [string]$Fw = "D:\MCU_Tools\bflb-cklink\fw_bl616\at_bl616_whole_flash_data.bin",
  [string]$Tool = "D:\MCU_Tools\BouffaloLabDevCube-v1.8.7\bflb_iot_tool.exe",
  [string]$LogFile = "D:\MCU_Tools\bflb-cklink\bl616_bootlog.txt",
  # FTDI TTL: DtrEnable=$true typically drives the DTR pin LOW (active-low).
  # -Invert flips the mapping so we can match this board's wiring empirically.
  [switch]$Invert
)

# COM23 = control lines: DTR->BOOT, RTS->reset (active-low pins).  COM22 = data UART + console.
# pin-level intent: enter ISP = BOOT HIGH, reset LOW->HIGH;  boot = BOOT LOW, reset LOW->HIGH.
$HIGH = $false; $LOW = $true            # default: Enable=$false => pin high (FTDI active-low)
if ($Invert) { $HIGH = $true; $LOW = $false }

$ctrl = New-Object System.IO.Ports.SerialPort('COM23',115200,'None',8,'One')
$ctrl.Open()

Write-Output ("== enter ISP: BOOT high, reset low->high (Invert=" + $Invert + ") ==")
$ctrl.DtrEnable = $HIGH           # BOOT pin high
$ctrl.RtsEnable = $LOW            # reset asserted (low)
Start-Sleep -Milliseconds 250
$ctrl.RtsEnable = $HIGH           # reset released (high) -> latch ISP
Start-Sleep -Milliseconds 250
# keep COM23 open holding BOOT high through the flash

Write-Output "== flash over COM22 @ $Baud =="
$toolDir = Split-Path $Tool
Push-Location $toolDir
$argline = "--interface uart --port COM22 --baudrate $Baud --chipname bl616 --single --firmware `"$Fw`" --addr 0x0"
$p = Start-Process -FilePath $Tool -ArgumentList $argline -NoNewWindow -Wait -PassThru -RedirectStandardOutput "D:\MCU_Tools\bflb-cklink\bl616_flash_out.txt"
Pop-Location
Write-Output ("== tool exit code: " + $p.ExitCode + " ==")

# --- boot + capture log ---
# Order matters: OPEN THE COM22 READER FIRST, then pulse COM23 RTS to reset, so the boot
# burst is not missed.  COM23 stays OPEN the whole time (closing it floats DTR/RTS and can
# hold the chip in reset -> empty log that looks dead but isn't).
Write-Output "== set run-state (BOOT low), open COM22 @ $LogBaud, then RTS-reset & capture =="
$ctrl.DtrEnable = $LOW            # BOOT pin low (normal boot)
$ctrl.RtsEnable = $HIGH           # reset released for now
Start-Sleep -Milliseconds 150

$log = New-Object System.IO.Ports.SerialPort('COM22',$LogBaud,'None',8,'One')
$log.Open()
$log.DiscardInBuffer()

# reset pulse via COM23 RTS: assert (low) -> release (high) -> chip boots
$ctrl.RtsEnable = $LOW
Start-Sleep -Milliseconds 200
$ctrl.RtsEnable = $HIGH

$sb = New-Object System.Text.StringBuilder
$t = (Get-Date).AddSeconds($LogSeconds)
while ((Get-Date) -lt $t) {
  try { $c = $log.ReadExisting(); if ($c) { [void]$sb.Append($c) } } catch {}
  Start-Sleep -Milliseconds 20
}
$log.Close()
$ctrl.Close()

$sb.ToString() | Out-File -Encoding ascii $LogFile
Write-Output ("== boot log (" + $sb.Length + " bytes) saved to " + $LogFile + " ==")
Write-Output $sb.ToString()
Write-Output "== done =="
