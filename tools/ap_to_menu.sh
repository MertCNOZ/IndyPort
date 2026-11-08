#!/usr/bin/env bash
# Drives a game started with tools/ap_start.ps1 from boot to the main menu (the throne room), then captures it.
# Usage: bash tools/ap_to_menu.sh [snap name]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/out/build/release/logs/indyet.log"
# the renderer is up once the log names the adapter
for _ in $(seq 1 300); do grep -q "D3D12 adapter" "$LOG" 2>/dev/null && break; sleep 1; done
sleep 6
bash "$ROOT/tools/ap.sh" press start   # skip the intro
sleep 4
bash "$ROOT/tools/ap.sh" press a       # the Loading page waits for a press
sleep 4
bash "$ROOT/tools/ap.sh" press start   # end the menu camera tour
sleep 5
bash "$ROOT/tools/ap.sh" snap "${1:-menu}"
