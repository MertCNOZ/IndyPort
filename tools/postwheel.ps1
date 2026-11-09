# Posts mouse wheel turns to a process's main window at its centre (negative notches scroll down).
# Usage: powershell -ExecutionPolicy Bypass -File tools\postwheel.ps1 <process name> <notches>
param([Parameter(Mandatory = $true)][string]$Name, [int]$Notches = -5)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Wheel {
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$h = (Get-Process -Name $Name -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
$r = New-Object Wheel+RECT
[void][Wheel]::GetWindowRect($h, [ref]$r)
$x = [int](($r.Left + $r.Right) / 2); $y = [int](($r.Top + $r.Bottom) / 2)
$step = if ($Notches -lt 0) { -120 } else { 120 }
for ($i = 0; $i -lt [math]::Abs($Notches); $i++) {
    $w = [IntPtr]([int64](($step -band 0xFFFF) -shl 16))
    $l = [IntPtr]([int64](($y -shl 16) -bor ($x -band 0xFFFF)))
    [void][Wheel]::PostMessage($h, 0x020A, $w, $l)
    Start-Sleep -Milliseconds 40
}
