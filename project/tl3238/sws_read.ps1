param(
    [string]$BdtDirectory = 'C:\temp\tl3238_hello\bdt',
    [uint32]$Address = 0,
    [ValidateRange(1, 1048576)][int]$Size = 1048576,
    [string]$Output = (Join-Path $PSScriptRoot '..\..\build\tl3238\ota-readback.bin')
)
$ErrorActionPreference = 'Stop'
$parent = Split-Path $Output
New-Item -ItemType Directory -Force $parent | Out-Null
$parent = (Resolve-Path $parent).Path
$Output = Join-Path $parent (Split-Path $Output -Leaf)
if (Test-Path $Output) { throw 'Readback file already exists; choose a new output name' }
$log = $Output + '.log'
$tool = Join-Path $BdtDirectory 'config\Cmd_download_tool.exe'
$temporary = Join-Path $env:TEMP ('tl3238-read-' + [guid]::NewGuid().ToString('N') + '.bin')
function Bdt([string]$Arguments) {
    $si = [System.Diagnostics.ProcessStartInfo]::new()
    $si.FileName = $tool
    $si.Arguments = '1 TL323X ' + $Arguments
    $si.WorkingDirectory = $BdtDirectory
    $si.UseShellExecute = $false
    $si.RedirectStandardOutput = $true
    $si.RedirectStandardError = $true
    $p = [System.Diagnostics.Process]::Start($si)
    $text = $p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    $rc = $p.ExitCode
    $p.Dispose()
    Add-Content $log ($Arguments + "`n" + $text) -Encoding UTF8
    Write-Host (($text -split "`n" | Select-Object -Last 4) -join "`n")
    if ($rc -ne 0) { throw "BDT failed: $rc" }
    return $text
}
try {
    $null = Bdt 'sws b0 05 b0 10'
    $chip = Bdt 'rc 8014083e -s 2'
    if ($chip -notmatch '8014083e:\s*87 56') { throw 'Unexpected chip ID' }
    $null = Bdt ('rf ' + $Address.ToString('x') + ' -s ' + $Size + ' -o "_' + $temporary + '"')
    if ((Get-Item $temporary).Length -ne $Size) { throw 'Incomplete readback' }
    Copy-Item $temporary $Output
    Get-FileHash $Output -Algorithm SHA256 | Format-List
} finally {
    $null = Bdt 'rst -f'
    if (Test-Path $temporary) { Remove-Item $temporary }
}
