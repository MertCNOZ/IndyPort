# Clicks a point of a process's window, in the pixels of tools\winshot.ps1's image of it (the whole window), with
# real input (SendInput), then optionally taps a key. The window is brought to the front first.
# Usage: powershell -ExecutionPolicy Bypass -File tools\clickat.ps1 -Name IndyPort -X 1370 -Y 792 [-Key 0x48] [-Right]
param([string]$Name = "IndyPort", [int]$X, [int]$Y, [string]$Key = "", [switch]$Right)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Clk {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[void][Clk]::SetProcessDPIAware()
$h = (Get-Process -Name $Name -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
[void][Clk]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 300
$r = New-Object Clk+RECT
[void][Clk]::GetWindowRect($h, [ref]$r)
[void][Clk]::SetCursorPos($r.Left + $X, $r.Top + $Y)
Start-Sleep -Milliseconds 150
if ($Right) { [Clk]::mouse_event(8, 0, 0, 0, [IntPtr]::Zero); [Clk]::mouse_event(16, 0, 0, 0, [IntPtr]::Zero) }
else { [Clk]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); [Clk]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero) }
Start-Sleep -Milliseconds 300
if ($Key) {
    $vk = [byte][Convert]::ToInt32($Key, 16)
    [Clk]::keybd_event($vk, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 80; [Clk]::keybd_event($vk, 0, 2, [IntPtr]::Zero)
}
"clicked"
