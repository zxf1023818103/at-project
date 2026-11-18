param([string]$Command='', [int]$Seconds=15, [switch]$Reset,
      [string]$DataPort='COM15', [string]$ControlPort='COM23')
$ErrorActionPreference='Stop'
$control=[IO.Ports.SerialPort]::new($ControlPort,115200,'None',8,'One')
$control.DtrEnable=$true
$control.RtsEnable=$false
$serial=[IO.Ports.SerialPort]::new($DataPort,2000000,'None',8,'One')
try {
 $control.Open()
 $serial.Open()
 if($Reset){$control.RtsEnable=$true; Start-Sleep -Milliseconds 1500; $control.RtsEnable=$false}
 Start-Sleep -Milliseconds 500
 if($Command){$serial.Write("`r`n$Command`r`n")}
 $deadline=(Get-Date).AddSeconds($Seconds)
 while((Get-Date) -lt $deadline){$text=$serial.ReadExisting(); if($text){[Console]::Write($text)}; Start-Sleep -Milliseconds 20}
} finally {
 if($serial.IsOpen){$serial.Close()}
 if($control.IsOpen){$control.DtrEnable=$true; $control.RtsEnable=$false; $control.Close()}
}
