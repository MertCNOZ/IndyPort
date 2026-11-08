# Starts the game, waits for it to settle, logs every guest thread's stack twice (F3), then closes the game.
# Usage: powershell -ExecutionPolicy Bypass -File tools\hang_probe.ps1 [first wait s] [gap s]
param([int]$Wait = 60, [int]$Gap = 20)
$root = Split-Path $PSScriptRoot
$exe = Join-Path $root "out\build\release\IndyET.exe"
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
"started pid $($p.Id), waiting $Wait s"
Start-Sleep -Seconds $Wait
& (Join-Path $PSScriptRoot "send_f3.ps1")
Start-Sleep -Seconds $Gap
& (Join-Path $PSScriptRoot "send_f3.ps1")
Start-Sleep -Seconds 3
Stop-Process -Id $p.Id -ErrorAction SilentlyContinue
"done"
