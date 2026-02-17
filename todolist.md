# vms-empire Project Todo

## Overview
Classic Empire game with both terminal (ncurses) and SDL GUI modes.

## Completed Tasks
- [x] Fix owner values in empire.h (COMP=5, USER=1-4)
- [x] Fix city_char array in object.c
- [x] Create player-specific move_info structures
- [x] Fix attack mode (targets both uppercase/lowercase)
- [x] Add 'y' key for attack mode
- [x] Add SDL GUI support files (display_sdl.c, term_sdl.c, sdl_stubs.c)
- [x] Update Makefile for SDL build (tnw-gui target)
- [x] Add SDL function declarations to extern.h

## Pending Tasks
- [ ] Fix terminal build (add #ifdef USE_SDL guards)
- [ ] Test terminal build: `./tnw`
- [ ] Test SDL build: `./tnw-gui`
- [ ] Make SDL version fully functional

## Technical Details
- Owner values: UNOWNED=0, USER=1, USER2=2, USER3=3, USER4=4, COMP=5
- City chars: {'*', '1', '2', '3', '4', 'C'}
- Build: `make` for terminal, `make tnw-gui` for SDL
