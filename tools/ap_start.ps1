# Starts the game for autopilot testing: commands from out\ap\cmd.txt (see src/common/autopilot.cpp), frame
# captures into out\ap\capture, the window size from settings.toml. Drive it with tools/ap.sh.
# When the game code was rebuilt, the "The game is ready. Start the game now?" page waits for Start: this posts
# Enter to the game's window until the renderer is up, which presses Start once the page is ready.
# Usage: powershell -ExecutionPolicy Bypass -File tools\ap_start.ps1   ($env:INDY_EXE = "IndyET_EU" starts the Europe release)
$root = Split-Path $PSScriptRoot
$ap = Join-Path $root "out\ap"
New-Item -ItemType Directory -Force (Join-Path $ap "capture") | Out-Null
New-Item -ItemType Directory -Force (Join-Path $ap "snaps") | Out-Null
Set-Content -Path (Join-Path $ap "cmd.txt") -Value "" -Encoding ascii
$exe = Join-Path $root ("out\build\release\" + $(if ($env:INDY_EXE) { $env:INDY_EXE } else { "IndyET" }) + ".exe")
$log = Join-Path (Split-Path $exe) "logs\indyet.log"
$env:INDY_AUTOPILOT = $ap
$env:XBE_CAPTURE_DIR = Join-Path $ap "capture"
# extra debug variables (XBE_TRACE_VS=104, ...) can be set in the calling shell; they pass through

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class ApWin {
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);
}
"@
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
"started pid $($p.Id)"
$deadline = (Get-Date).AddMinutes(10)
while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
    Start-Sleep -Seconds 2
    if ((Test-Path $log) -and (Select-String -Path $log -Pattern "D3D12 adapter" -Quiet)) { "renderer up"; break }
    $p.Refresh()
    if ($p.MainWindowHandle -ne 0) { [void][ApWin]::PostMessageW($p.MainWindowHandle, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero) }
}
