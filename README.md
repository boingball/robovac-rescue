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

## Build

```bash
m68k-amigaos-gcc -s -Os -o robovac robovac.c
```
