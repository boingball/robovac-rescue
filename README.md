# RoboVac Rescue

A tiny top-down AmigaOS game prototype where you control a robot hoover cleaning a house.

Built as a father-and-son Amiga game project.

## Current Features

- Opens in an Amiga Workbench window using Intuition (no custom screen yet)
- Single-file C codebase (`robovac.c`) targeting AmigaOS 3.x style APIs
- Simple game state flow:
  - Title / instructions screen
  - Playing
  - Won (all dirt cleaned)
  - Battery flat (ran out before finishing)
- House-like room drawing with:
  - Floor tile pattern
  - Wall blocks with simple shading
  - Furniture/table obstacles
  - Dirt spots to clean
  - Dock/charging station tile
- Arrow-key movement with simple smooth tile-to-tile animation
- Collision against walls and furniture
- Battery drain per move, recharge when stepping onto dock
- Move counter and dirt counter in status line
- Reset and quit handling

## Controls

- Arrow keys: Move robot (or start game from title screen)
- R: Reset level/start playing
- Esc: Quit
- Close gadget: Quit

## Build

```bash
m68k-amigaos-gcc -s -Os -o robovac robovac.c
```
