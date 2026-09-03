#ifndef ROBOVAC_H
#define ROBOVAC_H

/*
 * RoboVac Rescue - optimized custom-screen engine
 *
 * Adds:
 * - Smooth pixel movement between tiles using fixed-point positions
 * - Up to 10 robots
 * - AI rival robots that compete to clean dirt
 * - Per-robot coloured cached BOB sprites
 * - Pre-rendered tile cache
 * - Persistent room/background bitmap
 * - Offscreen render bitmap
 * - WaitTOF() frame loop
 *
 * Source layout:
 *   robovac.h    - shared types, constants, globals and every function
 *                  prototype, included by every .c file below.
 *   robovac.c    - program entry point (main), screen/window setup, raw
 *                  input polling; #includes the module files below so the
 *                  whole game still compiles as a single translation unit.
 *   game.c       - core game state: rooms, dirt, robot movement, powerups,
 *                  the bonus boss fight, match/round flow.
 *   ai.c         - AI move/fire decisions for rival robots and the boss.
 *   render.c     - all drawing/blitting, sprite and tile caches, palette
 *                  effects, the dirty-rect presentation system.
 *   audio.c      - sample and music playback, audio channel management.
 *   minigames.c  - RoboRace, RoboPuck and Bumper Bots specific logic.
 *   network.c    - reserved for future multiplayer/networking code.
 *
 * Build:
 *   m68k-amigaos-gcc -s -Os -o robovac robovac.c
 *   (robovac.c #includes the other .c files, so only it is ever passed
 *   to the compiler - see the Makefile.)
 *
 * Controls:
 *   Arrow keys / B - move player 1 robot / fire bolts
 *   Z/X/C/S / V   - move player 2 robot / fire bolts
 *   Joysticks     - disabled until that joystick fire is pressed (J1/J2 shown)
 *                 Title menu also enables J1/J2 from fire so the carousel can be used
 *                 On-screen J1/P1 uses the physical joystick port: JOY1DAT + CIAA PRA bit 7
 *                 On-screen J2/P2 uses the mouse port: JOY0DAT + CIAA PRA bit 6
 *   P2 fire/V     - arm two-player carousel selection from title screen
 *   Space/Return/RMB/blue/hold fire - select/start; R resets the active level
 *   Q          - open in-game restart/quit menu
 *   0/1/2/3    - choose AI rivals from two-player AI prompt
 *   1/2/3      - choose one-player AI rivals from title screen
 *   4          - cycle game speed on title screen (low/normal/high)
 *   1/2/3,E/N/H - choose Easy/Normal/Hard when AI difficulty is prompted
 *   O          - hidden 9-rival mode (battle mode)
 *   D          - hidden Hoover Mode (straight-pattern cleaning showcase)
 *   Esc       - quit
 */


#include <exec/types.h>

#include <exec/memory.h>

#include <exec/execbase.h>

#include <intuition/intuition.h>

#include <graphics/rastport.h>

#include <graphics/view.h>

#include <graphics/gfxmacros.h>

#include <graphics/copper.h>

#include <graphics/videocontrol.h>

#include <proto/exec.h>

#include <proto/intuition.h>

#include <proto/graphics.h>

#include <proto/dos.h>

#include <proto/datatypes.h>



#include <dos/dos.h>

#include <datatypes/pictureclass.h>

#include <datatypes/datatypes.h>

#include <hardware/custom.h>

#include <hardware/cia.h>

#include <hardware/dmabits.h>

#include <utility/tagitem.h>



#include <stdio.h>

#include <string.h>


extern struct Custom custom;

extern struct CIA ciaa;


static const char __attribute__((used)) min_stack[] = "$STACK:65536";



#define SCREEN_W    320

#define SCREEN_H    256

#define DEPTH       5



#ifndef USE_DIRTY_RECTS

#define USE_DIRTY_RECTS 1

#endif

#ifndef DIRTY_RECT_DEBUG_PRINTF

#define DIRTY_RECT_DEBUG_PRINTF 0

#endif



#define DIRTY_RECT_MAX_DRAW_RECTS 14

#define DIRTY_RECT_LOW_MAX_DRAW_RECTS 8

#define DIRTY_RECT_FALLBACK_AREA ((SCREEN_W * SCREEN_H) * 3 / 5)

#define DIRTY_RECT_LOW_FALLBACK_AREA ((SCREEN_W * SCREEN_H) / 2)

#define DIRTY_RECT_CLOSE_MERGE_PAD 6

#define DIRTY_RECT_LOW_CLOSE_MERGE_PAD 10

#define DIRTY_RECT_CLOSE_MERGE_MAX_AREA 4096

#define DIRTY_RECT_STRIP_MAX 4

#define DIRTY_RECT_STRIP_MIN_SAVED_RECTS 3

#define DIRTY_RECT_STRIP_MAX_HEIGHT 72



#define TILE_SIZE   16

#define MAP_W       20

#define MAP_H       14



#define HUD_H       32

#define MAP_X       0

#define MAP_Y       HUD_H



#define FP_SHIFT    8

#define FP_ONE      (1 << FP_SHIFT)

#define TO_FP(x)    ((x) << FP_SHIFT)

#define FP_TO_INT(x) ((x) >> FP_SHIFT)



#define MOVE_SPEED  (2 * FP_ONE)

#define DOUBLE_SPEED_MOVE_SPEED  (4 * FP_ONE)

#define EMERGENCY_MOVE_SPEED  (1 * FP_ONE)

#define EMERGENCY_DOCK_MOVES  5

#define DOCK_CHARGE_TICKS     250

#define POWERUP_CLEAN_TARGET    5

#define POWERUP_CLEAN_TARGET_2  15

#define POWERUP_CLEAN_TARGET_3  30

#define POWERUP_DURATION_MOVES  20

#define POWERUP_BOLT_MOVES      10

#define POWERUP_EMP_STEP_FRAMES  17

#define POWERUP_EMP_TICKS       (5 * POWERUP_EMP_STEP_FRAMES)

#define POWERUP_DIRT_DROP       5

#define POWERUP_QUAD_RADIUS     1

#define ROBOT_TURN_TICKS        1

#define SPEED_FLASH_TICKS       36

#define SPEED_TRAIL_FRAME_COUNT 4

#define SPEED_TRAIL_W           24

#define SPEED_TRAIL_H           24

#define HOOVER_RANDOM_TURN_CHANCE 8

#define BONUS_SCORE_THRESHOLD    50


/* Robo Party intermissions.  The first mini-game deliberately reuses the
 * normal arena, robot BOBs, fixed-point movement and input loop: only the
 * rules and map change.  This keeps the feature friendly to classic Amigas
 * and leaves a small enum/switch seam for Puck and Dirt Dash later. */

#define MINIGAME_NONE            0

#define MINIGAME_RACE            1

#define MINIGAME_PUCK            2

#define MINIGAME_BUMPER          3

#define MINIGAME_AIRHOCKEY       4

#define MINIGAME_BOWLING         5

#define MINIGAME_FLOODHOUSE      6

#define MINIGAME_COUNT           6

#define MINIGAME_INTRO_TICKS     100

#define RACE_LAPS                2

#define RACE_TIME_TICKS          (40 * 50)

#define RACE_FINISH_GRACE_TICKS  (5 * 50)

#define RACE_CHECKPOINT_COUNT    4

#define RACE_MOVE_SPEED          (3 * FP_ONE)

#define RACE_BOOST_SPEED         (5 * FP_ONE)

#define RACE_SLICK_SPEED         (1 * FP_ONE)

#define RACE_BOOST_MOVES         8

#define RACE_BUMP_STUN_TICKS     6

#define RACE_TURN_TICKS          5

#define RACE_DRIFT_PIXELS        4


/* RoboPuck keeps the robots on the familiar tile grid, but the puck itself
 * uses fixed-point, per-frame movement and a masked BOB.  That gives the
 * little goal-scoring intermission smooth motion without adding a
 * second general-purpose physics engine. */

#define PUCK_W                    12

#define PUCK_H                    12

#define PUCK_TIME_TICKS           (180 * 50)

#define PUCK_GOALS_TO_WIN         3

#define PUCK_ROBOT_MOVE_SPEED     (3 * FP_ONE)

#define PUCK_KICK_SPEED           (6 * FP_ONE)

#define PUCK_MAX_SPEED            (7 * FP_ONE)

#define PUCK_HIT_COOLDOWN_TICKS   4

#define PUCK_GOAL_PAUSE_TICKS     40

#define PUCK_GOAL_TOP_TILE        4

#define PUCK_GOAL_BOTTOM_TILE     9

#define DIRT_CLEAN_BATTERY_COST   1


/* RoboHockey: a narrow Shufflepuck-style table, goals on the top and
 * bottom walls instead of RoboPuck's left/right. Team 0 defends the top
 * goal and is confined to the top half of the table, team 1 mirrors it on
 * the bottom - RobotCanPassTile refuses any move that would cross the
 * halfway row, so unlike RoboPuck a paddle can never leave its own side.
 * The fire button doubles as an EMP-charged power shot: standing next to
 * the puck with the move off cooldown sends it rocketing away far faster
 * than an ordinary touch. */

#define AIRHOCKEY_W                    PUCK_W

#define AIRHOCKEY_H                    PUCK_H

#define AIRHOCKEY_TIME_TICKS           (120 * 50)

#define AIRHOCKEY_GOALS_TO_WIN         5

#define AIRHOCKEY_ROBOT_MOVE_SPEED     (3 * FP_ONE)

#define AIRHOCKEY_KICK_SPEED           (6 * FP_ONE)

#define AIRHOCKEY_MAX_SPEED            (8 * FP_ONE)

#define AIRHOCKEY_HIT_COOLDOWN_TICKS   4

#define AIRHOCKEY_GOAL_PAUSE_TICKS     40

#define AIRHOCKEY_GOAL_LEFT_TILE       7

#define AIRHOCKEY_GOAL_RIGHT_TILE      12

#define AIRHOCKEY_HALF_Y               7

#define AIRHOCKEY_BOOST_SPEED          (11 * FP_ONE)

#define AIRHOCKEY_BOOST_COOLDOWN_TICKS (5 * 50)

#define AIRHOCKEY_BOOST_RANGE          2

#define AIRHOCKEY_BOOST_FLASH_TICKS    20

#define AIRHOCKEY_BOOST_STUN_TICKS     15


/* Robo Bowling: every robot gets its own private lane running the length of
 * the map, walled off from its neighbours, with its own rack of ten "pin"
 * tables (reusing TILE_TABLE) at the far end. A shared rack in the middle of
 * the room meant whoever was already closest got there first every time;
 * separate lanes make it a fair, individual game like real bowling.
 *
 * Standing anywhere in your lane and firing (the same bolt used to bump
 * rivals in Bumper Bots) launches a ball straight down the lane - see the
 * MINIGAME_BOWLING branch in StepPlayerBolt, which checks a small spread
 * around the bolt's path each tick so a well-aimed throw can clear more
 * than one pin, and lets the bolt travel through knocked pins instead of
 * stopping dead on the first one. Ranking and points reuse Bumper Bots'
 * pattern (a per-robot score, top three get 3/2/1) instead of a two-team
 * split, since each robot's total is now entirely its own.
 *
 * Each pin cluster is a compact 2-wide x 5-tall block (10 pins) rather than
 * the old wide 7-column triangle, so BOWLING_LANE_WIDTH lanes side by side
 * (pins plus a one-tile wall divider) fit across the room; see StartRoboBowling
 * for how lanes are actually laid out for the current robotCount. */
#define BOWLING_TIME_TICKS          (90 * 50)

#define BOWLING_PIN_COUNT           10

#define BOWLING_PIN_COLS            2

#define BOWLING_PIN_ROWS            5

#define BOWLING_PIN_BASE_Y          2

#define BOWLING_LANE_WIDTH          3

#define BOWLING_LAUNCH_ROW          11

#define BOWLING_FLASH_TICKS         40


/* Flood House: everyone gets a fixed home tile and a 30-second build phase.
 * Loose blocks are scattered on the floor; walking onto ANY block (loose,
 * or sitting in a rival's fort) steals one level of it into your carry
 * (max 2), and bumping a rival who is currently carrying also steals
 * straight out of their carry slots. Fire places one carried block onto
 * the tile you are facing, stacking up to 3 high. None of this touches
 * map[][] - block state is its own per-tile height grid, since a floor
 * tile needs to hold 0-3 and a tile type constant cannot - so a block
 * never blocks a walk-through the way a wall or table would.
 *
 * When the timer runs out, survival is per-robot and structural: if every
 * one of the 4 tiles orthogonally adjacent to YOUR home has a block on it
 * (any height, 1 or more), the water can't reach your tile and you are
 * "surrounded" - safe. Missing even one side floods you. Ranking and
 * points reuse Bumper Bots' pattern (a per-robot rank score, top three get
 * 3/2/1): surrounded robots always outrank flooded ones, tiebroken within
 * each group by total blocks owned (carried + built on your own walls). */
#define FLOODHOUSE_BUILD_TICKS       (30 * 50)

#define FLOODHOUSE_CARRY_MAX         2

#define FLOODHOUSE_STACK_MAX         3

/* A full house needs exactly 4 (one per side); 3 loose blocks per robot -
 * less than a single robot needs to wall up alone, let alone with rivals
 * free to steal - meant every robot ran dry with a permanent gap in its
 * wall and nothing left anywhere on the floor to top up with. 6 leaves
 * enough spare per robot, after the 4 a full house costs, for the steal
 * mechanic to actually matter instead of just being a race to starvation. */
#define FLOODHOUSE_LOOSE_PER_ROBOT   6

#define FLOODHOUSE_FLASH_TICKS       40

#define FLOODHOUSE_PALETTE_TICKS     100

#define FLOODHOUSE_FLOOD_PAUSE_TICKS (3 * 50)

/* How fast the floor-tint overlay covers the whole map once the flood
 * hits (see DrawFloodWaterOverlay) - a handful of frames, not the whole
 * pause, so it reads as the floor suddenly flooding rather than a tide
 * slowly climbing into view. */
#define FLOODHOUSE_FLOOD_RISE_TICKS  8

#define FLOODHOUSE_EVENT_NONE        0

#define FLOODHOUSE_EVENT_STOLE       1

#define FLOODHOUSE_EVENT_BUILT       2

#define FLOODHOUSE_EVENT_RAIDED      3


/* Bumper Bots: a tiny floor "rug" island sits on the same TILE_WALL border
 * art used for room walls, but here it reads as the surrounding abyss - the
 * arena is deliberately small so a shoulder-bump or a bolt hit can shove a
 * rival clean off the edge. Walking off voluntarily is blocked the normal
 * way (RobotCanPassTile refuses TILE_WALL); only a push can send a robot
 * out of bounds, at which point it is eliminated instead of relocated. */

#define BUMPER_ARENA_MIN_X       6

#define BUMPER_ARENA_MAX_X       13

#define BUMPER_ARENA_MIN_Y       3

#define BUMPER_ARENA_MAX_Y       10

#define BUMPER_TIME_TICKS        (60 * 50)

#define BUMPER_BUMP_PUSH_TILES   1

#define BUMPER_BOLT_PUSH_TILES   3

#define BUMPER_BUMP_STUN_TICKS   6

#define BUMPER_ELIMINATION_POINTS 5

#define BUMPER_ELIM_FLASH_TICKS  80

#define BUMPER_AI_FIRE_CHANCE    20

#define BUMPER_AI_FIRE_RANGE     8

#define BUMPER_FALL_TICKS        20
#define BUMPER_SLIDE_SPEED       (5 * FP_ONE)


/* Dirt Storm: a runaway "broken" hoover that zips across a row, ignoring
 * walls/tables, scattering fresh dirt as it goes. Fires once per round,
 * just as the room is about to be fully cleaned, to stretch play out a
 * little - shooting it with a bolt cancels the whole event. */

#define DIRT_STORM_SPEED             (4 * FP_ONE)

#define DIRT_STORM_PASSES            3

#define DIRT_STORM_GAP_TICKS         30

#define DIRT_STORM_TRIGGER_DIRT_LEFT 5

#define DIRT_STORM_TRIGGER_CHANCE_PCT 50

#define DIRT_STORM_DROP_CHANCE_PCT   65

#define DIRT_STORM_STOP_POINTS       5

#define BONUS_BOSS_MAX_HEALTH    50

#define BONUS_BOSS_HIT_POINTS    2

#define BONUS_BOSS_SCALE         3

#define BONUS_BOSS_TOUCH_STUN_TICKS 100

/* A glancing/angled touch only needs to cover most of the robot's box before
 * it counts as a full run-over stun; anything less is just a shove. */

#define BONUS_BOSS_STUN_OVERLAP_NUM 3

#define BONUS_BOSS_STUN_OVERLAP_DEN 4

#define BONUS_BOSS_GRAZE_COOLDOWN_TICKS 20


/* Boss movement patterns: the old build only ever bounced diagonally.
 * BOSS_PATTERN_SPIN opens outward from the arena centre; the others sweep a
 * single axis. StepBonusBoss picks a random mode every BOSS_PATTERN_*_TICKS
 * window so a run sees a mix instead of only diagonals. */

#define BOSS_PATTERN_DIAGONAL   0

#define BOSS_PATTERN_HORIZONTAL 1

#define BOSS_PATTERN_VERTICAL   2

#define BOSS_PATTERN_SPIN       3

#define BOSS_PATTERN_MODE_COUNT 4

#define BOSS_PATTERN_MIN_TICKS 200

#define BOSS_PATTERN_MAX_TICKS 400

#define BOSS_SPIN_RADIUS_STEPS 48

#define BOSS_SPIN_RADIUS_GROW_TICKS 4

#define BOLT_STUN_STEP_FRAMES    17

#define BOLT_STUN_TICKS          (5 * BOLT_STUN_STEP_FRAMES)

#define BOLT_STUN_DAMAGE         5

#define BOLT_ESCAPE_IMMUNE_TICKS  (2 * BOLT_STUN_STEP_FRAMES)

#define MAIN_AI_FIRE_CHANCE      18

#define MAIN_AI_FIRE_RANGE_NORMAL 3

#define MAIN_AI_FIRE_RANGE_HARD   8

/* Easy-difficulty tile-cleaning AI ignores its own best pathfound move and
 * wanders a random passable direction instead on 4 out of 5 turns, so it
 * stops beelining straight to every dirt tile like Normal/Hard do. */

#define EASY_AI_CONFUSION_CHANCE 5

#define EASY_AI_CONFUSION_ROLL   4

#define BONUS_BOSS_TOUCH_COOLDOWN_TICKS 100

#define BONUS_BOSS_FIRE_INTERVAL_TICKS 50

#define MAX_BOSS_BOLTS 12

#define BONUS_AI_FIRE_INTERVAL_TICKS 25

#define BONUS_BOSS_EXPLOSION_TICKS 70



#define ROBOT_W     16

#define ROBOT_H     16

#define BOLT_FRAME_COUNT 8

#define ROBOT_SCALE2_W (ROBOT_W * 2)

#define ROBOT_SCALE2_H (ROBOT_H * 2)

#define MAX_ROBOTS  10

#define MAX_DIRTY_RECTS 96

#define MAX_HUMAN_PLAYERS 2

#define JOY_CONFIRM_HOLD_FRAMES 100  /* 2 seconds at PAL 50Hz */

#define ROBOT_VARIANTS 7

#define AI_RECENT_TILE_COUNT 4



#define START_X     1

#define START_Y     1



#define TILE_FLOOR  0

#define TILE_WALL   1

#define TILE_DIRT   2

#define TILE_DOCK   3

#define TILE_TABLE     4

#define TILE_OBSTACLE  5

#define TILE_CLEAN     6

#define TILE_MARKER    7

#define TILE_COUNT     8

#define WALL_ROTATION_COUNT 4

#define FLOOR_ROTATION_COUNT 4

#define TILE_CACHE_COUNT (TILE_COUNT + WALL_ROTATION_COUNT + FLOOR_ROTATION_COUNT)



#define GAME_INTRO      0

#define GAME_TITLE      1

#define GAME_PLAYING    2

#define GAME_ROUND_END  3

#define GAME_MATCH_END  4

#define GAME_BONUS_PLAYING 5

#define GAME_BONUS_END 6

#define GAME_MINIGAME_INTRO 7

#define GAME_MINIGAME_PLAYING 8

#define GAME_MINIGAME_END 9



#define RAW_ESC     0x45

#define RAW_LEFT    0x4F

#define RAW_RIGHT   0x4E

#define RAW_UP      0x4C

#define RAW_DOWN    0x4D

#define RAW_Q       0x10

#define RAW_R       0x13

#define RAW_SPACE   0x40

#define RAW_RETURN  0x44

#define RAW_0       0x0A

#define RAW_1       0x01

#define RAW_2       0x02

#define RAW_3       0x03

#define RAW_4       0x04

#define RAW_O       0x18
#define RAW_P       0x19

#define RAW_B       0x35

#define RAW_N       0x36

#define RAW_E       0x12

#define RAW_S       0x21

#define RAW_H       0x25

#define RAW_Z       0x31

#define RAW_X       0x32

#define RAW_C       0x33

#define RAW_V       0x34

#define RAW_D       0x22

#define RAW_G       0x24


/* Attract/demo mode: idles on the title screen this long with no input
 * before it kicks in on its own (D also starts it immediately). */

#define TITLE_IDLE_DEMO_TICKS (30 * 50)



#define TITLE_CAROUSEL_Y 172

#define TITLE_ROBOT_SCALE 2

#define INTRO_TITLE_W 300

#define INTRO_TITLE_H 100

#define INTRO_HOLD_FRAMES 90

#define INTRO_EFFECT_FRAMES 64

#define INTRO_SLICE_H 4

#define INTRO_TOTAL_FRAMES (INTRO_HOLD_FRAMES + INTRO_EFFECT_FRAMES)

#define TITLE_SPIN_STEPS 32

#define TITLE_SPIN_STEPS_LOW 16

#define TITLE_ROT_W (ROBOT_W * TITLE_ROBOT_SCALE)

#define TITLE_ROT_H (ROBOT_H * TITLE_ROBOT_SCALE)

#define TITLE_COPPER_PEN 1

#define TITLE_COPPER_BANDS 32

#define TITLE_DIRTY_TOP (TITLE_CAROUSEL_Y - 24)

#define TITLE_MENU_DIRTY_TOP 66

#define TITLE_FORCED_FULL_PRESENT_FRAMES 3



#define MENU_MUSIC_SAMPLE_PATH "PROGDIR:samples/RoboVacRescueMenu.8svx"

#define GET_READY_SAMPLE_PATH "PROGDIR:samples/getready.8svx"

#define COUNTDOWN_SAMPLE_PATH "PROGDIR:samples/countdown.8svx"

#define GO_SAMPLE_PATH "PROGDIR:samples/go.8svx"

#define MAIN_MUSIC_SAMPLE_PATH "PROGDIR:samples/mainmusic-lo.8svx"

#define BOLT_FIRE_SAMPLE_PATH "PROGDIR:samples/boltfire.8svx"

#define GOAL_SAMPLE_PATH "PROGDIR:samples/goal.8svx"

#define HOOVER_MOVE_SAMPLE_PATH "PROGDIR:samples/hoover-go-loop-low.8svx"

/*
 * Paula channel ownership is deliberately partitioned so state changes can
 * hand channels over without accidental overlap on real hardware:
 *   countdown: ch0 get ready, ch1 number countdown, ch2 go, ch3 silent
 *   gameplay:  ch0/ch3 main game music, ch1 hoover movement loop,
 *              ch2 bolt/fire effects
 */

#define MENU_MUSIC_LEFT_AUDIO_CHANNEL 0

#define MENU_MUSIC_RIGHT_AUDIO_CHANNEL 3

#define MAIN_MUSIC_LEFT_AUDIO_CHANNEL 0

#define MAIN_MUSIC_RIGHT_AUDIO_CHANNEL 3

#define HOOVER_MOVE_AUDIO_CHANNEL 1

#define GET_READY_AUDIO_CHANNEL 0

#define COUNTDOWN_AUDIO_CHANNEL 1

#define GO_AUDIO_CHANNEL 2

#define BOLT_FIRE_AUDIO_CHANNEL 2
#define GOAL_AUDIO_CHANNEL 2

#define PAULA_CLOCK_HZ 3546895UL

#define ROUND_COUNTDOWN_SECONDS 3

#define ROUND_COUNTDOWN_STEP_FRAMES 12

#define ROUND_COUNTDOWN_FRAMES (ROUND_COUNTDOWN_SECONDS * ROUND_COUNTDOWN_STEP_FRAMES)

#define ROUND_COUNTDOWN_TOTAL_FRAMES ROUND_COUNTDOWN_FRAMES

#define ROUND_GO_FRAMES 24

#define ROUND_GO_TEXT_SCALE 6

#define ROUND_GO_TEXT_W (2 * 4 * ROUND_GO_TEXT_SCALE)

#define ROUND_GO_TEXT_H (5 * ROUND_GO_TEXT_SCALE)

#define ROUND_GO_TEXT_LEFT ((SCREEN_W - ROUND_GO_TEXT_W) / 2)

#define ROUND_GO_TEXT_TOP ((SCREEN_H - ROUND_GO_TEXT_H) / 2)

#define ROUND_START_OVERLAY_LEFT 44

#define ROUND_START_OVERLAY_TOP 66

#define ROUND_START_OVERLAY_W 233

#define ROUND_START_OVERLAY_H 115

#define ROUND_START_OVERLAY_COUNT 4

#define EMP_ROBOT_VISUAL_W 7

#define EMP_ROBOT_VISUAL_H 7

#define EMP_PALETTE_CYCLE_FRAMES 4

#define EMP_FLOOR_PEN 9

#define EMP_ROBOT_LIGHT_PEN_A 29

#define EMP_ROBOT_LIGHT_PEN_B 30

#define EMP_ROOM_DIM_DIVISOR  3


/* Boss-fight "night mode": at random points the room dims to near-black,
 * with only the robots' light pens (and the status-text pens that ride on
 * top of them) left bright, then it fades back after a few seconds. */

#define NIGHT_MODE_MIN_DURATION_TICKS 150

#define NIGHT_MODE_MAX_DURATION_TICKS 300

#define NIGHT_MODE_MIN_GAP_TICKS 400

#define NIGHT_MODE_MAX_GAP_TICKS 900

#define NIGHT_MODE_DIM_DIVISOR 5



#ifndef DMAF_SETCLR

#define DMAF_SETCLR 0x8000

#endif

#ifndef DMAF_AUD0

#define DMAF_AUD0 0x0001

#define DMAF_AUD1 0x0002

#define DMAF_AUD2 0x0004

#define DMAF_AUD3 0x0008

#endif

#ifndef INTF_AUD0

#define INTF_AUD0 0x0080

#endif

#ifndef INTF_AUD3

#define INTF_AUD3 0x0400

#endif

#ifndef INTF_SETCLR

#define INTF_SETCLR 0x8000

#endif

/* Paula's AUDxLEN is a 16-bit word count, so a single DMA block cannot
 * describe the full menu track. Keep the source in normal/Fast RAM and copy
 * it through two modest chip-RAM buffers, swapping only when Paula reports
 * that the previous block actually finished. */

#define MENU_MUSIC_STREAM_CHUNK_BYTES 32768UL

#define MENU_MUSIC_INT_MASK (INTF_AUD0 | INTF_AUD3)


struct Robot {
    WORD tileX;
    WORD tileY;
    WORD targetX;
    WORD targetY;
    LONG px;
    LONG py;
    LONG targetPx;
    LONG targetPy;
    WORD battery;
    WORD score;
    WORD stunTicks;
    WORD boltImmuneTicks;
    BOOL boltStunned;
    WORD emergencyMovesLeft;
    WORD chargeTicks;
    WORD cleanStreak;
    WORD powerCleanTarget;
    WORD powerUseCount;
    WORD powerMovesLeft;
    UBYTE powerType;
    BOOL ai;
    BOOL moving;
    UBYTE spriteIndex;
    UBYTE prevSpriteIndex;
    WORD turnTicks;
    WORD turnDirection;
    UBYTE spriteVariant;
    WORD tablesPushed;
};


struct RaceCheckpoint {
    UBYTE minX;
    UBYTE maxX;
    UBYTE minY;
    UBYTE maxY;
    UBYTE targetX;
    UBYTE targetY;
};




struct OneShotSample {
    UBYTE *data;
    LONG dataSize;
    UWORD lengthWords;
    UWORD period;
    UWORD sampleRate;
    UWORD playbackTicks;
    WORD ticksRemaining;
    UWORD loopStartWords;
    UWORD loopLengthWords;
    UBYTE volume;
    BOOL loaded;
    BOOL playing;
};



static struct Screen *scr = NULL;

static struct Window *win = NULL;


static struct RastPort renderRP;

static struct BitMap *renderBM = NULL;


static struct RastPort roomRP;

static struct BitMap *roomBM = NULL;


static struct RastPort tileRP;

static struct BitMap *tileCacheBM = NULL;


static struct RastPort roundOverlayRP;

static struct BitMap *roundOverlayBM = NULL;


static struct RastPort robotRP;

static struct BitMap *robotCacheBM = NULL;

static struct BitMap *robotMaskBM = NULL;

static struct RastPort boltRP;

static struct BitMap *boltCacheBM = NULL;

static struct BitMap *boltMaskBM = NULL;

static struct RastPort robotScaledRP;

static struct BitMap *robotScaledCacheBM = NULL;

static struct BitMap *robotScaledMaskBM = NULL;

static struct RastPort speedTrailRP;

static struct BitMap *speedTrailBM = NULL;

static struct BitMap *speedTrailMaskBM = NULL;

static struct RastPort puckRP;

static struct BitMap *puckBM = NULL;

static struct BitMap *puckMaskBM = NULL;

static struct RastPort titleCarouselRP;

static struct BitMap *titleCarouselBM = NULL;

static struct BitMap *titleCarouselMaskBM = NULL;

static WORD titleCarouselFrameCount = 0;

static WORD titleCarouselPhaseFrame[TITLE_SPIN_STEPS];

static struct RastPort titleStaticRP;

static struct BitMap *titleStaticBM = NULL;

static struct RastPort bonusBossCacheRP;

static struct BitMap *bonusBossCacheBM = NULL;

static struct BitMap *bonusBossCacheMaskBM = NULL;

static BOOL titleStaticDirty = TRUE;

static BOOL titlePanelDirty = TRUE;

static BOOL titleFullPresentPending = TRUE;

static WORD titleFullPresentFrames = 0;

static BOOL titleFirstFullPresentDone = FALSE;

static BOOL titleStaticCacheAllocFailedLogged = FALSE;


enum GameSpeed {
    GAME_SPEED_LOW = 0,
    GAME_SPEED_NORMAL = 1,
    GAME_SPEED_HIGH = 2
};


static enum GameSpeed gameSpeed = GAME_SPEED_NORMAL;

static WORD gameSpeedFrameCounter = 0;

static const char *gameSpeedNames[3] = { "LOW 50", "NORMAL 66", "HIGH 100" };


enum TitleEffectQuality {
    EFFECT_LOW = 0,
    EFFECT_NORMAL = 1,
    EFFECT_HIGH = 2
};


static WORD titleCarouselDesiredFrameCount = TITLE_SPIN_STEPS;

static WORD titleSpinAdvanceDivisor = 1;

static WORD titleSpinAdvanceCounter = 0;

static enum TitleEffectQuality effectQuality = EFFECT_NORMAL;

static const char *titleFxModeName = "normal";

static const char *detectedCpuName = "unknown";

static struct RastPort introTitleRP;

static struct BitMap *introTitleBM = NULL;

static WORD introTitleW = 0;

static WORD introTitleH = 0;

static WORD introTicks = 0;

static BOOL introPaletteActive = FALSE;

static BOOL titleCopperActive = FALSE;

static struct UCopList *titleUCopList = NULL;

static UWORD introPalette[32];

static struct OneShotSample menuMusicSample;

static UBYTE *menuMusicChunkBuf[2] = {NULL, NULL};

static ULONG menuMusicSourceBytes = 0;

static ULONG menuMusicNextOffsetBytes = 0;

static WORD menuMusicCurrentBuf = 0;

static UWORD menuMusicSavedAudioIntena = 0;

static BOOL menuMusicIntenaSaved = FALSE;

static struct OneShotSample getReadySample;

static struct OneShotSample countdownSample;

static struct OneShotSample goSample;

static struct OneShotSample mainMusicSample;

static struct OneShotSample boltFireSample;

static struct OneShotSample goalSample;

static struct OneShotSample hooverMoveSample;


static UBYTE map[MAP_H][MAP_W];


static struct Robot robots[MAX_ROBOTS];

static WORD aiPrevTileX[MAX_ROBOTS];

static WORD aiPrevTileY[MAX_ROBOTS];

static WORD aiRecentTileX[MAX_ROBOTS][AI_RECENT_TILE_COUNT];

static WORD aiRecentTileY[MAX_ROBOTS][AI_RECENT_TILE_COUNT];

static WORD aiTargetDirtX[MAX_ROBOTS];

static WORD aiTargetDirtY[MAX_ROBOTS];

static WORD robotCount = 2;

static WORD aiRivals = 1;

static WORD humanPlayers = 1;

static WORD selectedPlayerVariant[MAX_HUMAN_PLAYERS] = {0, 1};

static WORD titleSelectPlayer = 0;

static BOOL titleTwoPlayerArmed = FALSE;

static BOOL titlePlayer2Locked = FALSE;

static WORD titleSpinPhase = 0;

static BOOL hooverModeActive = FALSE;

/* Hidden Big Head mode (toggle with G): purely a visual gag, drawn via the
 * same 2x scaled sprite/dirty-rect path already used for the Quad powerup.
 * It must never touch the POWER_QUAD gameplay checks (wall-phasing, area
 * cleaning) - only where a robot's sprite is drawn or dirty-rect-sized. */
static BOOL bigHeadMode = FALSE;

static WORD hooverModeDir[MAX_ROBOTS];

static WORD speedFlashTicks[MAX_ROBOTS];

static UWORD *audioSilenceWord = NULL;

static BOOL demoModeActive = FALSE;

static BOOL demoJoyPrimed = FALSE;

/* Party Mode: a one-key shortcut from the title screen (P) straight into a
 * match of nothing but Robo Party rounds - every mini-game exactly once, in
 * a shuffled order, then the usual match-end leaderboard. Built for fast
 * testing/showing off the party games without playing full cleaning rounds
 * in between. */
static BOOL partyModeActive = FALSE;

static WORD partyModeQueue[MINIGAME_COUNT];

static WORD partyModeQueueIndex = 0;

static BOOL demoPrevLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL demoPrevRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL demoPrevUp[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL demoPrevDown[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL demoPrevFire[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL demoPrevBlue[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static WORD titleIdleTicks = 0;




#ifndef AFF_68020

#define AFF_68020 (1L << 1)

#endif

#ifndef AFF_68030

#define AFF_68030 (1L << 2)

#endif

#ifndef AFF_68040

#define AFF_68040 (1L << 3)

#endif

#ifndef AFF_68060

#define AFF_68060 (1L << 7)

#endif



/* Kept near the title animation because the carousel deliberately reacts to
 * whether the physical J1 has been activated. */
static BOOL joyEnabled[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};


enum AudioOwner {
    AUDIO_OWNER_NONE = 0,
    AUDIO_OWNER_MENU_MUSIC,
    AUDIO_OWNER_ROUND_VOICE,
    AUDIO_OWNER_MAIN_MUSIC,
    AUDIO_OWNER_HOOVER_LOOP,
    AUDIO_OWNER_BOLT_FIRE,
    AUDIO_OWNER_GOAL
};


static UBYTE audioChannelOwner[4] = {
    AUDIO_OWNER_NONE, AUDIO_OWNER_NONE, AUDIO_OWNER_NONE, AUDIO_OWNER_NONE
};


struct DirtPos { WORD x, y; };

static struct DirtPos dirtList[MAP_W * MAP_H];

static WORD dirtListCount = 0;

static BOOL dirtListValid = FALSE;

static WORD dirtLeft = 0;

static BOOL dirtStormActive = FALSE;

static BOOL dirtStormTriggeredThisRound = FALSE;

static LONG dirtStormPx = 0;

static WORD dirtStormTileY = 0;

static WORD dirtStormLastDropTileX = 0;

static WORD dirtStormPassesLeft = 0;

static WORD dirtStormGapTicks = 0;

static UBYTE dirtStormVariant = 0;

static WORD dirtStormSpinPhase = 0;

static WORD moves = 0;

static WORD maxBattery = 110;

static WORD batteryCostPerMove = 1;

static BOOL EnableTitleCopperGradient(void);

static WORD gameState = GAME_TITLE;


static WORD roundIndex = 0;

static WORD roundWins[MAX_ROBOTS] = {0,0,0,0};

static WORD totalScores[MAX_ROBOTS] = {0,0,0,0};

static WORD roundScores[MAX_ROBOTS] = {0,0,0,0};

static WORD totalTablePushes[MAX_ROBOTS] = {0,0,0,0};

static WORD roundWinner = -1;

static WORD finalWinner = -1;

static WORD miniGameType = MINIGAME_NONE;

static WORD miniGameIntroTicks = 0;

static WORD miniGameWinner = -1;

static WORD miniGamePoints[MAX_ROBOTS];

static WORD lastMiniGame = MINIGAME_NONE;

static WORD raceTicksRemaining = 0;

static WORD raceFinishGraceTicks = 0;

static WORD raceLap[MAX_ROBOTS];

static WORD raceNextCheckpoint[MAX_ROBOTS];

static WORD racePlace[MAX_ROBOTS];

static WORD raceBoostMoves[MAX_ROBOTS];

static WORD raceFinishCount = 0;

static LONG puckPx = 0;

static LONG puckPy = 0;

static LONG puckVx = 0;

static LONG puckVy = 0;

static WORD puckTicksRemaining = 0;

static WORD puckTeamScore[2] = {0, 0};

static WORD puckGoalPauseTicks = 0;

static WORD puckHitCooldownTicks = 0;

static WORD puckLastTouch = -1;

static WORD puckScoringTeam = -1;

static WORD puckScoringRobot = -1;

static LONG airhockeyPuckPx = 0;

static LONG airhockeyPuckPy = 0;

static LONG airhockeyPuckVx = 0;

static LONG airhockeyPuckVy = 0;

static WORD airhockeyTicksRemaining = 0;

static WORD airhockeyTeamScore[2] = {0, 0};

static WORD airhockeyGoalPauseTicks = 0;

static WORD airhockeyHitCooldownTicks = 0;

static WORD airhockeyLastTouch = -1;

static WORD airhockeyScoringTeam = -1;

static WORD airhockeyScoringRobot = -1;

static WORD airhockeyBoostCooldown[MAX_ROBOTS];

static WORD airhockeyBoostFlashTicks = 0;

/* Each robot bowls its own lane with its own private rack - bowlingPinX/Y
 * are per-robot (row i = robot i's 10 pins), reset in place between frames
 * rather than shared across the whole map. */
static WORD bowlingPinX[MAX_ROBOTS][BOWLING_PIN_COUNT];

static WORD bowlingPinY[MAX_ROBOTS][BOWLING_PIN_COUNT];

static WORD bowlingPinsStanding[MAX_ROBOTS];

static WORD bowlingLaneBaseX[MAX_ROBOTS];

static WORD bowlingLaunchX[MAX_ROBOTS];

static WORD bowlingLaunchY[MAX_ROBOTS];

/* 0 = about to throw the frame's first ball, 1 = first ball already thrown
 * this frame and some pins are still standing, so the next throw is the
 * frame's second ball (real bowling's "spare" attempt) before the lane
 * resets fresh. */
static WORD bowlingBallsThisFrame[MAX_ROBOTS];

static BOOL bowlingBallInFlight[MAX_ROBOTS];

static WORD bowlingScore[MAX_ROBOTS];

static WORD bowlingTicksRemaining = 0;

static WORD bowlingLastKnockedRobot = -1;

static WORD bowlingFlashTicks = 0;

static UBYTE floodBlockHeight[MAP_H][MAP_W];

static WORD floodCarried[MAX_ROBOTS];

static BOOL floodSurrounded[MAX_ROBOTS];

static WORD floodTicksRemaining = 0;

static WORD floodPauseTicks = 0;

static WORD floodLooseRemaining = 0;

static WORD floodFlashTicks = 0;

static WORD floodLastEventRobot = -1;

static WORD floodLastEventKind = FLOODHOUSE_EVENT_NONE;

static WORD floodPaletteTicks = 0;

static WORD bumperTicksRemaining = 0;

static WORD bumperAliveCount = 0;

static BOOL bumperEliminated[MAX_ROBOTS];

static WORD bumperEliminatedSeq[MAX_ROBOTS];

static WORD bumperEliminationSeq = 0;

static WORD bumperEliminatedRobot = -1;

static WORD bumperEliminatedBy = -1;

static WORD bumperEliminatedFlashTicks = 0;

static WORD bumperFallTicks[MAX_ROBOTS];

/* A bump/bolt push relocates a robot's logical tile instantly (so the
 * pusher's own move into the vacated tile stays correctly timed, exactly
 * like the existing table/race-bump shoves), but the on-screen sprite
 * eases from the old position to the new one at BUMPER_SLIDE_SPEED
 * instead of snapping there, so a push reads as a shove rather than a
 * teleport. bumperVisualPx/Py hold the in-flight drawn position while
 * bumperSliding[id] is set; DrawRobotBob uses them instead of the
 * (already-updated) logical robots[id].px/py for that robot meanwhile. */
static LONG bumperVisualPx[MAX_ROBOTS];
static LONG bumperVisualPy[MAX_ROBOTS];
static BOOL bumperSliding[MAX_ROBOTS];

static BOOL bonusAvailable = FALSE;

static WORD bonusBossHealth = 0;

static WORD bonusBossX = 0;

static WORD bonusBossY = 0;

static WORD bonusBossDx = 1;

static WORD bonusBossDy = 1;

static WORD bonusBossPatternMode = BOSS_PATTERN_DIAGONAL;

static WORD bonusBossPatternTicks = 0;

static WORD bonusBossSpinAngle = 0;

static WORD bonusBossSpinRadiusStep = 0;

static WORD bonusBossSpinRadiusDir = 1;

static WORD bonusBossSpinRadiusTickCounter = 0;

static WORD bonusBossPhase = 0;

static WORD bonusBossFacingState = 2;
 /* SPR_DOWN is declared later in the sprite enum. */
static WORD bonusBossFireTicks = 0;

static WORD bonusAiFireTicks = 0;

static WORD bonusBossExplosionTicks = 0;

static WORD bonusBossExplosionX = 0;

static WORD bonusBossExplosionY = 0;

static WORD bonusBossTouchCooldown[MAX_ROBOTS];

static BOOL bonusBossTouching[MAX_ROBOTS];

static WORD roomType = 0;

static BOOL running = TRUE;

static BOOL pauseMenuOpen = FALSE;

static WORD pauseMenuSelection = 0;

static BOOL aiSelectMenuOpen = FALSE;

static WORD aiSelectMenuSelection = 0;

static BOOL aiDifficultyMenuOpen = FALSE;

static WORD aiDifficultyMenuSelection = 1;

static WORD aiDifficultyPendingPlayers = 1;

static WORD aiDifficultyPendingRivals = 0;

static WORD aiDifficulty = 1;

static WORD roundCountdownTicks = 0;

static WORD roundGoTicks = 0;

static WORD roundCountdownLastSoundNumber = 0;

static BOOL roundGoSoundPlayed = FALSE;


static BOOL keyLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL keyRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL keyUp[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL keyDown[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL joyLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL joyRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL joyUp[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL joyDown[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL joyFirePrev[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static WORD joyFireHoldTicks[MAX_HUMAN_PLAYERS] = {0, 0};

static BOOL joyFireHoldTriggered[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL joyBluePrev[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL pauseJoyUpPrev[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static BOOL pauseJoyDownPrev[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

static WORD playerFacingX[MAX_HUMAN_PLAYERS] = {0, 0};

static WORD playerFacingY[MAX_HUMAN_PLAYERS] = {-1, -1};

static char lastPowerText[80] = "";

static WORD lastPowerTicks = 0;

static char cachedBossHpText[32] = "";

static BOOL dirtyBossHpText = TRUE;

static char cachedHudStatusText[160] = "";

static BOOL dirtyHudStatusText = TRUE;

static WORD empCountdownTicks = 0;

static WORD empCountdownOwner = -1;

static BOOL empPaletteCycleActive = FALSE;

static WORD empPaletteCyclePhase = -1;

static BOOL nightModeActive = FALSE;

static WORD nightModeTicks = 0;

static WORD nightModeCooldownTicks = 0;


struct Bolt {
    BOOL active;
    WORD dirX;
    WORD dirY;
    LONG px;
    LONG py;
    WORD ttl;
};

static struct Bolt playerBolts[MAX_ROBOTS];

static struct Bolt bossBolts[MAX_BOSS_BOLTS];


struct DirtyRect {
    WORD x;
    WORD y;
    WORD w;
    WORD h;
};
#if USE_DIRTY_RECTS
static struct DirtyRect dirtyRects[MAX_DIRTY_RECTS];
static WORD dirtyRectCount = 0;
static BOOL dirtyRectsValid = FALSE;
static BOOL dirtyRectsBuiltForFrame = FALSE;
static BOOL dirtyForceFullFrame = TRUE;
static ULONG fallbackFullFrameCount = 0;
static ULONG dirtyFrameCount = 0;
static ULONG dirtyRectTotal = 0;
static WORD dirtyRectPreMergeCount = 0;
static WORD dirtyRectPostMergeCount = 0;
static ULONG dirtyRectAreaTotal = 0;
static ULONG dirtyRectLastArea = 0;
static ULONG dirtyStripFrameCount = 0;
static LONG dirtyPrevRobotPx[MAX_ROBOTS];
static LONG dirtyPrevRobotPy[MAX_ROBOTS];
static BOOL dirtyPrevRobotValid[MAX_ROBOTS];
static UBYTE dirtyPrevRobotSpriteIndex[MAX_ROBOTS];
static UBYTE dirtyPrevRobotPrevSpriteIndex[MAX_ROBOTS];
static UBYTE dirtyPrevRobotPowerType[MAX_ROBOTS];
static WORD dirtyPrevRobotPowerMovesLeft[MAX_ROBOTS];
static WORD dirtyPrevRobotStunTicks[MAX_ROBOTS];
static WORD dirtyPrevRobotBattery[MAX_ROBOTS];
static WORD dirtyPrevRobotTurnTicks[MAX_ROBOTS];
static WORD dirtyPrevRobotTurnDirection[MAX_ROBOTS];
static UBYTE dirtyPrevRobotQuadActive[MAX_ROBOTS];
static UBYTE dirtyPrevRobotTileUnder[MAX_ROBOTS];
static WORD dirtyPrevRobotSpeedFlashTicks[MAX_ROBOTS];
static WORD dirtyPrevRobotFloodCarried[MAX_ROBOTS];
static LONG dirtyPrevPlayerBoltPx[MAX_ROBOTS];
static LONG dirtyPrevPlayerBoltPy[MAX_ROBOTS];
static BOOL dirtyPrevPlayerBoltActive[MAX_ROBOTS];
static LONG dirtyPrevBossBoltPx[MAX_BOSS_BOLTS];
static LONG dirtyPrevBossBoltPy[MAX_BOSS_BOLTS];
static BOOL dirtyPrevBossBoltActive[MAX_BOSS_BOLTS];
static WORD dirtyPrevBossX = 0;
static WORD dirtyPrevBossY = 0;
static WORD dirtyPrevBossHealth = 0;
static WORD dirtyPrevBossExplosionTicks = 0;
static WORD dirtyPrevDirtLeft = -1;
static WORD dirtyPrevMoves = -1;
static WORD dirtyPrevBattery[MAX_ROBOTS];
static WORD dirtyPrevScore[MAX_ROBOTS];
static WORD dirtyPrevStunTicks[MAX_ROBOTS];
static WORD dirtyPrevPowerMoves[MAX_ROBOTS];
static UBYTE dirtyPrevPowerType[MAX_ROBOTS];
static WORD dirtyPrevLastPowerTicks = -1;
static WORD dirtyPrevEmpCountdown[MAX_ROBOTS];
static WORD dirtyPrevEmpScreenX[MAX_ROBOTS];
static WORD dirtyPrevEmpScreenY[MAX_ROBOTS];
static BOOL dirtyPrevEmpVisible[MAX_ROBOTS];
static WORD dirtyPrevEmpTicks = -1;
static WORD dirtyPrevRoundGoTicks = -1;
static WORD dirtyPrevCountdownTicks = -1;
static WORD dirtyPrevRobotCount = -1;
static WORD dirtyPrevRaceSecond = -1;
static WORD dirtyPrevRaceLap[MAX_ROBOTS];
static WORD dirtyPrevRacePlace[MAX_ROBOTS];
static LONG dirtyPrevPuckPx = 0;
static LONG dirtyPrevPuckPy = 0;
static BOOL dirtyPrevPuckValid = FALSE;
static WORD dirtyPrevPuckSecond = -1;
static WORD dirtyPrevPuckScore[2] = {-1, -1};
static WORD dirtyPrevPuckScoringTeam = -2;
static LONG dirtyPrevAirhockeyPuckPx = 0;
static LONG dirtyPrevAirhockeyPuckPy = 0;
static BOOL dirtyPrevAirhockeyPuckValid = FALSE;
static WORD dirtyPrevAirhockeySecond = -1;
static WORD dirtyPrevAirhockeyScore[2] = {-1, -1};
static WORD dirtyPrevAirhockeyScoringTeam = -2;
static WORD dirtyPrevBumperSecond = -1;
static WORD dirtyPrevBumperAlive = -1;
static WORD dirtyPrevBumperFlashTicks = -1;
static WORD dirtyPrevBowlingSecond = -1;
/* A running total across every robot's score is enough to catch any change
 * cheaply - scores only ever count up during a round, so the sum can never
 * go stale without the HUD noticing. */
static WORD dirtyPrevBowlingScoreSum = -1;
static WORD dirtyPrevBowlingFlashTicks = -1;
static WORD dirtyPrevFloodSecond = -1;
static WORD dirtyPrevFloodLooseRemaining = -1;
static WORD dirtyPrevFloodFlashTicks = -1;
static LONG dirtyPrevDirtStormPx = 0;
static WORD dirtyPrevDirtStormTileY = 0;
static BOOL dirtyPrevDirtStormValid = FALSE;
#endif


static ULONG rng = 0x1234ABCD;


static const WORD robotStartXOnePlayer[MAX_ROBOTS] = {1, 18, 1, 18, 17, 2, 1, 18, 17, 18};

static const WORD robotStartYOnePlayer[MAX_ROBOTS] = {1, 1, 11, 11, 10, 10, 10, 10, 11, 10};

static const WORD robotDockXOnePlayer[MAX_ROBOTS]  = {1, 18, 1, 18, 17, 1, 1, 18, 18, 18};

static const WORD robotDockYOnePlayer[MAX_ROBOTS]  = {1, 1, 11, 11, 11, 11, 11, 11, 11, 11};

static const WORD robotStartXTwoPlayer[MAX_ROBOTS] = {1, 18, 18, 1, 17, 2, 1, 18, 17, 18};

static const WORD robotStartYTwoPlayer[MAX_ROBOTS] = {1, 11, 1, 11, 10, 10, 10, 10, 11, 10};

static const WORD robotDockXTwoPlayer[MAX_ROBOTS]  = {1, 18, 18, 1, 17, 1, 1, 18, 18, 18};

static const WORD robotDockYTwoPlayer[MAX_ROBOTS]  = {1, 11, 1, 11, 11, 11, 11, 11, 11, 11};

/* The first four (the common case: solo plus up to three AI rivals) share
 * one column, so all four start with equal forward progress toward the
 * first gate - a 3-per-column-then-spill-over layout put the 4th racer in
 * a new column at the SAME row as the 1st, one tile further along the
 * track than everyone else, which read as starting in front of them. */
static const WORD raceStartX[MAX_ROBOTS] = {5, 5, 5, 5, 6, 6, 6, 7, 7, 7};

static const WORD raceStartY[MAX_ROBOTS] = {9, 10, 11, 12, 9, 10, 11, 9, 10, 11};


/* Spread around the rug with a one-tile buffer from the abyss edge, so
 * nobody starts a shove away from falling straight off. */
static const WORD bumperStartX[MAX_ROBOTS] = {7, 9, 11, 7, 12, 7, 12, 7, 9, 11};

static const WORD bumperStartY[MAX_ROBOTS] = {5, 5, 5, 6, 6, 7, 7, 8, 8, 8};


/* Two rows of five home tiles, three columns apart - plenty of clearance
 * for each home's own 4-tile perimeter fort to never overlap a neighbour's. */
static const WORD floodHomeX[MAX_ROBOTS] = {3, 6, 9, 12, 15, 3, 6, 9, 12, 15};

static const WORD floodHomeY[MAX_ROBOTS] = {4, 4, 4, 4, 4, 9, 9, 9, 9, 9};


/* Clockwise gates around the central island.  Robots start just beyond the
 * start/finish gate and must visit 1, 2, 3, then 0 to complete a lap.
 * Gate 0 is pulled back off row 12 - one tile shy of the bottom wall - so
 * it reads as a gate on the open floor rather than tucked into the corner
 * pocket down there. */
static const struct RaceCheckpoint raceCheckpoints[RACE_CHECKPOINT_COUNT] = {
    {4, 4, 8, 11, 4, 9},
    {15, 18, 8, 8, 16, 8},
    {14, 14, 1, 3, 14, 2},
    {1, 4, 4, 4, 3, 4}
};


static UWORD palette[32] = {
    0x000, 0x222, 0xA52, 0xE84,
    0x28F, 0x742, 0x888, 0xFFF,
    0x444, 0x6A6, 0x0F8, 0x0AA,
    0xF22, 0xFD0, 0x7DF, 0xB0F,
    0x000, 0x111, 0x222, 0x333,
    0x444, 0x555, 0x666, 0x777,
    0x888, 0x999, 0xAAA, 0xBBB,
    0xCCC, 0xDDD, 0xEEE, 0xFFF
};


enum RobotSpriteState {
    SPR_READY = 0,
    SPR_UP = 1,
    SPR_DOWN = 2,
    SPR_LEFT = 3,
    SPR_RIGHT = 4,
    SPR_CHARGING = 5,
    SPR_LOW_BATTERY = 6,
    SPR_ENERGY_BOLT = 7,
    SPR_STATE_COUNT = 8
};


/* Ordered to match a clockwise 45-degree rotation step (k = frame index),
 * so BuildBoltCache can bake all eight from the same rotation sampler. */
enum BoltSpriteFrame {
    BOLT_FRAME_RIGHT = 0,
    BOLT_FRAME_DOWN_RIGHT = 1,
    BOLT_FRAME_DOWN = 2,
    BOLT_FRAME_DOWN_LEFT = 3,
    BOLT_FRAME_LEFT = 4,
    BOLT_FRAME_UP_LEFT = 5,
    BOLT_FRAME_UP = 6,
    BOLT_FRAME_UP_RIGHT = 7
};


static const char *roomNames[5] = {
    "Living Room", "Dining Room", "Kitchen", "Bathroom", "Bedroom"
};


static const char *robotVariantNames[ROBOT_VARIANTS] = {
    "Dust Viper",
    "Crumb Comet",
    "Neon Nibbler",
    "Mote Marauder",
    "Pixel Prowler",
    "Bristle Blitz",
    "Static Sweep"
};


static const char *robotVariantTags[ROBOT_VARIANTS] = {
    "VIP", "COM", "NIB", "MAR", "PIX", "BLZ", "SWP"
};


enum PowerType {
    POWER_NONE = 0,
    POWER_DOUBLE_SPEED,
    POWER_BOLT,
    POWER_QUAD,
    POWER_EMP,
    POWER_DIRT_DROP,
    POWER_BATTERY_BURST,
    POWER_WALL_SMASH
};


static const char *powerNames[ROBOT_VARIANTS] = {
    "DOUBLE SPEED",
    "STORM BOLT",
    "QUAD GHOST",
    "EMP BLAST",
    "DIRT BOMB",
    "BATTERY BURST",
    "WALL SMASH"
};


static const WORD roundDirtTargets[5] = {14, 20, 26, 32, 38};

static void StepPlayerBolts(void);

static void StepBossBolts(void);

static void MaybeStartDirtStorm(void);

static void StepDirtStorm(void);

static void StopDirtStorm(WORD stopperId);

static void DrawDirtStorm(void);

static void StepBonusAiFire(void);

static void StepMainGameAiFire(void);

static BOOL ActivateSpaceOrFireAction(void);

static BOOL EnableTitleCopperGradient(void);

static void DrawTitleCarousel(void);

static void DrawHud(void);

static WORD EmpRobotCountdownNumber(WORD id);

static BOOL RobotLowBatteryWarningActive(WORD id);

static WORD EmpRobotVisualState(WORD id);

static void EmpRobotVisualAnchor(WORD id, WORD *outSx, WORD *outSy);

static void GetEmpRobotVisualRectFromScreen(WORD sx, WORD sy, struct DirtyRect *rect);

static BOOL GetEmpRobotVisualRect(WORD id, struct DirtyRect *rect);

static void DrawEmpRobotVisual(WORD id);

static void DrawEmpRobotVisuals(void);

static void MarkTitlePanelDirty(void);

static void MarkTitleAllDirty(void);

static void PrepareTitlePresentation(void);

static void RequestTitleFullPresents(void);

static WORD MiniTextWidth(const char *s, WORD scale);

static void MiniText(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen);

static void TriggerRobotPower(WORD id);

static WORD NextPowerCleanTarget(WORD useCount);

static WORD SpawnDirtTiles(WORD count);

static void EnterTitleScreen(void);

static void ClosePauseMenu(void);

static void CloseAiSelectMenu(void);

static void OpenAiSelectMenu(WORD initialSelection);

static void CloseAiDifficultyMenu(void);

static void OpenAiDifficultyMenu(WORD players, WORD rivals);

static void StartMatch(WORD players, WORD rivals);

static void StartDemoMode(void);

static void StartHooverMode(void);

static void StartPartyMode(void);

static WORD MatchRoundCount(void);

static void StartMiniGameIntro(void);

static void StartRoboRace(void);

static void StepRoboRace(void);

static void FinishRoboRace(void);

static void RaceHandleRobotArrival(WORD id);

static void ChooseRaceAiMove(WORD id);

static void StartRoboPuck(void);

static void StepRoboPuck(void);

static void FinishRoboPuck(void);

static void ChoosePuckAiMove(WORD id);

static WORD PuckTeamForRobot(WORD id);

static void StartAirHockey(void);

static void StepAirHockey(void);

static void FinishAirHockey(void);

static void ChooseAirHockeyAiMove(WORD id);

static WORD AirHockeyTeamForRobot(WORD id);

static BOOL TryAirHockeyBoost(WORD id);

static void StartBumperBots(void);

static void StepBumperBots(void);

static void FinishBumperBots(void);

static void ChooseBumperAiMove(WORD id);

static void StepBumperAiFire(void);

static void StartRoboBowling(void);

static void StepRoboBowling(void);

static void FinishRoboBowling(void);

static void ChooseBowlingAiMove(WORD id);

static void ResetBowlingLane(WORD id);

static void ResolveBowlingThrow(WORD id);

static BOOL TryKnockdownPin(WORD id, WORD tx, WORD ty);

static void ScatterFloodBlocks(void);

static void StartFloodHouse(void);

static void StepFloodHouse(void);

static void FinishFloodHouse(void);

static void ChooseFloodAiMove(WORD id);

static void FloodHandleRobotArrival(WORD id);

static void TryFloodRaidRobot(WORD blockedId, WORD attackerId);

static BOOL TryFloodBuild(WORD id);

static WORD FloodHomeWallCount(WORD id);

static WORD FloodBlocksOwned(WORD id);

static LONG FloodRank(WORD id);

static void ResolveFloodHouse(void);

static void RestartCurrentMiniGame(void);

static BOOL IsArenaPlaying(void);

static void DrawMiniGameIntroScreen(void);

static void DrawMiniGameEndScreen(void);

static void StartBonusRound(void);

static void FinishBonusRound(void);

static BOOL BuildBonusBossCache(void);

static void FreeBonusBossCache(void);

static void StartBonusBossExplosion(void);

static void ClearMovementKeys(void);

static BOOL LoadMenuMusicStream(void);

static void ResetAllJoystickConfirmHolds(void);

static void FreeMenuMusicSample(void);

static void StartMenuMusic(void);

static void StopMenuMusic(void);

static UWORD FillMenuMusicChunk(WORD bufferIndex);

static void ServiceMenuMusicStream(void);

static void ServiceMenuMusicForState(void);

static UWORD AudioDmaBit(WORD channel);

static void PlayFullLoopedSample(struct OneShotSample *sample, WORD channel);

static BOOL LoadRoundStartSamples(void);

static void FreeRoundStartSamples(void);

static void PlayGetReadySample(void);

static void PlayCountdownSample(void);

static void PlayGoSample(void);

static void AudioPrepareChannel(WORD channel, UBYTE owner);

static void AudioReleaseChannel(WORD channel, UBYTE owner);

static void AudioSafeWait(void);

static void AudioDmaLatchWait(void);

static void StartRoundCountdownAudio(void);

static void StopGetReadySample(void);


#if USE_DIRTY_RECTS
static void ClearDirtyRects(void);
static void AddDirtyRect(WORD x, WORD y, WORD w, WORD h);
static void AddDirtyTile(WORD tx, WORD ty);
static void AddDirtyRobot(WORD id);
static void GetRobotDirtyBounds(LONG px, LONG py, WORD id, WORD *x, WORD *y, WORD *w, WORD *h);
static void AddDirtyRobotAt(LONG px, LONG py, WORD id);
static BOOL RobotNeedsCurrentDirtyRect(WORD id);
static void StoreDirtyRobotVisualState(WORD id);
static void OptimiseDirtyRects(void);
static void AddDirtyBolt(struct Bolt *bolt);
static void AddDirtyBoltAt(LONG px, LONG py);
static void AddDirtyBossExplosionAt(WORD ticks);
static void AddDirtyEmpRobotVisualAt(WORD sx, WORD sy);
static void AddDirtyEmpRobotVisual(WORD id);
static void MarkDirtyEmpRobotVisuals(void);
static BOOL RectIntersects(WORD ax, WORD ay, WORD aw, WORD ah, WORD bx, WORD by, WORD bw, WORD bh);
static BOOL DirtyGameplayRectsReady(void);
static BOOL DirtyGameplayNoChanges(void);
static void RestoreDirtyRectFromRoom(struct DirtyRect *rect);
static void DrawRobotsIntersectingRect(struct DirtyRect *rect);
static void DrawBoltsIntersectingRect(struct DirtyRect *rect);
static void DrawHudIfDirty(struct DirtyRect *rect);
static void DrawGameplayDirtyRects(void);
static void ForceGameplayFullPresent(void);
static void BeginGameplayDirtyRects(void);
static void FinishGameplayDirtyRects(void);
#else
#define AddDirtyTile(tx, ty) ((void)0)
#define ForceGameplayFullPresent() ((void)0)
#define BeginGameplayDirtyRects() ((void)0)
#define FinishGameplayDirtyRects() ((void)0)
#endif

static void StopCountdownSample(void);

static void StopGoSample(void);

static void StopRoundStartSamples(void);

static void StepRoundStartSamples(void);

static BOOL InitRoundStartOverlayCache(void);

static void FreeRoundStartOverlayCache(void);

static BOOL LoadGameplaySamples(void);

static void FreeGameplaySamples(void);

static void StartMainGameMusic(void);

static void StopGameplaySamples(void);

static void ServiceHooverMoveSample(void);

static void PlayBoltFireSample(void);
static void PlayGoalSample(void);

static UWORD AudioDmaBit(WORD channel);

static UWORD ReadBE16(const UBYTE *p);

static ULONG ReadBE32(const UBYTE *p);

static LONG GetFileSize(BPTR fh);

static BOOL RoundStartLocked(void);

static void CloseGameScreen(void);

static void UpdateEmpPaletteCycle(void);

static void StopEmpPaletteCycle(void);

static void UpdateNightMode(void);

static void StopNightMode(void);

static BOOL BuildSpeedTrailCache(void);

static void FreeSpeedTrailCache(void);

static BOOL BuildPuckCache(void);

static void FreePuckCache(void);

static void DrawPuck(void);

static void DrawAirHockeyPuck(void);


static const char *roomLayouts[5][MAP_H] = {
    {
        "####################",
        "#D...............D.#",
        "#..TTT.......TTT...#",
        "#..T...........T...#",
        "#..T...##..##..T...#",
        "#......#....#......#",
        "#......#....#......#",
        "#......#....#......#",
        "#..T...##..##..T...#",
        "#..T...........T...#",
        "#..TTT.......TTT...#",
        "#D...............D.#",
        "#..................#",
        "####################"
    },
    {
        "####################",
        "#D....TT......TT..D#",
        "#.....TT......TT...#",
        "#..####........###.#",
        "#..#..#..TTTT..#...#",
        "#..................#",
        "#..####........###.#",
        "#..................#",
        "#..TT..##..##..TT..#",
        "#......#....#......#",
        "#......#....#......#",
        "#D.....######.....D#",
        "#..................#",
        "####################"
    },
    {
        "####################",
        "#D....######......D#",
        "#.......TT.........#",
        "#....T#....#T......#",
        "#..####....####....#",
        "#..#...............#",
        "#..#..######..#....#",
        "#..#..........#....#",
        "#..#..######..#....#",
        "#..................#",
        "#..####....####....#",
        "#D...............D.#",
        "#.....######.......#",
        "####################"
    },
    {
        "####################",
        "#D....TT..##.....D.#",
        "#.....TT..##.......#",
        "#..#.####....TTT...#",
        "#..#...............#",
        "#..#....#....T.....#",
        "#..#....######.....#",
        "#..................#",
        "#..#.####....#.##..#",
        "#.....TT........#..#",
        "#.....TT.....#..#..#",
        "#D...........####.D#",
        "#..................#",
        "####################"
    },
    {
        "####################",
        "#D....TT......TT..D#",
        "#.....TT......TT...#",
        "#..................#",
        "#..#.####....####.##",
        "#..#.........#....##",
        "#..#....#....#....##",
        "#..#....#.........##",
        "#..#.####....####.##",
        "#..................#",
        "#...TT..........TT.#",
        "#D..TT............D#",
        "#..................#",
        "####################"
    }
};


static const char *raceLayout[MAP_H] = {
    "####################",
    "#..................#",
    "#..................#",
    "#..................#",
    "#....##########....#",
    "#....##########....#",
    "#....##########....#",
    "#....##########....#",
    "#....##########....#",
    "#..................#",
    "#..................#",
    "#..................#",
    "#..................#",
    "####################"
};


/* Amiga joystick direction bits are encoded in each JOYxDAT word:
 *   up    = bit 9 xor bit 8
 *   down  = bit 1 xor bit 0
 *   left  = bit 9
 *   right = bit 1
 * This avoids treating LEFT as RIGHT in the carousel, which happened when
 * RIGHT was decoded as bit 1 xor bit 9.
 */

#define JOY_UP(dat)    (((((dat) & 0x0200) >> 1) ^ ((dat) & 0x0100)) != 0)

#define JOY_DOWN(dat)  (((((dat) & 0x0002) >> 1) ^ ((dat) & 0x0001)) != 0)

#define JOY_LEFT(dat)  (((dat) & 0x0200) != 0)

#define JOY_RIGHT(dat) (((dat) & 0x0002) != 0)

/* ---------------------------------------------------------------------
 * Forward declarations for functions that were defined-before-use in
 * the original single-file build (no separate prototype existed). Adding
 * them here means every module file can call any function regardless of
 * which order robovac.c #includes the module files in.
 * ---------------------------------------------------------------------*/
static void InitTitleEffectQuality(void);
static void AdvanceTitleCarouselSpin(void);
static void ResetGameplaySpeedFrameCounter(void);
static BOOL ShouldAdvanceGameplayFrame(void);
static void CycleGameSpeed(void);
static void MarkBossHpTextDirty(void);
static void MarkHudStatusTextDirty(void);
static WORD RobotStartX(WORD id);
static WORD RobotStartY(WORD id);
static WORD RobotDockX(WORD id);
static WORD RobotDockY(WORD id);
static const char *RobotName(WORD id);
static const char *RobotTag(WORD id);
static const char *RobotControlLabel(WORD id);
static WORD ActiveRobotCountForDirt(void);
static WORD RoundDirtTarget(WORD round);
static UWORD RandRange(UWORD n);
static void SeedRandom(void);
static WORD AbsW(WORD v);
static UBYTE DirtyRobotTileUnder(WORD id);
static BOOL DirtyRectIntersectsRobotBounds(struct DirtyRect *rect, WORD id);
static BOOL DirtyOverlayIntersectsRobot(WORD id);
static ULONG DirtyRectArea(struct DirtyRect *rect);
static ULONG DirtyRectsTotalArea(void);
static BOOL DirtyRectsShouldMerge(struct DirtyRect *a, struct DirtyRect *b, WORD pad);
static void MergeDirtyRectPair(WORD dst, WORD src);
static void CoalesceDirtyRects(WORD pad);
static BOOL BuildDirtyHorizontalStrips(void);
static BOOL RobotDirtyBoundsUseQuad(WORD id);
static void AddDirtyPuckAt(LONG px, LONG py);
static void AddDirtyAirHockeyPuckAt(LONG px, LONG py);
static void AddDirtyDirtStormAt(LONG px, WORD tileY);
static void MarkDirtyHudIfChanged(void);
static void MarkDirtyRoundGoOverlay(void);
static void MarkDirtyEmpOverlay(void);
static void MarkDirtyBossArea(void);
static void PutText(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen);
static void RebuildDirtList(void);
static void CountDirt(void);
static void ClearDirtList(void);
static void EnsureDirtListValid(void);
static void AddDirtListTile(WORD x, WORD y);
static void RemoveDirtListTile(WORD x, WORD y);
static BOOL IsBlocked(WORD tx, WORD ty);
static BOOL RobotCanPassTile(WORD id, WORD tx, WORD ty);
static BOOL RobotAtTile(WORD tx, WORD ty, WORD ignoreId);
static WORD RobotIdAtTile(WORD tx, WORD ty, WORD ignoreId);
static BOOL RobotIsStranded(WORD id);
static BOOL TryPushStrandedRobot(WORD blockedId, WORD dx, WORD dy);
static BOOL MoveRobotToNearestFreeTile(WORD id);
static void DrawTileIntoCache(UBYTE tileType);
static UBYTE SampleAudioOwner(struct OneShotSample *sample);
static void StopOneShotSample(struct OneShotSample *sample, WORD channel);
static void PlayOneShotSample(struct OneShotSample *sample, WORD channel);
static void StepOneShotSample(struct OneShotSample *sample, WORD channel);
static void PlayLoopedSample(struct OneShotSample *sample, WORD channel);
static BOOL AnyHooverMoving(void);
static void FreeOneShotSample(struct OneShotSample *sample, WORD channel);
static BOOL LoadOneShotSample(const char *path, struct OneShotSample *sample, const char *description);
static void ServiceTitleMusicForState(void);
static BOOL LoadTileSheetIntoCache(void);
static void BuildWallRotationsInCache(void);
static void BuildFloorRotationsInCache(void);
static BOOL InitTileCache(void);
static void BlitTileTo(struct RastPort *rp, UBYTE tileType, WORD tx, WORD ty);
static BOOL IsWallTileAt(WORD tx, WORD ty);
static UBYTE GetWallRotation(WORD tx, WORD ty);
static void BlitWallRotatedTo(struct RastPort *rp, WORD tx, WORD ty);
static void DrawFloodBlockTile(struct RastPort *rp, WORD tx, WORD ty);
static void UpdateRoomTile(WORD tx, WORD ty);
static void BuildRoomBuffer(void);
static void CopyRobotPixel(struct RastPort *srcRP, struct RastPort *dstRP, struct RastPort *maskRP,
                           WORD srcX, WORD srcY, WORD dstX, WORD dstY);
static void BlitRobotFrameRotated(struct RastPort *srcRP, struct RastPort *dstRP, struct RastPort *maskRP,
                                  WORD srcBaseX, WORD dstBaseX, WORD rotation);
static void BlitRobotVariant(Object *dto, struct RastPort *dstRP, struct RastPort *maskRP,
                             WORD variantIndex, WORD baseRotation);
static BOOL ReadExact(BPTR file, void *buffer, LONG size);
static BOOL SkipBytes(BPTR file, ULONG size);
static ULONG ReadBigEndian32(const UBYTE *b);
static BOOL LoadPaletteCMapInto(const char *path, UWORD *destPalette, WORD firstPen, WORD maxPens);
static BOOL LoadPaletteCMap(const char *path, WORD firstPen, WORD maxPens);
static void DisableTitleCopperGradient(BOOL reloadPalette);
static void FreeTitleCopperGradient(void);
static WORD EmpPalettePhase(void);
static UBYTE EmpWarningPen(void);
static void LoadGamePalette(void);
static void LoadIntroPaletteLevel(WORD level);
static void CaptureIntroPalette(ULONG *cRegs, ULONG numColors);
static WORD IntroEffectSin(WORD phase);
static void DrawIntroTitleBobEffect(WORD dstX, WORD dstY, WORD effectTick);
static BOOL LoadIntroTitleImage(void);
static void FreeIntroTitleImage(void);
static BOOL LoadRobotSheetIntoCache(void);
static void FreeBoltCache(void);
static void CacheBoltPixel(struct RastPort *maskRP, WORD srcBaseX, WORD srcX, WORD srcY,
                           WORD dstBaseX, WORD dstX, WORD dstY);
static void BuildBoltCacheFrame(struct RastPort *maskRP, WORD frame, WORD k);
static BOOL BuildBoltCache(void);
static void FreeRobotScaledCache(void);
static BOOL BuildRobotScaledCache(void);
static void FreeTitleCarouselCache(void);
static void FreeTitleStaticCache(void);
static BOOL AllocTitleStaticCache(void);
static BOOL AllocTitleCarouselCache(WORD frameCount);
static WORD RoundedDiv(WORD value, WORD divisor);
static void BuildTitleCarouselRotationFrame(struct RastPort *maskRP, WORD variant, WORD frame);
static BOOL BuildTitleCarouselRotationCache(void);
static BOOL InitRobotBobs(void);
static void PlotRobotPixel(WORD x, WORD y, UBYTE pen);
static void DrawRobotBobScaled2Cpu(WORD srcX, WORD sx, WORD sy);
static void DrawRobotBobScaled2(WORD srcX, WORD sx, WORD sy);
static void DrawRobotBobRotated(WORD srcX, WORD sx, WORD sy, WORD angleStep);
static void BuildSpeedTrailBar(struct RastPort *maskRP, WORD x, WORD y,
                               WORD w, WORD h, UBYTE pen);
static void DrawSpeedMotionBlur(WORD id, WORD sx, WORD sy);
static void DrawBumperFallingRobot(WORD id);
static void DrawRobotBob(WORD id);
static WORD BoltFrameForDirection(struct Bolt *bolt);
static void DrawBoltSprite(struct Bolt *bolt);
static void DrawPlayerBolt(WORD playerId);
static void DrawBossBolts(void);
static WORD EmpRobotVisualState(WORD id);
static void SetRobotTile(WORD id, WORD tx, WORD ty);
static void InitRobots(void);
static WORD CleanTileForRobot(WORD id, WORD tx, WORD ty);
static void CleanQuadArea(WORD id);
static WORD SpriteDirectionIndex(UBYTE spriteIndex);
static void SetRobotMoveSprite(WORD id, UBYTE newSpriteIndex);
static BOOL ApplyBoltStun(WORD id, WORD damage);
static BOOL IsRobotDock(WORD id, WORD tx, WORD ty);
static BOOL ValidDirtTile(WORD tx, WORD ty);
static void SpawnRoundDirt(WORD count);
static void DirtStormBeginPass(void);
static void ResetLevel(void);
static BOOL ShouldStartMiniGameAfterRound(WORD completedRound);
static WORD ChooseNextMiniGame(void);
static void ResetPuckPosition(void);
static BOOL RaceCheckpointReached(WORD id, WORD checkpoint);
static LONG RaceProgressScore(WORD id);
static LONG BumperRank(WORD id);
static BOOL TryRaceBumpRobot(WORD blockedId, WORD dx, WORD dy);
static BOOL BumperTileInArena(WORD tx, WORD ty);
static void BumperEliminateRobot(WORD id, WORD attackerId);
static BOOL BumperPushRobot(WORD blockedId, WORD dx, WORD dy, WORD tiles, WORD attackerId);
static BOOL TryPushTable(WORD id, WORD tx, WORD ty, WORD dx, WORD dy);
static BOOL StartRobotMove(WORD id, WORD dx, WORD dy);
static void FinishRobotTileMove(WORD id);
static void StepRobotMovement(WORD id);
static void ShuffleAiDirs(WORD dirs[4]);
static WORD AiRecentVisitPenalty(WORD id, WORD tileX, WORD tileY);
static void OrderAiPathDirs(WORD id, WORD curX, WORD curY, WORD targetX, WORD targetY, WORD dirs[4]);
static void OrderAiExploreDirs(WORD id, WORD curX, WORD curY, WORD dirs[4]);
static BOOL AiFindNearestReachableDirt(WORD id, WORD *outX, WORD *outY, WORD *outDx, WORD *outDy);
static BOOL AiFindPathStep(WORD id, WORD targetX, WORD targetY, WORD *outDx, WORD *outDy, WORD *outDist);
static void ChooseHooverModeMove(WORD id);
static void ChooseAiMove(WORD id);
static void ChoosePlayerMove(WORD id);
static BOOL AnyRobotCanMove(void);
static WORD MatchRoundCount(void);
static void FinalizeMatchEnd(void);
static void CheckEndState(void);
static void ResetBonusBoss(void);
static void BossStunRobot(WORD id, const char *label);
static BOOL RectsOverlap(WORD ax, WORD ay, WORD aw, WORD ah, WORD bx, WORD by, WORD bw, WORD bh);
static BOOL RectOverlapCoversMost(WORD ax, WORD ay, WORD aw, WORD ah, WORD bx, WORD by, WORD bw, WORD bh);
static void BossPushBackRobot(WORD id, WORD bossCenterX, WORD bossCenterY);
static void FireBossBolt(void);
static void ChooseBossPattern(void);
static void StepBonusBoss(void);
static void StepIntro(void);
static void LimitPuckVelocity(void);
static void ScorePuckGoal(WORD team);
static void StepPuckPhysics(void);
static void ResetAirHockeyPuckPosition(void);
static void LimitAirHockeyVelocity(void);
static void ScoreAirHockeyGoal(WORD team);
static void StepAirHockeyPhysics(void);
static void StepGame(void);
static const UBYTE *MiniGlyph(char ch);
static void MiniCharScaled(struct RastPort *rp, WORD x, WORD y, char ch, UBYTE pen, WORD scale);
static void MiniTextScaled(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen, WORD scale);
static void MiniTextCentered(struct RastPort *rp, WORD y, const char *s, UBYTE pen, WORD scale);
static void MiniTextCenteredIn(struct RastPort *rp, WORD left, WORD width, WORD y, const char *s, UBYTE pen, WORD scale);
static void BuildRoundStartOverlay(WORD index, const char *label);
static void DrawCachedTitleRobotSpinFrame(WORD variant, WORD frame, WORD dstX, WORD dstY);
static void DrawCachedTitleRobotSpin(WORD variant, WORD phase, WORD dstX, WORD dstY);
static void DrawCachedTitleRobotSpinScaled(WORD variant, WORD phase, WORD dstX, WORD dstY, WORD scale);
static void DrawTitlePanelBase(WORD top, WORD bottom);
static void DrawTitleSelectorBox(WORD x, WORD y);
static void DrawTitleRobotStatic(WORD variant, WORD state, WORD dstX, WORD dstY);
static BOOL BuildTitleStaticCache(void);
static void DrawRobotHealthStrip(void);
static void DrawJoystickIcons(void);
static void DrawRobotLarge(WORD robotId, WORD x, WORD y, WORD scale, WORD phase);
static void DrawRobotIcon(WORD robotId, WORD x, WORD y, UBYTE state);
static void BuildRankOrder(WORD *order);
static void DrawLeaderboardScreen(BOOL finalBoard);
static void DrawBossExplosion(void);
static void DrawBonusBoss(void);
static void DrawRaceHud(void);
static void DrawPuckHud(void);
static void DrawAirHockeyHud(void);
static void DrawBumperHud(void);
static void DrawBowlingHud(void);
static void DrawFloodHouseHud(void);
static void DrawFloodWaterOverlay(void);
static void DrawIntroTitleImage(void);
static void DrawRoundStartOverlay(void);
static void DrawPauseMenu(void);
static void DrawAiSelectMenu(void);
static void DrawAiDifficultyMenu(void);
static BOOL RobotIntersectsRect(WORD id, struct DirtyRect *rect);
static void DrawPuckIntersectingRect(struct DirtyRect *rect);
static void DrawAirHockeyPuckIntersectingRect(struct DirtyRect *rect);
static void DrawDirtStormIntersectingRect(struct DirtyRect *rect);
static BOOL BoltIntersectsRect(struct Bolt *bolt, struct DirtyRect *rect);
static BOOL BonusBossIntersectsRect(struct DirtyRect *rect);
static BOOL BonusBossExplosionIntersectsRect(struct DirtyRect *rect);
static BOOL RoundGoOverlayIntersectsRect(struct DirtyRect *rect);
static BOOL EmpRobotVisualIntersectsRect(WORD id, struct DirtyRect *rect);
static void DrawEmpRobotVisualsIntersectingRect(struct DirtyRect *rect);
static void DrawGameplayDirtyOverlays(struct DirtyRect *rect);
static void DrawFrame(void);
static void PresentFullFrame(void);
static void PresentDirtyGameplayRects(void);
static void PresentDirtyGameplayFrame(void);
static void PresentFrame(void);
static void OpenPauseMenu(void);
static void ActivateAiDifficultyMenu(void);
static void ActivateAiSelectMenu(void);
static BOOL HandleAiDifficultyMenuRawKey(UWORD code, BOOL keyUpEvent);
static BOOL HandleAiSelectMenuRawKey(UWORD code, BOOL keyUpEvent);
static void ActivatePauseMenuSelection(void);
static BOOL HandlePauseMenuRawKey(UWORD code, BOOL keyUpEvent);
static void StartDemoMode(void);
static void StartWithRivals(WORD rivals);
static void TitleChooseVariant(WORD playerId, WORD delta);
static void TitleArmTwoPlayer(void);
static void TitleLockPlayer2(void);
static void TitlePlayer2Fire(void);
static void FireRobotBolt(WORD id, WORD dirX, WORD dirY, BOOL useBattery, BOOL playSound);
static void FirePlayerBolt(WORD id);
static void StepPlayerBolt(WORD ownerId);
static void FireAiBoltAtBoss(WORD id);
static BOOL ClearBoltLane(WORD sx, WORD sy, WORD tx, WORD ty);
static void HandleRawKey(UWORD rawCode);
static BOOL ReadJoystickFire(WORD id);
static void ResetJoystickConfirmHold(WORD id);
static BOOL ReadJoystickBlueButton(WORD id);
static BOOL UpdateJoystickConfirmHold(WORD id, BOOL fire);
static BOOL ActivateTitleJoystickConfirmAction(void);
static void HandleAiDifficultyJoystick(BOOL up, BOOL down, BOOL firePressed);
static void HandleAiSelectJoystick(BOOL up, BOOL down, BOOL firePressed);
static void HandleTitleJoystick(WORD id, BOOL left, BOOL right, BOOL up, BOOL down, BOOL firePressed, BOOL confirmPressed);
static void PollJoysticks(void);
static void PollWindowMessages(void);
static void DrainWindowMessages(void);
static BOOL OpenGameScreen(void);
int main(void);

#endif /* ROBOVAC_H */
