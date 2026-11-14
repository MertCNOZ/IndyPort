# Real input for keyboard and mouse tests: brings a process's window to the front, then holds keys and moves the
# mouse with SendInput (what GetAsyncKeyState and raw input see; posted messages do not reach them).
# Usage: powershell -ExecutionPolicy Bypass -File tools\sendinput.ps1 -Name IndyET [-Key 0x57] [-HoldMs 1000]
#        [-MouseDx 0] [-MouseDy 0] [-Steps 50] [-Click left|right]
param([string]$Name = $(if ($env:INDY_EXE) { $env:INDY_EXE } else { "IndyET" }), [string]$Key = "", [int]$HoldMs = 500, [int]$MouseDx = 0, [int]$MouseDy = 0,
      [int]$Steps = 50, [string]$Click = "")
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Inp {
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MOUSEINPUT m; [FieldOffset(0)] public KEYBDINPUT k; }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U u; }
    [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] i, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
    public static void Key(ushort vk, bool up) {
        var i = new INPUT[1]; i[0].type = 1; i[0].u.k.vk = vk; i[0].u.k.flags = up ? 2u : 0u;
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Move(int dx, int dy) {
        var i = new INPUT[1]; i[0].type = 0; i[0].u.m.dx = dx; i[0].u.m.dy = dy; i[0].u.m.flags = 1;
        SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
    }
    public static void Button(uint down, uint up) {
        var i = new INPUT[2]; i[0].type = 0; i[0].u.m.flags = down; i[1].type = 0; i[1].u.m.flags = up;
        SendInput(2, i, Marshal.SizeOf(typeof(INPUT)));
    }
}
"@
$h = (Get-Process -Name $Name -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
[void][Inp]::ShowWindow($h, 5)
[void][Inp]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 300
if ($Key) { [Inp]::Key([Convert]::ToUInt16($Key, 16), $false) }
if ($MouseDx -or $MouseDy) {
    for ($s = 0; $s -lt $Steps; $s++) {
        [Inp]::Move([int]($MouseDx / $Steps), [int]($MouseDy / $Steps))
        Start-Sleep -Milliseconds ([int]($HoldMs / $Steps))
    }
} else { Start-Sleep -Milliseconds $HoldMs }
if ($Key) { [Inp]::Key([Convert]::ToUInt16($Key, 16), $true) }
if ($Click -eq "left") { [Inp]::Button(2, 4) }
if ($Click -eq "right") { [Inp]::Button(8, 16) }
"sent"
