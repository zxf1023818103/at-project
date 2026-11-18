param(
 [ValidateSet('bl618dg','bl616')][string]$Chip='bl618dg',
 [string]$Fw=(Join-Path $PSScriptRoot '..\build\whole_flash_data.bin'),
 [string]$DataPort='COM15', [string]$CtrlPort='COM23',
 [string]$Tool='D:\MCU_Tools\bflb-cklink\bouffalo_flash_cube\BLFlashCommand.exe',
 [int]$LogSeconds=8, [switch]$EraseAll, [switch]$SoftwareBoot, [switch]$ManualBoot
)
$ErrorActionPreference='Stop'
if($Chip -eq 'bl616' -and (!$PSBoundParameters.ContainsKey('Fw') -or !$PSBoundParameters.ContainsKey('DataPort'))){throw 'BL616 requires explicit -Fw and -DataPort; do not use BL618DG defaults'}
if($SoftwareBoot -and $ManualBoot){throw 'Choose SoftwareBoot or ManualBoot'}
$pinControl=!($SoftwareBoot -or $ManualBoot)
if($Chip -eq 'bl616' -and $pinControl -and !$PSBoundParameters.ContainsKey('CtrlPort')){throw 'BL616 requires SoftwareBoot/ManualBoot or an explicit CtrlPort'}
if(!(Test-Path $Fw)){throw "Firmware not found: $Fw"}
if($Chip -eq 'bl616' -and !$PSBoundParameters.ContainsKey('Tool')){
 $Tool=Join-Path $PSScriptRoot '..\..\..\platform\bouffalo_sdk_bl618dg\tools\bflb_tools\bouffalo_flash_cube\BLFlashCommand.exe'
}
if(!(Test-Path $Tool)){throw "Flasher not found: $Tool"}
$Fw=(Resolve-Path $Fw).ProviderPath
$results=Join-Path $PSScriptRoot '..\build\validation'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$control=$null
if($pinControl){
 $control=[IO.Ports.SerialPort]::new($CtrlPort,115200,'None',8,'One')
 $control.DtrEnable=$false
 $control.RtsEnable=$false
}
$serial=$null
$normalBoot=$false
try {
 if($pinControl){
  $control.Open()
  $control.RtsEnable=$true
  Start-Sleep -Milliseconds 1500
  $control.RtsEnable=$false
 } elseif($SoftwareBoot){
  $isp=[IO.Ports.SerialPort]::new($DataPort,2000000)
  try {$isp.Open();$isp.Write("`r`nreboot uart`r`n");Start-Sleep -Milliseconds 500} finally {$isp.Dispose()}
 }
 Start-Sleep -Milliseconds 500
 $flashArgs=@("--chip=$Chip",'--port',$DataPort,'--firmware',$Fw,'--addr','0x0','--baudrate','2000000')
 if($EraseAll){$flashArgs+='--whole_chip'}
 if(!$pinControl){$flashArgs+='--reset'}
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
 if($pinControl){$control.DtrEnable=$true}
 Start-Sleep -Milliseconds 500
 $serial=[IO.Ports.SerialPort]::new($DataPort,2000000,'None',8,'One')
 $serial.ReadBufferSize=65536
 $serial.Open()
 if($pinControl){
  $control.RtsEnable=$true
  Start-Sleep -Milliseconds 1500
  $control.RtsEnable=$false
 } else {
  Start-Sleep -Milliseconds 500
  $serial.Write("`r`nreboot`r`n")
 }
 $normalBoot=$true
 $text=[Text.StringBuilder]::new()
 $deadline=(Get-Date).AddSeconds($LogSeconds)
 while((Get-Date) -lt $deadline){
  $chunk=$serial.ReadExisting()
  if($chunk){[void]$text.Append($chunk);[Console]::Write($chunk)}
  Start-Sleep -Milliseconds 20
 }
 $text.ToString() | Set-Content (Join-Path $results 'boot.log')
 if(!$text.Length){Write-Output '[flash] No application log captured; a CKLink or manual normal boot may be required'}
} finally {
 if($serial -and $serial.IsOpen){$serial.Close()}
 if($control -and $control.IsOpen){
  $control.DtrEnable=$true
  if(!$normalBoot){$control.RtsEnable=$true;Start-Sleep -Milliseconds 1500}
  $control.RtsEnable=$false
  $control.Close()
 }
}
