#requires -version 5
param(
  [string]$Port = 'COM3', [int]$Baud = 1500000, [string]$ResetPort = '',
  [string]$Cmds = '', [double]$ReadSec = 6, [double]$DrainSec = 0.8
)
$ErrorActionPreference = 'Stop'
if ($ResetPort) {
  $bp = New-Object System.IO.Ports.SerialPort $ResetPort,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
  $bp.Open(); $bp.DtrEnable=$false; $bp.RtsEnable=$true; Start-Sleep -Milliseconds 250; $bp.RtsEnable=$false; $bp.Close(); Start-Sleep -Milliseconds 100
}
$sp = New-Object System.IO.Ports.SerialPort $Port,$Baud,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
$sp.ReadTimeout=150; $sp.WriteTimeout=800; $sp.Open(); Start-Sleep -Milliseconds 150
$sw=[Diagnostics.Stopwatch]::StartNew(); while($sw.Elapsed.TotalSeconds -lt $DrainSec){try{$o=$sp.ReadExisting();if($o){[Console]::Out.Write($o)}}catch{};Start-Sleep -Milliseconds 40}
$cl=@(); if($Cmds){$cl=$Cmds.Split(';')}
foreach($c in $cl){ [Console]::Out.Write("`n>>> $c`n"); foreach($ch in $c.ToCharArray()){$sp.Write([string]$ch);Start-Sleep -Milliseconds 4}; $sp.Write("`r")
  $r=[Diagnostics.Stopwatch]::StartNew(); while($r.Elapsed.TotalSeconds -lt 2.0){try{$o=$sp.ReadExisting();if($o){[Console]::Out.Write($o)}}catch{};Start-Sleep -Milliseconds 30} }
$sw=[Diagnostics.Stopwatch]::StartNew(); while($sw.Elapsed.TotalSeconds -lt $ReadSec){try{$o=$sp.ReadExisting();if($o){[Console]::Out.Write($o)}}catch{};Start-Sleep -Milliseconds 40}
$sp.Close()
