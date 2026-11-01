param(
 [string]$Fw=(Join-Path $PSScriptRoot '..\build\whole_flash_data.bin'),
 [string]$DataPort='COM15', [string]$CtrlPort='COM23',
 [string]$Tool='D:\MCU_Tools\bflb-cklink\bouffalo_flash_cube\BLFlashCommand.exe',
 [int]$LogSeconds=8, [switch]$EraseAll
)
$ErrorActionPreference='Stop'
if(!(Test-Path $Fw)){throw "Firmware not found: $Fw"}
if(!(Test-Path $Tool)){throw "Flasher not found: $Tool"}
$Fw=(Resolve-Path $Fw).ProviderPath
$results=Join-Path $PSScriptRoot '..\build\validation'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$control=[IO.Ports.SerialPort]::new($CtrlPort,115200,'None',8,'One')
$control.DtrEnable=$false
$control.RtsEnable=$false
$serial=$null
$normalBoot=$false
try {
 $control.Open()
 $control.RtsEnable=$true
 Start-Sleep -Milliseconds 1500
 $control.RtsEnable=$false
 Start-Sleep -Milliseconds 500
 $flashArgs=@('--chip=bl618dg','--port',$DataPort,'--firmware',$Fw,'--addr','0x0','--baudrate','2000000')
 if($EraseAll){$flashArgs+='--whole_chip'}
 # Windows PowerShell wraps stderr lines as ErrorRecord. Convert them back to
 # text and judge success from the native exit code AND verification result.
 $ErrorActionPreference='Continue'
 $output=& $Tool @flashArgs 2>&1 | ForEach-Object {$_.ToString()}
 $result=$LASTEXITCODE
 $ErrorActionPreference='Stop'
 $output | Set-Content (Join-Path $results 'flash.log')
 $output | Write-Output
 if($result -ne 0 -or ($output -join "`n") -notmatch 'Verification succeeded'){
  throw "Flash verification failed (exit=$result)"
 }
 $control.DtrEnable=$true
 Start-Sleep -Milliseconds 200
 $serial=[IO.Ports.SerialPort]::new($DataPort,2000000,'None',8,'One')
 $serial.Open()
 $control.RtsEnable=$true
 Start-Sleep -Milliseconds 1500
 $control.RtsEnable=$false
 $normalBoot=$true
 $text=[Text.StringBuilder]::new()
 $deadline=(Get-Date).AddSeconds($LogSeconds)
 while((Get-Date) -lt $deadline){
  $chunk=$serial.ReadExisting()
  if($chunk){[void]$text.Append($chunk);[Console]::Write($chunk)}
  Start-Sleep -Milliseconds 20
 }
 $text.ToString() | Set-Content (Join-Path $results 'boot.log')
} finally {
 if($serial -and $serial.IsOpen){$serial.Close()}
 if($control.IsOpen){
  $control.DtrEnable=$true
  if(!$normalBoot){$control.RtsEnable=$true;Start-Sleep -Milliseconds 1500}
  $control.RtsEnable=$false
  $control.Close()
 }
}
