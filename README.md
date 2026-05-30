# RoboVac Rescue

RoboVac Rescue is an AmigaOS C top-down robot hoover game using a custom 320x256 screen, cached tile blits, masked robot BOBs, an offscreen render bitmap, a room bitmap, WaitTOF frame pacing, and smooth fixed-point movement.

## Controls

- Player 1: arrow keys move; `B` shoots a bolt. Joystick 1 stays disabled until joystick 1 fire is pressed, then `J1` appears and joystick 1 can move/fire on menus; mouse/joystick port 1 movement is ignored during gameplay so the mouse cannot steer Player 1.
- Player 2: `Z` left, `X` down, `C` right, `S` up; `V` shoots a bolt. Joystick 2 stays disabled until joystick 2 fire is pressed, then `J2` appears and joystick 2 can move/fire.
- Title carousel:
  - By default this is a one-player game and Left/Right arrows choose Player 1's hoover.
  - Player 2 joins by pressing joystick 2 fire or `V`; the carousel switches to Player 2 selection.
  - Player 2 chooses with joystick 2 left/right or `Z`/`C`, then presses joystick 2 fire or `V` again to lock Player 2 and open the two-player AI rival prompt.
  - In the two-player AI prompt, choose `0`, `1`, `2`, or `3` AI rivals directly, or use Up/Down plus `R` / `Space` / Enter.
- `R` / `Space`:
  - On title: start match (or lock Player 2 if Player 2 is currently selecting; locked two-player mode opens the AI rival prompt)
  - During match: reset current round
  - End of round: continue to next round
  - End of match: start new best-of-5 match
- `1` / `2` / `3`: choose AI rival count from the one-player title screen
- `0` / `1` / `2` / `3`: choose AI rival count from the two-player AI prompt
- `Esc` or RMB: quit

## Robot Super Powers

Every robot tracks a five-dirt clean streak. Whenever a robot cleans its fifth dirt tile in that streak, it triggers the super power tied to that robot variant:

- Dust Viper: double speed for 20 moves.
- Crumb Comet: storm bolt for 10 moves, letting the robot zap nearby rivals.
- Neon Nibbler: quad ghost for 20 moves, cleaning a 3x3 area and passing through walls/tables.
- Mote Marauder: EMP blast that stuns every other robot.
- Pixel Prowler: dirt bomb that drops up to 5 new dirt tiles onto open floor.
- Bristle Blitz: battery burst that instantly refills the robot.
- Static Sweep: wall smash for 20 moves, breaking interior wall tiles while moving.

The HUD shows each robot's current clean-streak count, flashes `P` while a timed power is active, and announces each triggered power.

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

- Supports one or two human players plus configurable AI rivals.
- Every robot has its own battery.
- Player battery is managed manually by movement.
- AI robots monitor their own battery and return to their own dock when low (<= 25), then resume cleaning after recharge.
- Recharging only occurs on each robot's own dock.
- Dock positions:
  - Player 1: top-left
  - Player 2: bottom-right
  - AI rivals: remaining dock positions, starting with top-right and bottom-left

## Startup Title Image

The startup/title screens also look for a 4-channel ProTracker module at:

- `PROGDIR:mods/robovac_startup.mod`

When present, the module is loaded into chip RAM and played while the startup title image or main title menu is visible. Playback is clocked from elapsed time at a fixed 120 BPM target so heavy title rendering does not drag the module tempo around. Playback stops as soon as a match starts.

On startup, the game looks for a 32-colour ILBM title image at:

- `PROGDIR:tiles/robovac-title.iff`

The expected artwork size is `300x100`, which is centred on the 320x256 custom screen. Because this image can use its own 32-colour palette, RoboVac Rescue temporarily loads the embedded title image palette for the startup splash, then fades the palette while blitting from a prebuilt rotation-frame cache before restoring the normal game palette for the main menu. Palette index `0` is treated as transparent/background by the title effect; the engine does not try to infer or remap a background colour from the image corners. The rotation frames are generated once at startup, so the intro playback path uses cached bitmaps and blitter copies rather than per-pixel redraws every frame. If the file is missing, the game skips the splash and opens directly on the menu.

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

Robot variants are loaded from `PROGDIR:tiles/airobot1.iff` through `PROGDIR:tiles/airobot7.iff`. The title screen displays these variants in a double-size player-select carousel, and the Left/Right arrows choose which variant becomes the main player robot. The engine builds rotated frames at startup; `airobot7.iff` is expected to face down in its source art, so its generated directions are:

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
