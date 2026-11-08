# Writes a minidump (threads, stacks, module list) of the running IndyET.exe, for tools/stack_scan.py.
# Usage: powershell -ExecutionPolicy Bypass -File tools\dump_process.ps1 [out.dmp]
param([string]$Out = "out\hang.dmp")

$proc = Get-Process -Name IndyET -ErrorAction Stop | Select-Object -First 1
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Dbg {
    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool MiniDumpWriteDump(IntPtr hProcess, uint processId, IntPtr hFile, uint dumpType,
                                                IntPtr exceptionParam, IntPtr userStreamParam, IntPtr callbackParam);
}
"@
$path = [System.IO.Path]::GetFullPath($Out)
$fs = [System.IO.File]::Create($path)
try {
    # MiniDumpWithThreadInfo (0x1000) | MiniDumpWithDataSegs (0x1) | MiniDumpNormal: stacks, threads, modules
    $ok = [Dbg]::MiniDumpWriteDump($proc.Handle, [uint32]$proc.Id, $fs.SafeFileHandle.DangerousGetHandle(), 0x1001,
                                   [IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero)
} finally {
    $fs.Close()
}
if ($ok) { "wrote $path ($((Get-Item $path).Length) bytes) for pid $($proc.Id)" } else { "MiniDumpWriteDump failed" }
