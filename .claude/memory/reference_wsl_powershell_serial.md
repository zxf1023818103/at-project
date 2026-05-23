---
name: reference-wsl-powershell-serial
description: How to drive Windows COM ports from WSL via powershell.exe + System.IO.Ports.SerialPort for AT-command / UART verification
metadata: 
  node_type: memory
  type: reference
  originSessionId: 1686892a-3cdc-44f9-ba09-65773131dc6e
---

WSL2 can't open `\\.\COM<N>` directly. To send/receive bytes on a Windows COM port from WSL, shell out to `powershell.exe` and let .NET's `System.IO.Ports.SerialPort` do the I/O.

## Path gotcha (load-bearing)

`powershell.exe -File <path>` resolves the path **on the Windows side**. Two options that both work:

1. **Script on Windows fs** — write to `/mnt/c/...` and pass the Windows-style path:
   ```bash
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Users\zxf10\AppData\Local\Temp\test_at.ps1'
   ```

2. **Script in repo (WSL fs)** — keep the .ps1 in the repo and pass via WSL UNC path:
   ```bash
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File '\\wsl.localhost\Ubuntu-24.04\home\zxf\at-project\project\amebad\flash.ps1'
   ```
   Confirmed working 2026-05-20 with AmebaD flash script. Note: when running over UNC, `$PSScriptRoot` may not expand in `param()` default values — defer to body, or use `Split-Path -Parent $MyInvocation.MyCommand.Path` as fallback. Also `Resolve-Path` rejects `..` past the UNC root; normalize paths manually instead.

Don't pass `/tmp/foo.ps1` — PowerShell treats it as a Windows path and errors out (mojibake'd: 找不到指定的文件).

## Minimal serial probe pattern

```powershell
$sp = New-Object System.IO.Ports.SerialPort('COM10', 115200, 'None', 8, 'One')
$sp.ReadTimeout  = 1500
$sp.WriteTimeout = 1000
$sp.Open()
Start-Sleep -Milliseconds 200
$null = $sp.ReadExisting()   # drain
$sp.Write("AT`r`n")
Start-Sleep -Milliseconds 400
$resp = $sp.ReadExisting()
Write-Output ($resp -replace "`r","<CR>" -replace "`n","<LF>")
$sp.Close()
```

Use `Write` (not `WriteLine`) so you control CR/LF exactly. Always sleep between Write and ReadExisting — `ReadExisting` is non-blocking and returns "" if the device hasn't replied yet.

## Discovering which COM is which

```powershell
Get-PnpDevice -Class Ports | Where-Object { $_.Status -eq 'OK' } | Select FriendlyName,InstanceId | Format-List
```

`InstanceId` carries the USB VID/PID so you can map a port to a specific adapter (FTDI / CH340 / CMSIS-DAP CDC) without unplugging. **The DAP-Link's built-in USB-CDC is not necessarily the AT UART** — it's usually wired to LOGUART. Identify the AT UART by probing each candidate with `AT\r\n` and looking for `\r\nOK\r\n` (see [[reference-ameba-openocd-flash]] for current Ameba Dplus mapping).

## When to use which approach

- **One-shot query** ("does AT respond?"): inline `powershell.exe -NoProfile -Command "..."` for a few commands; switch to `-File` once the script grows past 2 lines.
- **Multi-command test matrix**: write to `/mnt/c/Users/<user>/AppData/Local/Temp/*.ps1`, run via `-File`. Easier to iterate, no quoting hell.
- **Streaming logs**: `screen //./COMx 115200` from Windows side, or open the port in PowerShell and loop on `ReadLine()`. Not practical purely from WSL.
