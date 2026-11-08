# Saves what the IndyET window shows as a PNG (PrintWindow with PW_RENDERFULLCONTENT, so it works while the
# window is behind others). This is the image a player sees, which frame captures (final.bmp) can differ from.
# Usage: powershell -ExecutionPolicy Bypass -File tools\winshot.ps1 <out.png>
param([Parameter(Mandatory = $true)][string]$Out, [string]$Name = "IndyET")
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Shot {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
# real pixels, not the display scaling's logical ones
[void][Shot]::SetProcessDPIAware()
$p = Get-Process -Name $Name -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$r = New-Object Shot+RECT
[void][Shot]::GetClientRect($p.MainWindowHandle, [ref]$r)
$w = $r.R - $r.L; $h = $r.B - $r.T
if ($w -le 0 -or $h -le 0) { "window has no client area"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
# 1 = PW_CLIENTONLY, 2 = PW_RENDERFULLCONTENT
$ok = [Shot]::PrintWindow($p.MainWindowHandle, $dc, 3)
$g.ReleaseHdc($dc)
$bmp.Save([System.IO.Path]::GetFullPath($Out), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"saved $Out (${w}x$h, PrintWindow $ok)"
