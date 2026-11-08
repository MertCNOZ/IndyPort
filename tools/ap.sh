#!/usr/bin/env bash
# Drives a game started with tools/ap_start.ps1 (INDY_AUTOPILOT=out/ap, XBE_CAPTURE_DIR=out/ap/capture).
#   tools/ap.sh <command ...>         append one autopilot command, wait for its acknowledgement
#   tools/ap.sh snap <name>           capture a frame, convert it to out/ap/snaps/<name>.png, print the path
#   tools/ap.sh shot <name>           screenshot the game window into out/ap/snaps/<name>.png
#   tools/ap.sh wait <seconds>        sleep
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AP="$ROOT/out/ap"
mkdir -p "$AP/snaps" "$AP/capture"

send() {
    local before
    before=$(wc -l < "$AP/ack.txt" 2>/dev/null || echo 0)
    printf '%s\n' "$*" >> "$AP/cmd.txt"
    for _ in $(seq 1 60); do
        if [ "$(wc -l < "$AP/ack.txt" 2>/dev/null || echo 0)" -gt "$before" ]; then
            tail -1 "$AP/ack.txt"
            return 0
        fi
        sleep 0.1
    done
    echo "no acknowledgement (is the game running with INDY_AUTOPILOT?)" >&2
    return 1
}

case "$1" in
    shot)
        # what the window shows (tools/winshot.ps1), which can differ from a frame capture
        name="${2:-shot}"
        powershell -ExecutionPolicy Bypass -File "$ROOT/tools/winshot.ps1" "$AP/snaps/$name.png" > /dev/null
        echo "$AP/snaps/$name.png"
        ;;
    snap)
        name="${2:-snap}"
        last=$(ls -1 "$AP/capture" 2>/dev/null | sort | tail -1)
        send capture > /dev/null
        for _ in $(seq 1 100); do
            new=$(ls -1 "$AP/capture" 2>/dev/null | sort | tail -1)
            if [ -n "$new" ] && [ "$new" != "$last" ] && [ -f "$AP/capture/$new/final.bmp" ]; then
                sleep 0.5
                python -I "$ROOT/tools/bmp2png.py" "$AP/capture/$new/final.bmp" "$AP/snaps/$name.png"
                echo "$AP/snaps/$name.png ($new)"
                exit 0
            fi
            sleep 0.2
        done
        echo "no capture appeared" >&2
        exit 1
        ;;
    wait)
        sleep "${2:-1}"
        ;;
    *)
        send "$@"
        ;;
esac
