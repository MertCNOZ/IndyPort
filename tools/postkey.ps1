# Posts a key press (down and up) to a process's main window, which need not be in front.
# Usage: powershell -ExecutionPolicy Bypass -File tools\postkey.ps1 <process name> <virtual key, e.g. 0x0D>
param([Parameter(Mandatory = $true)][string]$Name, [Parameter(Mandatory = $true)][string]$Key)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class PostKey {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$vk = [Convert]::ToInt32($Key, 16)
$h = (Get-Process -Name $Name -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
[void][PostKey]::PostMessage($h, 0x100, [IntPtr]$vk, [IntPtr]1)
Start-Sleep -Milliseconds 80
[void][PostKey]::PostMessage($h, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001)
