# Posts F3 to the IndyET window, which makes the runtime log every guest thread's stack (DumpThreads).
# Usage: powershell -ExecutionPolicy Bypass -File tools\send_f3.ps1
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);
}
"@
$proc = Get-Process -Name IndyET -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { "IndyET has no window yet"; exit 1 }
$WM_KEYDOWN = 0x0100; $VK_F3 = 0x72
[void][Win]::PostMessageW($proc.MainWindowHandle, $WM_KEYDOWN, [IntPtr]$VK_F3, [IntPtr]::Zero)
"sent F3 to window $($proc.MainWindowHandle) of pid $($proc.Id)"
