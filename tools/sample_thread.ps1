# Samples where a thread of IndyET runs: suspends it, reads its instruction pointer, resumes it, many times, and
# prints module+offset counts. Without -ThreadId it takes the thread with the most CPU time over one second.
# Map IndyET.dll offsets to recompiled functions with tools/sample_map.py.
# Usage: powershell -ExecutionPolicy Bypass -File tools\sample_thread.ps1 [-Samples 400] [-ThreadId n] > samples.txt
param([int]$Samples = 400, [int]$ThreadId = 0)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Sampler {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenThread(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")] public static extern uint SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] public static extern uint ResumeThread(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
    // CONTEXT (x64) is 1232 bytes, 16-byte aligned; ContextFlags at 0x30, Rip at 0xF8
    public static ulong Rip(IntPtr h) {
        IntPtr raw = Marshal.AllocHGlobal(1232 + 16);
        IntPtr ctx = new IntPtr((raw.ToInt64() + 15) & ~15L);
        Marshal.WriteInt32(ctx, 0x30, 0x100001);  // CONTEXT_AMD64 | CONTEXT_CONTROL
        ulong rip = 0;
        if (GetThreadContext(h, ctx)) rip = (ulong)Marshal.ReadInt64(ctx, 0xF8);
        Marshal.FreeHGlobal(raw);
        return rip;
    }
}
"@
$p = Get-Process IndyET -ErrorAction Stop
if ($ThreadId -eq 0) {
    $a = @{}
    foreach ($t in $p.Threads) { try { $a[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {} }
    Start-Sleep 1
    $p = Get-Process IndyET
    $best = 0; $most = -1
    foreach ($t in $p.Threads) { try { $d = $t.TotalProcessorTime.TotalMilliseconds - $a[$t.Id]; if ($d -gt $most) { $most = $d; $best = $t.Id } } catch {} }
    $ThreadId = $best
}
$mods = $p.Modules | ForEach-Object { [pscustomobject]@{ Name = $_.ModuleName; Base = [uint64]$_.BaseAddress.ToInt64(); End = [uint64]$_.BaseAddress.ToInt64() + [uint64]$_.ModuleMemorySize } }
$h = [Sampler]::OpenThread(0x0002 -bor 0x0008 -bor 0x0040, $false, [uint32]$ThreadId)  # suspend/resume, get context, query
$counts = @{}
for ($i = 0; $i -lt $Samples; $i++) {
    [void][Sampler]::SuspendThread($h)
    $rip = [Sampler]::Rip($h)
    [void][Sampler]::ResumeThread($h)
    $m = $mods | Where-Object { $rip -ge $_.Base -and $rip -lt $_.End } | Select-Object -First 1
    $key = if ($m) { "{0}+{1:X}" -f $m.Name, ($rip - $m.Base) } else { "{0:X}" -f $rip }
    $counts[$key] = 1 + [int]$counts[$key]
    Start-Sleep -Milliseconds 7
}
[void][Sampler]::CloseHandle($h)
"thread $ThreadId, $Samples samples"
$counts.GetEnumerator() | Sort-Object Value -Descending | ForEach-Object { "{0,5} {1}" -f $_.Value, $_.Key }
