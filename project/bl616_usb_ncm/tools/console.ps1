param([string]$DataPort='COM22',[string]$Command='',[int]$Seconds=20,
      [string]$ControlPort='',[switch]$Reset)
$ErrorActionPreference='Stop'
if($Reset -and !$ControlPort){throw 'Hardware reset requires an explicit ControlPort'}
$control=$null
$serial=[IO.Ports.SerialPort]::new($DataPort,2000000,'None',8,'One')
$serial.ReadBufferSize=65536
try {
 if($ControlPort){
  $control=[IO.Ports.SerialPort]::new($ControlPort,115200,'None',8,'One')
  $control.DtrEnable=$true;$control.RtsEnable=$false;$control.Open()
 }
 $serial.Open()
 if($Reset){$control.RtsEnable=$true;Start-Sleep -Milliseconds 1500;$control.RtsEnable=$false}
 if($Command){$serial.Write("`r`n$Command`r`n")}
 $deadline=(Get-Date).AddSeconds($Seconds)
 while((Get-Date) -lt $deadline){
  $chunk=$serial.ReadExisting()
  if($chunk){[Console]::Write($chunk)}
  Start-Sleep -Milliseconds 20
 }
} finally {
 $serial.Dispose()
 if($control){$control.DtrEnable=$true;$control.RtsEnable=$false;$control.Dispose()}
}
