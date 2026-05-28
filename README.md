# RoboVac Rescue

RoboVac Rescue is an AmigaOS C top-down robot hoover game using a custom 320x256 screen, cached tile blits, masked robot BOBs, an offscreen render bitmap, a room bitmap, WaitTOF frame pacing, and smooth fixed-point movement.

## Controls

- Arrow keys: move your robot
- `R` / `Space`:
  - On title: start match
  - During match: reset current round
  - End of round: continue to next round
  - End of match: start new best-of-5 match
- `1` / `2` / `3`: choose AI rival count from title screen
- `Esc` or RMB: quit

## Match Structure (Best of 5)

- A match is 5 rounds.
- Each round picks a random room type.
- Round winner = robot with most dirt cleaned that round.
- After round 5, final winner is decided by:
  1. Most round wins
  2. Tie-breaker: highest total dirt cleaned across all rounds

## Room Types

Randomly selected each round:

1. Living Room
2. Dining Room
3. Kitchen
4. Bathroom
5. Bedroom

Each room has a different obstacle/furniture layout on the same 20x14 grid.

## Dirt Scaling Per Round

Dirt increases each round:

- Round 1: 14
- Round 2: 20
- Round 3: 26
- Round 4: 32
- Round 5: 38

Dirt only spawns on valid floor tiles (not walls, furniture, docks, or robot spawn/dock tiles).

## Robot, Battery, and Dock Rules

- Supports player + up to 3 AI rivals.
- Every robot has its own battery.
- Player battery is managed manually by movement.
- AI robots monitor their own battery and return to their own dock when low (<= 25), then resume cleaning after recharge.
- Recharging only occurs on each robot's own dock.
- Dock positions:
  - Player: near top-left
  - AI 1: near top-right
  - AI 2: near bottom-left
  - AI 3: near bottom-right

## Tilesheet (ILBM)

The engine now attempts to load a 16-colour ILBM tilesheet from:

- `PROGDIR:tiles/world-tile.iff`

Expected layout is a horizontal `128x16` strip containing eight `16x16` tiles in this exact order:

0. floor
1. wall
2. dirt
3. dock
4. furniture/table
5. obstacle variant
6. cleaned sparkle / bonus
7. spare/marker

If loading fails (missing file, wrong format, or too small), RoboVac Rescue falls back to its built-in generated tiles so gameplay still works.

## Robot Sprites and Palette

Robot variants are loaded from `PROGDIR:tiles/airobot1.iff` through `PROGDIR:tiles/airobot7.iff`. The engine builds rotated frames at startup; `airobot7.iff` is expected to face down in its source art, so its generated directions are:

- source/down image: down-ready/load pose
- 90 degrees: left
- 180 degrees: up
- 270 degrees: right

Robot art is copied into the upper half of the 32-colour screen palette (`16-31`). The first robot IFF still provides the fallback robot colours, but the engine now also looks for a Deluxe Paint/IFF palette file at:

- `PROGDIR:tiles/robopal2.pal`

When present, its `CMAP` colours replace robot palette entries `16-31`, so a one-colour tweak such as changing DPaint palette index `13` to blue only needs to be saved in `robopal2.pal`; the colour does not need to be re-saved into every robot IFF as long as all robot images keep using the same palette indexes.

## Build

```bash
m68k-amigaos-gcc -s -Os -o robovac robovac.c
```
