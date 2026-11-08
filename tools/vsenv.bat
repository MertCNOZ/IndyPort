@echo off
rem Runs a command with the Visual Studio 2022 Clang/CMake/Ninja toolchain on PATH.
rem Usage: tools\vsenv.bat <command> [args...]
set "VSDIR="
for %%e in (Community Professional Enterprise BuildTools) do (
    if not defined VSDIR if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%e\Common7\Tools\VsDevCmd.bat" set "VSDIR=%ProgramFiles%\Microsoft Visual Studio\2022\%%e"
)
if not defined VSDIR (
    echo Visual Studio 2022 with the C++ workload was not found.
    exit /b 1
)
rem VsDevCmd looks for vswhere.exe on PATH and complains when it is not there; the setup still works
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul 2>nul
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
cd /d "%~dp0.."
%*
