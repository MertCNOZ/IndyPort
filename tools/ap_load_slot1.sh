#!/usr/bin/env bash
# Starts the game (tools/ap_start.ps1) and loads save slot 1 into Ceylon like tools/ap_to_level.sh, but checks the
# window screenshots before each A: Load Game highlighted on the main menu (white text), slot 1 highlighted in the
# save book (red text). The main menu can drop a press, so a missed "down" is retried; anything else stops the
# script before a wrong A could start a New Game. Pixel regions are for a 3840x2160 window (tools/checkpx.ps1).
# Usage: bash tools/ap_load_slot1.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(cygpath -w "$ROOT")"
ap() { bash "$ROOT/tools/ap.sh" "$@"; }
px() {
    powershell -ExecutionPolicy Bypass -File "$W\\tools\\checkpx.ps1" -Src "$W\\out\\ap\\snaps\\$1.png" \
        -X "$2" -Y "$3" -W "$4" -H "$5" -Mode "$6" | tr -d '\r' | tail -1
}
powershell -ExecutionPolicy Bypass -File "$ROOT/tools/ap_start.ps1" > /dev/null &
sleep 2
bash "$ROOT/tools/ap_to_menu.sh" m > /dev/null 2>&1 || true
sleep 9           # the camera tour settles: a press during it is lost
ap press start > /dev/null
sleep 3
for try in 1 2 3; do
    ap press down > /dev/null
    sleep 1.5
    ap shot k1 > /dev/null
    lg=$(px k1 1600 1160 630 70 white)
    ng=$(px k1 1600 1050 630 70 white)
    echo "menu: Load Game white $lg, New Game white $ng"
    if awk "BEGIN{exit !($lg > 0.05 && $ng < 0.02)}"; then break; fi
    awk "BEGIN{exit !($ng > 0.05)}" || { echo "the cursor is on neither New Game nor Load Game"; exit 1; }
    [ "$try" = 3 ] && { echo "could not reach Load Game"; exit 1; }
done
ap press a > /dev/null   # the save book
sleep 4
ap shot k2 > /dev/null
s1=$(px k2 950 700 780 60 red)
s2=$(px k2 950 795 780 60 red)
echo "book: slot 1 red $s1, slot 2 red $s2"
awk "BEGIN{exit !($s1 > 0.02 && $s2 < 0.01)}" || { echo "slot 1 is not selected"; exit 1; }
for i in 1 2 3; do       # slot 1, the chapter, the mission page (back to the menu with Start selected)
    ap press a > /dev/null
    sleep 4.5
done
ap press a > /dev/null   # Start: loads the level
sleep 35
ap press start > /dev/null   # skip the opening cinematic
sleep 12
echo "in the level"
