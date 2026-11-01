# CredentialFile is private JSON: {"ssid":"...", "password":"..."}.
# UART echoes are captured and redacted before printing or saving output.
param([Parameter(Mandatory=$true)][string]$CredentialFile,
      [ValidateRange(2400,5900)][int]$Frequency=2467, [int]$Seconds=25,
      [string]$DataPort='COM15', [string]$ControlPort='COM23')
$ErrorActionPreference='Stop'
$credentials=Get-Content -Raw $CredentialFile | ConvertFrom-Json
if(!$credentials.ssid -or !$credentials.password){throw 'Missing SSID or password'}
foreach($value in @($credentials.ssid,$credentials.password)){
 if($value -match '["\r\n]'){throw 'Unsupported quote or newline in Wi-Fi credentials'}
}
$command='wifi_connect "'+$credentials.ssid+'" "'+$credentials.password+'" '+$Frequency
$control=[IO.Ports.SerialPort]::new($ControlPort,115200,'None',8,'One')
$control.DtrEnable=$true
$control.RtsEnable=$false
$serial=[IO.Ports.SerialPort]::new($DataPort,2000000,'None',8,'One')
$text=[Text.StringBuilder]::new()
try {
 $control.Open()
 $serial.Open()
 $serial.Write("`r`n$command`r`n")
 $deadline=(Get-Date).AddSeconds($Seconds)
 while((Get-Date) -lt $deadline){
  [void]$text.Append($serial.ReadExisting())
  Start-Sleep -Milliseconds 20
 }
} finally {
 if($serial.IsOpen){$serial.Close()}
 if($control.IsOpen){$control.DtrEnable=$true; $control.RtsEnable=$false; $control.Close()}
}
$clean=$text.ToString() -replace '\x1b\[[0-9;]*[A-Za-z]',''
$clean=$clean.Replace([string]$credentials.password,'<redacted>')
$clean -split "`r?`n" | Where-Object {$_ -notmatch 'wifi_connect|bouffalolab />'} | Write-Output
