#!/usr/bin/env bash
# From boot into the first level (Ceylon) from save slot 1: on the main menu Load Game (one below New Game),
# A on slot 1, A on the chapter (Ceylon), A on the mission page: the game takes the save and goes back to the main
# menu with Start selected, A on Start loads the level (the game restarts itself as a new process here). Then skips
# the opening cinematic and leaves a window screenshot. Needs a save in slot 1.
# Usage: bash tools/ap_to_level.sh [shot name]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ap() { bash "$ROOT/tools/ap.sh" "$@" > /dev/null; }
bash "$ROOT/tools/ap_to_menu.sh" menu > /dev/null
sleep 9          # the camera tour settles: a press during it is lost
ap press start   # the menu list (cursor on New Game)
sleep 3
ap press down    # Load Game
sleep 1
ap press a       # the save book
sleep 4
ap press a       # slot 1
sleep 4
ap press a       # chapter: Ceylon
sleep 4
ap press a       # mission page: back to the menu, Start selected
sleep 5
ap press a       # Start: loads the level
sleep 30
ap press start   # skip the opening cinematic
sleep 10
bash "$ROOT/tools/ap.sh" shot "${1:-level}"
