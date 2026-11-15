#requires -version 5
[CmdletBinding()]
param([string]$Port='COM42',[int]$Baud=1500000,[string[]]$Cmds=@(),
      [int]$PreRead=1,[int]$PostRead=3,[int]$GapMs=1200,[switch]$Reset,[string]$Log)
$ErrorActionPreference='Stop'
$p=New-Object System.IO.Ports.SerialPort $Port,$Baud,'None',8,'One'
$p.Handshake='None';$p.RtsEnable=$false;$p.DtrEnable=$false;$p.Open()
$buf=New-Object System.Text.StringBuilder
function Pump([double]$Seconds) {
    $end=(Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $end) { [void]$buf.Append($p.ReadExisting()); Start-Sleep -Milliseconds 20 }
}
try {
    if ($Reset) { $p.RtsEnable=$true;Start-Sleep -Milliseconds 100;$p.RtsEnable=$false }
    Pump $PreRead
    foreach ($cmd in $Cmds) {
        [void]$buf.Append("`n>>> $cmd`n")
        foreach ($c in $cmd.ToCharArray()) { $p.Write([string]$c);Start-Sleep -Milliseconds 3 }
        $p.Write("`r");Pump ($GapMs/1000.0)
    }
    Pump $PostRead
} finally { $p.Close();$p.Dispose() }
if ($Log) { [IO.File]::WriteAllText($Log,$buf.ToString()) }
Write-Output $buf.ToString()
