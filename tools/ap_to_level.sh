#!/usr/bin/env bash
# From boot into the first level (Ceylon) on save slot 1: menu, Start (options), A (Start: the save book),
# A (slot 1), A (chapter Ceylon), A (mission page), then waits for the level and leaves a window screenshot.
# Needs a profile in slot 1 (made once through New Game, see PORT_NOTES).
# Usage: bash tools/ap_to_level.sh [shot name]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ap() { bash "$ROOT/tools/ap.sh" "$@"; }
bash "$ROOT/tools/ap_to_menu.sh" menu > /dev/null
sleep 3
ap press start > /dev/null   # options
sleep 3
ap press a > /dev/null       # Start: the save book
sleep 5
ap press a > /dev/null       # slot 1
sleep 4
ap press a > /dev/null       # chapter: Ceylon
sleep 4
ap press a > /dev/null       # mission page
sleep 25
ap shot "${1:-level}"
