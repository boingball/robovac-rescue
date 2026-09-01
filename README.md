# RoboVac Rescue

RoboVac Rescue is an AmigaOS C top-down robot hoover game using a custom 320x256 screen, cached tile blits, masked robot BOBs, an offscreen render bitmap, a room bitmap, WaitTOF frame pacing, and smooth fixed-point movement.

## Controls

Several control schemes work at once; use whichever suits your setup.

- **Keyboard** - Player 1 moves with the arrow keys and shoots with `B`; Player 2 moves with `Z` (left), `X` (down), `C` (right), `S` (up) and shoots with `V`.
- **One-button joystick** - move and press fire. Because menus need a second action, one-button sticks confirm/start by *holding* fire for about two seconds.
- **Two-button joystick** - the second button acts as the menu confirm/start button.
- **CD32 controller** (either port, in its 2-button joystick-compatible mode) - the red button fires and, in the AI difficulty/rival menus, also selects the highlighted entry; the blue button confirms/starts menus. The pad's extra shoulder buttons and Play/Pause are not read; use `Q` to pause.
- Holding two perpendicular directions together (e.g. up + right) fires a diagonal bolt instead of a straight one.

Each joystick stays disabled until its fire button is pressed; then `J1`/`J2` appears and that stick can move and fire on menus and during gameplay.

Detailed actions:

- Player 1: arrow keys move; `B` shoots a bolt. On the title carousel, joystick 1 fire selects/focuses Player 1 instead of starting the match.
- Player 2: `Z` left, `X` down, `C` right, `S` up; `V` shoots a bolt.
- Title carousel:
  - By default this is a one-player game and Left/Right arrows choose Player 1's hoover.
  - Player 2 joins by pressing joystick 2 fire or `V`; the carousel switches to Player 2 selection.
  - Player 2 chooses with joystick 2 left/right or `Z`/`C`, then presses joystick 2 fire or `V` again to lock Player 2 and open the two-player AI rival prompt.
  - In the two-player AI prompt, choose `0`, `1`, `2`, or `3` AI rivals directly, or use Up/Down plus `Space` / Enter / the joystick blue (second) button / RMB. If you choose at least one AI rival, the next prompt asks for Easy / Normal / Hard AI difficulty.
- Start / advance menus:
  - On the title screen, start the match (or lock Player 2 if Player 2 is selecting) with `Space`/`Enter`, the joystick **blue (second) button**, RMB, or by holding fire for about two seconds. This opens the "how many AI rivals?" prompt (`0`-`3`, joystick up/down + fire/blue to choose) whether you're solo or in locked two-player mode - a joystick-only player is never stuck defaulting to 1 AI. Choosing at least one AI then opens the Easy / Normal / Hard difficulty prompt.
  - At the end of a round, continue to the next round with `Space`/`Enter`, joystick fire, or RMB.
  - At the end of a match, start a new best-of-5 with `Space`/`Enter`, joystick fire, or RMB.
- Pause: `Q` pauses during a match. The pause menu (Restart Level / Main Menu) is navigated with the arrow keys/joystick up-down and `Enter`/joystick fire/blue button; `Q` or `Esc` resumes (joystick has no resume shortcut - use the keyboard or select Main Menu).
- `R`:
  - During match: reset current round
- `1` / `2` / `3`: keyboard shortcut that skips the "how many rivals" prompt and jumps straight to the Easy / Normal / Hard difficulty prompt with that many AI rivals, from either the one-player title screen or the two-player AI prompt (where `0` is also available, and starts immediately with no AI).
- AI difficulty: Easy AI does not fire back, Normal AI fires only at close range, and Hard AI fires from longer range.
- `D`: hidden Hoover Mode: four AI hoovers clean the room using fresh random hoover variants and headings. They keep straight cleaning lines where possible, then use dirt/open-space/recent-path look-ahead to turn around walls, tables, and other hoovers. Any input returns to the title screen.
- `Esc`: quit

## Demo / Attract Mode

Leave the title screen idle for 30 seconds to start the normal attract demo: 4 AI robots (Easy difficulty, so they don't shoot each other) clean a random room on their own, looping into a fresh room whenever one finishes instead of showing the normal round/match-end screens. Press `D` for the separate Hoover Mode, which randomises the four hoovers and their headings, uses intelligent straight-line cleaning patterns, and also loops through rooms. Any keypress, joystick input, or mouse click immediately drops back to the title screen.

## Robot Super Powers

Every robot tracks a clean streak toward the super power tied to that robot variant. Standard powers trigger every 5 dirt tiles. EMP blast and dirt bomb escalate after each use, requiring 5 dirt tiles for the first trigger, 15 for the second, and 30 for later triggers:

- Dust Viper: double speed for 20 moves.
- Crumb Comet: storm bolt for 10 moves, letting the robot zap nearby rivals.
- Neon Nibbler: quad ghost for 20 moves, cleaning a 3x3 area and passing through walls/tables.
- Mote Marauder: EMP blast that briefly stuns every other robot while a fast 5-4-3-2-1 warning plays.
- Pixel Prowler: dirt bomb that drops up to 5 new dirt tiles onto open floor.
- Bristle Blitz: battery burst that instantly refills the robot.
- Static Sweep: wall smash for 20 moves, breaking interior wall tiles while moving.

The Dust Viper's speed effect leaves a short cached blitter motion trail behind the hoover. EMP also dims the room lighting, leaving the robot colours readable. Tables can be pushed one square when a hoover walks into them, provided the square behind them is clear; the winner screen records table shoves alongside score and round wins.

The HUD shows each robot's current clean-streak count, flashes `P` while a timed power is active, and announces each triggered power.

## Dirt Storm

Once per round, as the room is about to be fully cleaned, there's a chance a runaway broken hoover zips in from the left wall and sweeps across a row, scattering fresh dirt behind it (ignoring walls, tables, and docks - it just flies straight through). It does this three times, on three different random rows, each with a short pause in between, giving a round extra life instead of ending it early. Hit it with an energy bolt at any point and the whole event stops dead, worth 2 bonus points.

## Big Head Mode

Press `G` at any time during play to toggle every robot to double size, purely for laughs - it's the same visual effect as the Neon Nibbler's quad-ghost power, just switched on permanently for everyone until you press `G` again. It has no effect on collisions, cleaning radius, or anything else gameplay-related.

## Match Structure

- A solo match (no AI rivals) is 5 rounds. Every robot on the field beyond a 2-robot match adds 2 more rounds: 2 robots (e.g. 2 players, or 1 player + 1 AI rival) play 6 rounds, 3 play 8, 4 play 10, and so on.
- Each round picks a random room type.
- Dirt cleaned is scored as points.
- Hitting another robot with an energy bolt is worth 2 points. A bolted robot cannot be bolted again while stunned, and gets 2 seconds of bolt immunity after the stun ends so it can escape.
- Dirt targets scale up by active robot count: 1-2 robots get 20% more dirt than the base round target, 3 robots get 30% more, and 4+ robots get 40% more.
- Round winner = robot with the most points that round.
- After the last round, the final winner is the robot with the highest total points.
- The match-end screen zoom-pulses the winner with a large rotating robot image and shows score, round wins, and tables shoved before 2nd place, 3rd place, and the full leaderboard.

## Bonus Round

If any robot has more than 50 total points after the last regular round, the leaderboard offers an optional bonus battle. All robots enter a clean room with charging docks around the edges. A giant 3x rotating hover boss moves around the middle of the room, alternating between a diagonal bounce, a horizontal sweep, a vertical sweep, and an outward spiral from the arena centre every few seconds. Human players recharge on docks and shoot bolts at the boss; each boss hit adds 2 points to the shooter. The boss disappears after 80 hits, then the final scoreboard is shown and the winner is recalculated from the final totals. A near-full run-over gives a single 5-second stun/penalty per contact, with a cooldown so the slow-moving boss cannot stack repeated penalties while it overlaps a robot; a glancing/angled touch only stops the robot and shoves it back a tile. Walking into a teammate who is out of charge shoves them one tile further along instead of just blocking, so a stranded robot can be nudged back toward a dock. At random points the room also dips into a few seconds of near-darkness, leaving only the robots'/boss's light-coloured highlights and the on-screen status text visible.

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

Hoovers now start at a calmer base speed so a room lasts longer; the double-speed power remains noticeably faster. The title carousel keeps its normal full-speed animation after J1 is enabled.

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

The startup/title screens look for an optional uncompressed 8SVX menu-music sample at:

- `PROGDIR:samples/RoboVacRescueMenu.8svx`

On startup the whole track is loaded into a single chip-RAM buffer and looped by Paula in hardware across channels 1 and 4 while the startup title image or main title menu is visible, playing until a match starts. The track can run several minutes, more than reliably fits in chip RAM alongside the game's other assets, so if allocating a buffer for the full track fails, loading backs off to progressively shorter prefixes of it (still starting from the beginning) until one fits, rather than not playing any music at all.

Round starts and gameplay also look for optional uncompressed 8SVX samples:

- `PROGDIR:samples/getready.8svx` plays once on the first audio channel when the 3-2-1 overlay appears and is stopped when GO appears.
- `PROGDIR:samples/countdown.8svx` plays the three 3-2-1 beeps on a second audio channel and is clamped to the 3-2-1 overlay length so any trailing audio is cut before GO.
- The get-ready and countdown samples are stopped when GO appears, then `PROGDIR:samples/go.8svx` plays once on the GO audio channel while gameplay unlocks immediately.
- `PROGDIR:samples/mainmusic-lo.8svx` starts as soon as GO appears and loops using the 8SVX repeat section until the level ends, using Paula channels 1 and 4.
- `PROGDIR:samples/boltfire.8svx` plays once whenever a player fires an energy bolt, using Paula channel 3.
- `PROGDIR:samples/hoover-go-loop-low.8svx` loops while at least one hoover is moving and stops when all hoovers are stationary, using Paula channel 2.

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

RoboVac Rescue uses Bebbo's `m68k-amigaos-gcc` cross-compiler.

To compile the game binary:

```bash
make clean
make
```

The Makefile currently builds the equivalent of:

```bash
m68k-amigaos-gcc -s -Os -o robovac robovac.c
```

### Build a clean Amiga release drawer

For copying to a real Amiga or WinUAE, use the release target instead of copying the whole Git repository:

```bash
make release-clean
make clean
make release
```

This creates:

```text
release/RoboVac-Rescue/
```

containing the compiled `robovac` executable, root-level Workbench `*.info` files, `README.md`, and the runtime `tiles/` and `samples/` drawers. Source files, `.git`, GitHub Actions files and other development clutter are not copied into the release drawer.

You can override the drawer name when needed:

```bash
make release RELEASE_NAME=RoboVac-Rescue-v1.0
```

which creates:

```text
release/RoboVac-Rescue-v1.0/
```

After that, copy the generated release drawer to the Amiga rather than the repository itself.
