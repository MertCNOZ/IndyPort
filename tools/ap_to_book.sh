#!/usr/bin/env bash
# From boot to the save slot book (new game path): menu (tools/ap_to_menu.sh), Start opens the options,
# A picks the first one (Start Game). Leaves a window screenshot of the book.
# Usage: bash tools/ap_to_book.sh [shot name]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/tools/ap_to_menu.sh" menu > /dev/null
sleep 3
bash "$ROOT/tools/ap.sh" press start > /dev/null   # the options
sleep 3
bash "$ROOT/tools/ap.sh" press a > /dev/null       # Start Game: the save slot book
sleep 5
bash "$ROOT/tools/ap.sh" shot "${1:-book}"
