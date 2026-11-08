@echo off
rem Opens Ghidra with its own JDK 25, without changing the system Java.
rem Usage: tools\ghidra.bat            (GUI)
rem        tools\ghidra.bat headless <args for analyzeHeadless>
rem Run it from a normal terminal: Ghidra's plugin cache fails inside a sandboxed shell.
set "GHIDRA=D:\Tools\ghidra_12.2_PUBLIC"
set "JAVA_HOME=D:\Tools\jdk-25.0.5+8"
set "PATH=%JAVA_HOME%\bin;%PATH%"
if /i "%~1"=="headless" goto :headless
start "" "%GHIDRA%\ghidraRun.bat"
exit /b 0

:headless
shift
set ARGS=
:collect
if "%~1"=="" goto :run
set ARGS=%ARGS% "%~1"
shift
goto :collect
:run
call "%GHIDRA%\support\analyzeHeadless.bat" %ARGS%
exit /b %errorlevel%
