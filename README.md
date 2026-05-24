# RoboVac Rescue

A tiny top-down AmigaOS game prototype where you control a robot hoover cleaning a house.

Built as a father-and-son Amiga game project.

## Current Features

- Workbench window
- Top-down tile map
- Robot movement with arrow keys
- Dirt tiles that disappear when cleaned
- Wall and furniture collision
- Battery drain
- Charging dock
- Win condition when the room is clean

## Controls

- Arrow keys: Move robot
- R: Reset level
- Esc / Close gadget: Quit

## Build

```bash
m68k-amigaos-gcc -s -Os -o robovac robovac.c
