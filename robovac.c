/*
 * robovac_opt_ai_smooth.c - RoboVac Rescue optimized custom-screen engine
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
 * Build:
 *   m68k-amigaos-gcc -s -Os -o robovac robovac.c
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

#define MOVE_SPEED  (3 * FP_ONE)
#define DOUBLE_SPEED_MOVE_SPEED  (6 * FP_ONE)
#define EMERGENCY_MOVE_SPEED  (2 * FP_ONE)
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
#define BONUS_SCORE_THRESHOLD    50
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
#define BOLT_FRAME_COUNT 4
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
#define TILE_CACHE_COUNT (TILE_COUNT + WALL_ROTATION_COUNT)

#define GAME_INTRO      0
#define GAME_TITLE      1
#define GAME_PLAYING    2
#define GAME_ROUND_END  3
#define GAME_MATCH_END  4
#define GAME_BONUS_PLAYING 5
#define GAME_BONUS_END 6

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
static UWORD *audioSilenceWord = NULL;
static BOOL demoModeActive = FALSE;
static BOOL demoJoyPrimed = FALSE;
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

static void InitTitleEffectQuality(void)
{
    ULONG attnFlags = 0;

    detectedCpuName = "unknown";
    effectQuality = EFFECT_LOW;
    titleFxModeName = "low";
    titleCarouselDesiredFrameCount = TITLE_SPIN_STEPS_LOW;
    titleSpinAdvanceDivisor = 4;
    titleSpinAdvanceCounter = 0;

    if (SysBase) {
        attnFlags = SysBase->AttnFlags;
        if (attnFlags & AFF_68060) {
            detectedCpuName = "68060";
            effectQuality = EFFECT_HIGH;
            titleFxModeName = "high";
            titleCarouselDesiredFrameCount = TITLE_SPIN_STEPS;
            titleSpinAdvanceDivisor = 1;
        } else if (attnFlags & AFF_68040) {
            detectedCpuName = "68040";
            effectQuality = EFFECT_HIGH;
            titleFxModeName = "high";
            titleCarouselDesiredFrameCount = TITLE_SPIN_STEPS;
            titleSpinAdvanceDivisor = 1;
        } else if (attnFlags & AFF_68030) {
            detectedCpuName = "68030";
            effectQuality = EFFECT_NORMAL;
            titleFxModeName = "normal";
            titleCarouselDesiredFrameCount = TITLE_SPIN_STEPS;
            titleSpinAdvanceDivisor = 1;
        } else if (attnFlags & AFF_68020) {
            detectedCpuName = "68020";
        }
    }

    printf("Detected CPU: %s (AttnFlags=$%08lx); title FX: %s, %ld spin frames, advance every %ld frame(s)\n",
           detectedCpuName,
           (ULONG)attnFlags,
           titleFxModeName,
           (LONG)titleCarouselDesiredFrameCount,
           (LONG)titleSpinAdvanceDivisor);
}

static void AdvanceTitleCarouselSpin(void)
{
    titleSpinAdvanceCounter++;
    if (titleSpinAdvanceCounter >= titleSpinAdvanceDivisor) {
        titleSpinAdvanceCounter = 0;
        titleSpinPhase = (titleSpinPhase + 1) & (TITLE_SPIN_STEPS - 1);
    }
}

enum AudioOwner {
    AUDIO_OWNER_NONE = 0,
    AUDIO_OWNER_MENU_MUSIC,
    AUDIO_OWNER_ROUND_VOICE,
    AUDIO_OWNER_MAIN_MUSIC,
    AUDIO_OWNER_HOOVER_LOOP,
    AUDIO_OWNER_BOLT_FIRE
};

static UBYTE audioChannelOwner[4] = {
    AUDIO_OWNER_NONE, AUDIO_OWNER_NONE, AUDIO_OWNER_NONE, AUDIO_OWNER_NONE
};

struct DirtPos { WORD x, y; };
static struct DirtPos dirtList[MAP_W * MAP_H];
static WORD dirtListCount = 0;
static BOOL dirtListValid = FALSE;
static WORD dirtLeft = 0;
static WORD moves = 0;
static WORD maxBattery = 110;
static WORD batteryCostPerMove = 1;
static BOOL EnableTitleCopperGradient(void);
static WORD gameState = GAME_TITLE;

static void MarkTitlePanelDirty(void)
{
    if (gameState != GAME_TITLE) return;
    titlePanelDirty = TRUE;
}

static void RequestTitleFullPresents(void)
{
    titleFullPresentPending = TRUE;
    titleFullPresentFrames = TITLE_FORCED_FULL_PRESENT_FRAMES;
}

static void MarkTitleAllDirty(void)
{
    if (gameState != GAME_TITLE) return;
    titleStaticDirty = TRUE;
    titlePanelDirty = TRUE;
    RequestTitleFullPresents();
}

static void PrepareTitlePresentation(void)
{
    if (gameState != GAME_TITLE) return;
    titleFirstFullPresentDone = FALSE;
    EnableTitleCopperGradient();
    MarkTitleAllDirty();
}

static void ResetGameplaySpeedFrameCounter(void)
{
    gameSpeedFrameCounter = 0;
}

static BOOL ShouldAdvanceGameplayFrame(void)
{
    if (gameSpeed == GAME_SPEED_HIGH) return TRUE;

    gameSpeedFrameCounter++;
    if (gameSpeed == GAME_SPEED_LOW) {
        if (gameSpeedFrameCounter >= 2) gameSpeedFrameCounter = 0;
        return (gameSpeedFrameCounter != 0) ? TRUE : FALSE;
    }

    if (gameSpeedFrameCounter >= 3) {
        gameSpeedFrameCounter = 0;
        return FALSE;
    }
    return TRUE;
}

static void CycleGameSpeed(void)
{
    gameSpeed = (enum GameSpeed)(((WORD)gameSpeed + 1) % 3);
    ResetGameplaySpeedFrameCounter();
    MarkTitleAllDirty();
}

static WORD roundIndex = 0;
static WORD roundWins[MAX_ROBOTS] = {0,0,0,0};
static WORD totalScores[MAX_ROBOTS] = {0,0,0,0};
static WORD roundScores[MAX_ROBOTS] = {0,0,0,0};
static WORD roundWinner = -1;
static WORD finalWinner = -1;
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
static WORD bonusBossFacingState = 2; /* SPR_DOWN is declared later in the sprite enum. */
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
static WORD roundCountdownSoundNumber = 0;
static BOOL roundGoSoundPlayed = FALSE;
static WORD roundCountdownLastSoundNumber = 0;

static BOOL keyLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL keyRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL keyUp[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL keyDown[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyUp[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyDown[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyFirePrev[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyEnabled[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static WORD joyFireHoldTicks[MAX_HUMAN_PLAYERS] = {0, 0};
static BOOL joyFireHoldTriggered[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
static BOOL joyBluePrev[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
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

static void MarkBossHpTextDirty(void)
{
    dirtyBossHpText = TRUE;
}

static void MarkHudStatusTextDirty(void)
{
    dirtyHudStatusText = TRUE;
}

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

static WORD RobotStartX(WORD id)
{
    if (id < 0 || id >= MAX_ROBOTS) return 1;
    return (humanPlayers >= 2) ? robotStartXTwoPlayer[id] : robotStartXOnePlayer[id];
}

static WORD RobotStartY(WORD id)
{
    if (id < 0 || id >= MAX_ROBOTS) return 1;
    return (humanPlayers >= 2) ? robotStartYTwoPlayer[id] : robotStartYOnePlayer[id];
}

static WORD RobotDockX(WORD id)
{
    if (id < 0 || id >= MAX_ROBOTS) return 1;
    return (humanPlayers >= 2) ? robotDockXTwoPlayer[id] : robotDockXOnePlayer[id];
}

static WORD RobotDockY(WORD id)
{
    if (id < 0 || id >= MAX_ROBOTS) return 1;
    return (humanPlayers >= 2) ? robotDockYTwoPlayer[id] : robotDockYOnePlayer[id];
}

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

enum BoltSpriteFrame {
    BOLT_FRAME_UP = 0,
    BOLT_FRAME_DOWN = 1,
    BOLT_FRAME_LEFT = 2,
    BOLT_FRAME_RIGHT = 3
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

static const char *RobotName(WORD id)
{
    UBYTE variant;

    if (id < 0 || id >= robotCount) return "Vac";
    variant = robots[id].spriteVariant;
    if (variant >= ROBOT_VARIANTS) variant = 0;
    return robotVariantNames[variant];
}

static const char *RobotTag(WORD id)
{
    UBYTE variant;

    if (id < 0 || id >= robotCount) return "VAC";
    variant = robots[id].spriteVariant;
    if (variant >= ROBOT_VARIANTS) variant = 0;
    return robotVariantTags[variant];
}

static const char *RobotControlLabel(WORD id)
{
    if (id == 0 && humanPlayers >= 1) return joyEnabled[0] ? "J1" : "P1";
    if (id == 1 && humanPlayers >= 2) return joyEnabled[1] ? "J2" : "P2";
    return "AI";
}

static const WORD roundDirtTargets[5] = {14, 20, 26, 32, 38};

static WORD ActiveRobotCountForDirt(void)
{
    WORD count = humanPlayers + aiRivals;

    if (humanPlayers < 1) count = 1 + aiRivals;
    if (humanPlayers > MAX_HUMAN_PLAYERS) count = MAX_HUMAN_PLAYERS + aiRivals;
    if (count < 1) count = 1;
    if (count > MAX_ROBOTS) count = MAX_ROBOTS;
    return count;
}

static WORD RoundDirtTarget(WORD round)
{
    WORD base;
    WORD robotsInRound;
    WORD multiplier = 120;

    if (round < 0) round = 0;
    if (round > 4) round = 4;

    base = roundDirtTargets[round];
    robotsInRound = ActiveRobotCountForDirt();
    if (robotsInRound >= 4) multiplier = 140;
    else if (robotsInRound >= 3) multiplier = 130;

    return (WORD)(((LONG)base * multiplier + 99) / 100);
}
static void StepPlayerBolts(void);
static void StepBossBolts(void);
static void StepBonusAiFire(void);
static void StepMainGameAiFire(void);
static BOOL ActivateSpaceOrFireAction(void);
static BOOL EnableTitleCopperGradient(void);
static void DrawTitleCarousel(void);
static void DrawHud(void);
static WORD EmpRobotCountdownNumber(WORD id);
static BOOL RobotLowBatteryWarningActive(WORD id);
static WORD EmpRobotVisualState(WORD id);
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

static UWORD RandRange(UWORD n)
{
    rng = rng * 1103515245UL + 12345UL;
    if (!n) return 0;
    return (UWORD)((rng >> 16) % n);
}

static WORD AbsW(WORD v)
{
    return v < 0 ? -v : v;
}

#if USE_DIRTY_RECTS
static BOOL RectIntersects(WORD ax, WORD ay, WORD aw, WORD ah, WORD bx, WORD by, WORD bw, WORD bh)
{
    WORD ar;
    WORD ab;
    WORD br;
    WORD bb;

    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return FALSE;

    ar = ax + aw;
    ab = ay + ah;
    br = bx + bw;
    bb = by + bh;

    if (ar <= bx) return FALSE;
    if (br <= ax) return FALSE;
    if (ab <= by) return FALSE;
    if (bb <= ay) return FALSE;
    return TRUE;
}

static void ClearDirtyRects(void)
{
    dirtyRectCount = 0;
    dirtyRectsValid = TRUE;
    dirtyRectsBuiltForFrame = TRUE;
}

static void AddDirtyRect(WORD x, WORD y, WORD w, WORD h)
{
    if (!dirtyRectsValid || w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= SCREEN_W || y >= SCREEN_H || w <= 0 || h <= 0) return;
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;

    if (dirtyRectCount >= MAX_DIRTY_RECTS) {
        dirtyRectsValid = FALSE;
        dirtyForceFullFrame = TRUE;
        return;
    }

    dirtyRects[dirtyRectCount].x = x;
    dirtyRects[dirtyRectCount].y = y;
    dirtyRects[dirtyRectCount].w = w;
    dirtyRects[dirtyRectCount].h = h;
    dirtyRectCount++;
}

static void AddDirtyTile(WORD tx, WORD ty)
{
    AddDirtyRect(MAP_X + tx * TILE_SIZE, MAP_Y + ty * TILE_SIZE, TILE_SIZE, TILE_SIZE);
}

static UBYTE DirtyRobotTileUnder(WORD id)
{
    WORD tx;
    WORD ty;

    if (id < 0 || id >= robotCount) return TILE_FLOOR;
    tx = robots[id].tileX;
    ty = robots[id].tileY;
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return TILE_FLOOR;
    return map[ty][tx];
}

static BOOL DirtyRectIntersectsRobotBounds(struct DirtyRect *rect, WORD id)
{
    WORD x;
    WORD y;
    WORD w;
    WORD h;

    if (!rect || id < 0 || id >= robotCount) return FALSE;
    GetRobotDirtyBounds(robots[id].px, robots[id].py, id, &x, &y, &w, &h);
    return RectIntersects(rect->x, rect->y, rect->w, rect->h, x, y, w, h);
}

static BOOL DirtyOverlayIntersectsRobot(WORD id)
{
    struct DirtyRect rect;

    if (id < 0 || id >= robotCount) return FALSE;

    if (roundGoTicks > 0 || dirtyPrevRoundGoTicks > 0) {
        rect.x = ROUND_GO_TEXT_LEFT;
        rect.y = ROUND_GO_TEXT_TOP;
        rect.w = ROUND_GO_TEXT_W;
        rect.h = ROUND_GO_TEXT_H;
        if (DirtyRectIntersectsRobotBounds(&rect, id)) return TRUE;
    }

    if (dirtyPrevEmpVisible[id]) {
        GetEmpRobotVisualRectFromScreen(dirtyPrevEmpScreenX[id], dirtyPrevEmpScreenY[id], &rect);
        if (DirtyRectIntersectsRobotBounds(&rect, id)) return TRUE;
    }
    if (GetEmpRobotVisualRect(id, &rect)) {
        if (DirtyRectIntersectsRobotBounds(&rect, id)) return TRUE;
    }

    return FALSE;
}

static BOOL RobotNeedsCurrentDirtyRect(WORD id)
{
    UBYTE quadActive;

    if (id < 0 || id >= robotCount) return FALSE;
    if (!dirtyPrevRobotValid[id]) return TRUE;
    if (robots[id].px != dirtyPrevRobotPx[id] || robots[id].py != dirtyPrevRobotPy[id]) return TRUE;

    quadActive = (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) ? 1 : 0;
    if (robots[id].spriteIndex != dirtyPrevRobotSpriteIndex[id]) return TRUE;
    if (robots[id].prevSpriteIndex != dirtyPrevRobotPrevSpriteIndex[id]) return TRUE;
    if (robots[id].powerType != dirtyPrevRobotPowerType[id]) return TRUE;
    if (robots[id].powerMovesLeft != dirtyPrevRobotPowerMovesLeft[id]) return TRUE;
    if (robots[id].stunTicks != dirtyPrevRobotStunTicks[id]) return TRUE;
    if (robots[id].battery != dirtyPrevRobotBattery[id]) return TRUE;
    if (robots[id].turnTicks != dirtyPrevRobotTurnTicks[id]) return TRUE;
    if (robots[id].turnDirection != dirtyPrevRobotTurnDirection[id]) return TRUE;
    if (quadActive != dirtyPrevRobotQuadActive[id]) return TRUE;
    if (DirtyRobotTileUnder(id) != dirtyPrevRobotTileUnder[id]) return TRUE;
    if (DirtyOverlayIntersectsRobot(id)) return TRUE;

    return FALSE;
}

static void StoreDirtyRobotVisualState(WORD id)
{
    if (id < 0 || id >= robotCount) return;

    dirtyPrevRobotPx[id] = robots[id].px;
    dirtyPrevRobotPy[id] = robots[id].py;
    dirtyPrevRobotValid[id] = TRUE;
    dirtyPrevRobotSpriteIndex[id] = robots[id].spriteIndex;
    dirtyPrevRobotPrevSpriteIndex[id] = robots[id].prevSpriteIndex;
    dirtyPrevRobotPowerType[id] = robots[id].powerType;
    dirtyPrevRobotPowerMovesLeft[id] = robots[id].powerMovesLeft;
    dirtyPrevRobotStunTicks[id] = robots[id].stunTicks;
    dirtyPrevRobotBattery[id] = robots[id].battery;
    dirtyPrevRobotTurnTicks[id] = robots[id].turnTicks;
    dirtyPrevRobotTurnDirection[id] = robots[id].turnDirection;
    dirtyPrevRobotQuadActive[id] = (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) ? 1 : 0;
    dirtyPrevRobotTileUnder[id] = DirtyRobotTileUnder(id);
}

static ULONG DirtyRectArea(struct DirtyRect *rect)
{
    if (!rect || rect->w <= 0 || rect->h <= 0) return 0;
    return (ULONG)rect->w * (ULONG)rect->h;
}

static ULONG DirtyRectsTotalArea(void)
{
    WORD i;
    ULONG area = 0;

    for (i = 0; i < dirtyRectCount; i++) area += DirtyRectArea(&dirtyRects[i]);
    return area;
}

static BOOL DirtyRectsShouldMerge(struct DirtyRect *a, struct DirtyRect *b, WORD pad)
{
    WORD left;
    WORD top;
    WORD right;
    WORD bottom;
    ULONG mergedArea;
    ULONG oldArea;

    if (!a || !b) return FALSE;

    if (RectIntersects(a->x - pad, a->y - pad, a->w + pad * 2, a->h + pad * 2,
                       b->x, b->y, b->w, b->h)) {
        left = (a->x < b->x) ? a->x : b->x;
        top = (a->y < b->y) ? a->y : b->y;
        right = (a->x + a->w > b->x + b->w) ? a->x + a->w : b->x + b->w;
        bottom = (a->y + a->h > b->y + b->h) ? a->y + a->h : b->y + b->h;
        mergedArea = (ULONG)(right - left) * (ULONG)(bottom - top);
        oldArea = DirtyRectArea(a) + DirtyRectArea(b);
        if (mergedArea <= oldArea + DIRTY_RECT_CLOSE_MERGE_MAX_AREA) return TRUE;
    }

    return FALSE;
}

static void MergeDirtyRectPair(WORD dst, WORD src)
{
    WORD left;
    WORD top;
    WORD right;
    WORD bottom;

    if (dst < 0 || src < 0 || dst >= dirtyRectCount || src >= dirtyRectCount || dst == src) return;

    left = (dirtyRects[dst].x < dirtyRects[src].x) ? dirtyRects[dst].x : dirtyRects[src].x;
    top = (dirtyRects[dst].y < dirtyRects[src].y) ? dirtyRects[dst].y : dirtyRects[src].y;
    right = (dirtyRects[dst].x + dirtyRects[dst].w > dirtyRects[src].x + dirtyRects[src].w) ?
            dirtyRects[dst].x + dirtyRects[dst].w : dirtyRects[src].x + dirtyRects[src].w;
    bottom = (dirtyRects[dst].y + dirtyRects[dst].h > dirtyRects[src].y + dirtyRects[src].h) ?
             dirtyRects[dst].y + dirtyRects[dst].h : dirtyRects[src].y + dirtyRects[src].h;

    dirtyRects[dst].x = left;
    dirtyRects[dst].y = top;
    dirtyRects[dst].w = right - left;
    dirtyRects[dst].h = bottom - top;

    dirtyRectCount--;
    if (src != dirtyRectCount) dirtyRects[src] = dirtyRects[dirtyRectCount];
}

static void CoalesceDirtyRects(WORD pad)
{
    BOOL merged;

    do {
        WORD i;
        merged = FALSE;
        for (i = 0; i < dirtyRectCount && !merged; i++) {
            WORD j;
            for (j = i + 1; j < dirtyRectCount; j++) {
                if (DirtyRectsShouldMerge(&dirtyRects[i], &dirtyRects[j], pad)) {
                    MergeDirtyRectPair(i, j);
                    merged = TRUE;
                    break;
                }
            }
        }
    } while (merged);
}

static BOOL BuildDirtyHorizontalStrips(void)
{
    struct DirtyRect source[MAX_DIRTY_RECTS];
    struct DirtyRect strips[DIRTY_RECT_STRIP_MAX];
    WORD sourceCount;
    WORD stripCount = 0;
    WORD i;
    ULONG stripArea = 0;
    ULONG normalArea;

    if (effectQuality != EFFECT_LOW) return FALSE;
    if (dirtyRectCount <= DIRTY_RECT_STRIP_MAX + DIRTY_RECT_STRIP_MIN_SAVED_RECTS) return FALSE;

    sourceCount = dirtyRectCount;
    normalArea = DirtyRectsTotalArea();
    for (i = 0; i < sourceCount; i++) source[i] = dirtyRects[i];

    for (i = 0; i < sourceCount; i++) {
        WORD best = -1;
        WORD j;
        for (j = 0; j < stripCount; j++) {
            if (RectIntersects(0, strips[j].y - DIRTY_RECT_LOW_CLOSE_MERGE_PAD,
                               SCREEN_W, strips[j].h + DIRTY_RECT_LOW_CLOSE_MERGE_PAD * 2,
                               0, source[i].y, SCREEN_W, source[i].h)) {
                best = j;
                break;
            }
        }

        if (best < 0) {
            if (stripCount >= DIRTY_RECT_STRIP_MAX) return FALSE;
            strips[stripCount].x = 0;
            strips[stripCount].y = source[i].y;
            strips[stripCount].w = SCREEN_W;
            strips[stripCount].h = source[i].h;
            stripCount++;
        } else {
            WORD top = (strips[best].y < source[i].y) ? strips[best].y : source[i].y;
            WORD bottom = (strips[best].y + strips[best].h > source[i].y + source[i].h) ?
                          strips[best].y + strips[best].h : source[i].y + source[i].h;
            strips[best].y = top;
            strips[best].h = bottom - top;
        }
    }

    for (i = 0; i < stripCount; i++) {
        if (strips[i].h > DIRTY_RECT_STRIP_MAX_HEIGHT) return FALSE;
        stripArea += DirtyRectArea(&strips[i]);
    }

    if (stripCount >= sourceCount) return FALSE;
    if (stripArea >= normalArea && sourceCount <= DIRTY_RECT_LOW_MAX_DRAW_RECTS) return FALSE;
    if (stripArea > DIRTY_RECT_LOW_FALLBACK_AREA) return FALSE;

    dirtyRectCount = stripCount;
    for (i = 0; i < stripCount; i++) dirtyRects[i] = strips[i];
    dirtyStripFrameCount++;
    return TRUE;
}

static void OptimiseDirtyRects(void)
{
    WORD maxRects;
    ULONG maxArea;

    if (!dirtyRectsValid || dirtyForceFullFrame || dirtyRectCount <= 0) return;

    dirtyRectPreMergeCount = dirtyRectCount;
    CoalesceDirtyRects((effectQuality == EFFECT_LOW) ? DIRTY_RECT_LOW_CLOSE_MERGE_PAD : DIRTY_RECT_CLOSE_MERGE_PAD);
    dirtyRectPostMergeCount = dirtyRectCount;

    if (effectQuality == EFFECT_LOW) BuildDirtyHorizontalStrips();

    dirtyRectLastArea = DirtyRectsTotalArea();
    dirtyRectAreaTotal += dirtyRectLastArea;

    maxRects = (effectQuality == EFFECT_LOW) ? DIRTY_RECT_LOW_MAX_DRAW_RECTS : DIRTY_RECT_MAX_DRAW_RECTS;
    maxArea = (effectQuality == EFFECT_LOW) ? DIRTY_RECT_LOW_FALLBACK_AREA : DIRTY_RECT_FALLBACK_AREA;

    if (dirtyRectCount > maxRects || dirtyRectLastArea > maxArea) {
        ForceGameplayFullPresent();
    }
}

static BOOL RobotDirtyBoundsUseQuad(WORD id)
{
    if (id < 0 || id >= robotCount) return FALSE;

    if (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) return TRUE;

    if (dirtyPrevRobotValid[id] &&
        dirtyPrevRobotPowerType[id] == POWER_QUAD &&
        dirtyPrevRobotPowerMovesLeft[id] > 0) {
        return TRUE;
    }

    return FALSE;
}

static void GetRobotDirtyBounds(LONG px, LONG py, WORD id, WORD *x, WORD *y, WORD *w, WORD *h)
{
    WORD sx = MAP_X + FP_TO_INT(px);
    WORD sy = MAP_Y + FP_TO_INT(py);
    WORD left;
    WORD top;
    WORD right;
    WORD bottom;
    const WORD margin = 2;

    if (RobotDirtyBoundsUseQuad(id)) {
        left = sx - (ROBOT_W / 2);
        top = sy - (ROBOT_H / 2);
        right = left + ROBOT_SCALE2_W;
        bottom = top + ROBOT_SCALE2_H;

        if (sx < left) left = sx;
        if (sy + 21 < top) top = sy + 21;
        if (sx + 24 > right) right = sx + 24;
        if (sy + 25 > bottom) bottom = sy + 25;
    } else {
        left = sx;
        top = sy;
        right = sx + ROBOT_W;
        bottom = sy + ROBOT_H;

        if (sx + 4 < left) left = sx + 4;
        if (sy + 13 < top) top = sy + 13;
        if (sx + 13 > right) right = sx + 13;
        if (sy + 15 > bottom) bottom = sy + 15;
    }

    *x = left - margin;
    *y = top - margin;
    *w = (right - left) + (margin * 2);
    *h = (bottom - top) + (margin * 2);
}

static void AddDirtyRobotAt(LONG px, LONG py, WORD id)
{
    WORD x;
    WORD y;
    WORD w;
    WORD h;

    GetRobotDirtyBounds(px, py, id, &x, &y, &w, &h);
    AddDirtyRect(x, y, w, h);
}

static void AddDirtyRobot(WORD id)
{
    if (id < 0 || id >= robotCount) return;
    AddDirtyRobotAt(robots[id].px, robots[id].py, id);
}

static void AddDirtyBoltAt(LONG px, LONG py)
{
    AddDirtyRect(MAP_X + FP_TO_INT(px) - 1, MAP_Y + FP_TO_INT(py) - 1, ROBOT_W + 2, ROBOT_H + 2);
}

static void AddDirtyBolt(struct Bolt *bolt)
{
    if (!bolt || !bolt->active) return;
    AddDirtyBoltAt(bolt->px, bolt->py);
}

static void AddDirtyBossExplosionAt(WORD ticks)
{
    WORD spread;
    WORD centerX;
    WORD centerY;

    if (gameState != GAME_BONUS_PLAYING || ticks <= 0) return;

    spread = (BONUS_BOSS_EXPLOSION_TICKS - ticks) / 2;
    centerX = bonusBossExplosionX + ((ROBOT_W * BONUS_BOSS_SCALE) / 2) - (ROBOT_W / 2);
    centerY = bonusBossExplosionY + ((ROBOT_H * BONUS_BOSS_SCALE) / 2) - (ROBOT_H / 2);
    AddDirtyRect(centerX - spread - 2, centerY - spread - 2,
                 ROBOT_W + (spread * 2) + 4, ROBOT_H + (spread * 2) + 4);
}

static void AddDirtyEmpRobotVisualAt(WORD sx, WORD sy)
{
    struct DirtyRect rect;

    GetEmpRobotVisualRectFromScreen(sx, sy, &rect);
    if (rect.w > 0 && rect.h > 0) AddDirtyRect(rect.x, rect.y, rect.w, rect.h);
}

static void AddDirtyEmpRobotVisual(WORD id)
{
    struct DirtyRect rect;

    if (!GetEmpRobotVisualRect(id, &rect)) return;
    AddDirtyRect(rect.x, rect.y, rect.w, rect.h);
}

static void MarkDirtyEmpRobotVisuals(void)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        WORD state = EmpRobotVisualState(i);
        WORD sx = MAP_X + FP_TO_INT(robots[i].px);
        WORD sy = MAP_Y + FP_TO_INT(robots[i].py);

        if (dirtyPrevEmpVisible[i]) {
            AddDirtyEmpRobotVisualAt(dirtyPrevEmpScreenX[i], dirtyPrevEmpScreenY[i]);
        }
        if (state != 0) {
            if (dirtyPrevEmpCountdown[i] != state) {
                AddDirtyEmpRobotVisualAt(sx, sy);
            }
            AddDirtyEmpRobotVisual(i);
        }

        dirtyPrevEmpVisible[i] = state != 0;
        dirtyPrevEmpCountdown[i] = state;
        dirtyPrevEmpScreenX[i] = sx;
        dirtyPrevEmpScreenY[i] = sy;
    }
    for (i = robotCount; i < MAX_ROBOTS; i++) {
        if (dirtyPrevEmpVisible[i]) {
            AddDirtyEmpRobotVisualAt(dirtyPrevEmpScreenX[i], dirtyPrevEmpScreenY[i]);
        }
        dirtyPrevEmpVisible[i] = FALSE;
        dirtyPrevEmpCountdown[i] = 0;
    }
}

static void ForceGameplayFullPresent(void)
{
    dirtyForceFullFrame = TRUE;
    dirtyRectsValid = FALSE;
}

static void MarkDirtyHudIfChanged(void)
{
    WORD i;
    BOOL changed = FALSE;

    if (dirtyPrevDirtLeft != dirtLeft || dirtyPrevMoves != moves ||
        dirtyPrevLastPowerTicks != lastPowerTicks || dirtyPrevCountdownTicks != roundCountdownTicks ||
        dirtyPrevRobotCount != robotCount ||
        (gameState == GAME_BONUS_PLAYING && dirtyPrevBossHealth != bonusBossHealth)) {
        changed = TRUE;
    }

    for (i = 0; i < robotCount; i++) {
        if (dirtyPrevBattery[i] != robots[i].battery || dirtyPrevScore[i] != robots[i].score ||
            dirtyPrevStunTicks[i] != robots[i].stunTicks || dirtyPrevPowerMoves[i] != robots[i].powerMovesLeft ||
            dirtyPrevPowerType[i] != robots[i].powerType) {
            changed = TRUE;
            break;
        }
    }

    if (changed) {
        AddDirtyRect(0, 0, SCREEN_W, HUD_H);
        if (gameState == GAME_BONUS_PLAYING && dirtyPrevBossHealth != bonusBossHealth) {
            AddDirtyRect(80, 36, 160, 14);
        }
    }

    dirtyPrevDirtLeft = dirtLeft;
    dirtyPrevMoves = moves;
    dirtyPrevLastPowerTicks = lastPowerTicks;
    dirtyPrevEmpTicks = empCountdownTicks;
    dirtyPrevRoundGoTicks = roundGoTicks;
    dirtyPrevCountdownTicks = roundCountdownTicks;
    dirtyPrevRobotCount = robotCount;
    dirtyPrevBossHealth = bonusBossHealth;
    for (i = 0; i < robotCount; i++) {
        dirtyPrevBattery[i] = robots[i].battery;
        dirtyPrevScore[i] = robots[i].score;
        dirtyPrevStunTicks[i] = robots[i].stunTicks;
        dirtyPrevPowerMoves[i] = robots[i].powerMovesLeft;
        dirtyPrevPowerType[i] = robots[i].powerType;
    }
}

static void MarkDirtyRoundGoOverlay(void)
{
    if (roundGoTicks > 0 || dirtyPrevRoundGoTicks > 0) {
        AddDirtyRect(ROUND_GO_TEXT_LEFT, ROUND_GO_TEXT_TOP,
                     ROUND_GO_TEXT_W, ROUND_GO_TEXT_H);
    }
}

static void MarkDirtyEmpOverlay(void)
{
    MarkDirtyEmpRobotVisuals();
}

static void MarkDirtyBossArea(void)
{
    WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
    WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;

    if (gameState != GAME_BONUS_PLAYING) return;
    if (dirtyPrevBossHealth > 0) AddDirtyRect(dirtyPrevBossX - 8, dirtyPrevBossY - 8, bossW + 16, bossH + 16);
    if (bonusBossHealth > 0) AddDirtyRect(bonusBossX - 8, bonusBossY - 8, bossW + 16, bossH + 16);
    AddDirtyBossExplosionAt(dirtyPrevBossExplosionTicks);
    AddDirtyBossExplosionAt(bonusBossExplosionTicks);
    dirtyPrevBossX = bonusBossX;
    dirtyPrevBossY = bonusBossY;
    dirtyPrevBossExplosionTicks = bonusBossExplosionTicks;
}

static void BeginGameplayDirtyRects(void)
{
    WORD i;

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) {
        dirtyRectsBuiltForFrame = FALSE;
        return;
    }

    ClearDirtyRects();

    if (roundCountdownTicks > 0 || pauseMenuOpen) {
        ForceGameplayFullPresent();
    }

    MarkDirtyRoundGoOverlay();
    MarkDirtyEmpOverlay();

    for (i = 0; i < robotCount; i++) {
        if (dirtyPrevPlayerBoltActive[i]) AddDirtyBoltAt(dirtyPrevPlayerBoltPx[i], dirtyPrevPlayerBoltPy[i]);
    }
    for (i = 0; i < MAX_BOSS_BOLTS; i++) {
        if (dirtyPrevBossBoltActive[i]) AddDirtyBoltAt(dirtyPrevBossBoltPx[i], dirtyPrevBossBoltPy[i]);
    }
    MarkDirtyBossArea();
}

static void FinishGameplayDirtyRects(void)
{
    WORD i;

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) {
        ForceGameplayFullPresent();
        return;
    }

    for (i = 0; i < robotCount; i++) {
        if (RobotNeedsCurrentDirtyRect(i)) {
            if (dirtyPrevRobotValid[i]) AddDirtyRobotAt(dirtyPrevRobotPx[i], dirtyPrevRobotPy[i], i);
            AddDirtyRobot(i);
        }
        StoreDirtyRobotVisualState(i);
    }
    for (i = robotCount; i < MAX_ROBOTS; i++) dirtyPrevRobotValid[i] = FALSE;

    for (i = 0; i < robotCount; i++) {
        AddDirtyBolt(&playerBolts[i]);
        dirtyPrevPlayerBoltActive[i] = playerBolts[i].active;
        dirtyPrevPlayerBoltPx[i] = playerBolts[i].px;
        dirtyPrevPlayerBoltPy[i] = playerBolts[i].py;
    }
    for (i = robotCount; i < MAX_ROBOTS; i++) dirtyPrevPlayerBoltActive[i] = FALSE;

    for (i = 0; i < MAX_BOSS_BOLTS; i++) {
        AddDirtyBolt(&bossBolts[i]);
        dirtyPrevBossBoltActive[i] = bossBolts[i].active;
        dirtyPrevBossBoltPx[i] = bossBolts[i].px;
        dirtyPrevBossBoltPy[i] = bossBolts[i].py;
    }

    MarkDirtyBossArea();
    MarkDirtyRoundGoOverlay();
    MarkDirtyEmpOverlay();
    MarkDirtyHudIfChanged();

    if (roundCountdownTicks > 0 || pauseMenuOpen) ForceGameplayFullPresent();

    OptimiseDirtyRects();
}
#endif

static void PutText(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)s, strlen(s));
}

static void RebuildDirtList(void)
{
    WORD x;
    WORD y;

    dirtListCount = 0;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            if (map[y][x] == TILE_DIRT) {
                if (dirtListCount < MAP_W * MAP_H) {
                    dirtList[dirtListCount].x = x;
                    dirtList[dirtListCount].y = y;
                    dirtListCount++;
                }
            }
        }
    }

    dirtLeft = dirtListCount;
    MarkHudStatusTextDirty();
    dirtListValid = TRUE;
}

static void CountDirt(void)
{
    RebuildDirtList();
}

static void ClearDirtList(void)
{
    dirtListCount = 0;
    dirtLeft = 0;
    MarkHudStatusTextDirty();
    dirtListValid = TRUE;
}

static void EnsureDirtListValid(void)
{
    if (!dirtListValid || dirtListCount != dirtLeft) {
        RebuildDirtList();
    }
}

static void AddDirtListTile(WORD x, WORD y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || map[y][x] != TILE_DIRT) {
        RebuildDirtList();
        return;
    }

    if (!dirtListValid) {
        RebuildDirtList();
        return;
    }

    if (dirtListCount >= MAP_W * MAP_H) {
        RebuildDirtList();
        return;
    }

    dirtList[dirtListCount].x = x;
    dirtList[dirtListCount].y = y;
    dirtListCount++;
    dirtLeft = dirtListCount;
    MarkHudStatusTextDirty();
}

static void RemoveDirtListTile(WORD x, WORD y)
{
    WORD i;

    if (!dirtListValid) {
        RebuildDirtList();
        return;
    }

    for (i = 0; i < dirtListCount; i++) {
        if (dirtList[i].x == x && dirtList[i].y == y) {
            dirtListCount--;
            if (i != dirtListCount) {
                dirtList[i] = dirtList[dirtListCount];
            }
            dirtLeft = dirtListCount;
            MarkHudStatusTextDirty();
            return;
        }
    }

    RebuildDirtList();
}

static BOOL IsBlocked(WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return TRUE;
    return (map[ty][tx] == TILE_WALL || map[ty][tx] == TILE_TABLE);
}

static BOOL RobotCanPassTile(WORD id, WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return FALSE;
    if (id >= 0 && id < robotCount && robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) {
        return TRUE;
    }
    if (id >= 0 && id < robotCount && robots[id].powerType == POWER_WALL_SMASH && robots[id].powerMovesLeft > 0 &&
        map[ty][tx] == TILE_WALL && tx > 0 && ty > 0 && tx < MAP_W - 1 && ty < MAP_H - 1) {
        return TRUE;
    }
    return !IsBlocked(tx, ty);
}

static BOOL RobotAtTile(WORD tx, WORD ty, WORD ignoreId)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (i == ignoreId) continue;

        if (robots[i].tileX == tx && robots[i].tileY == ty) return TRUE;
        if (robots[i].targetX == tx && robots[i].targetY == ty) return TRUE;
    }

    return FALSE;
}

static WORD RobotIdAtTile(WORD tx, WORD ty, WORD ignoreId)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (i == ignoreId) continue;

        if (robots[i].tileX == tx && robots[i].tileY == ty) return i;
        if (robots[i].targetX == tx && robots[i].targetY == ty) return i;
    }

    return -1;
}

/* A robot with no battery for a full move and no emergency moves left
 * cannot get itself out of the way. */
static BOOL RobotIsStranded(WORD id)
{
    if (id < 0 || id >= robotCount) return FALSE;
    if (robots[id].moving) return FALSE;
    if (robots[id].battery >= batteryCostPerMove) return FALSE;
    if (robots[id].battery <= 0 && robots[id].emergencyMovesLeft > 0) return FALSE;
    return TRUE;
}

/* Bonus-fight helper: a stranded (out-of-charge) robot can't clear its own
 * path to a dock, so let a teammate shove it one tile further along the
 * direction it's already being bumped from, the same way the boss shoves a
 * grazed robot. Only succeeds if the tile being pushed into is itself free,
 * so this never displaces a robot into a wall or a third robot. */
static BOOL TryPushStrandedRobot(WORD blockedId, WORD dx, WORD dy)
{
    WORD pushX;
    WORD pushY;

    if (!RobotIsStranded(blockedId)) return FALSE;

    pushX = robots[blockedId].tileX + dx;
    pushY = robots[blockedId].tileY + dy;
    if (!RobotCanPassTile(blockedId, pushX, pushY)) return FALSE;
    if (RobotAtTile(pushX, pushY, blockedId)) return FALSE;

    robots[blockedId].tileX = pushX;
    robots[blockedId].tileY = pushY;
    robots[blockedId].px = TO_FP(pushX * TILE_SIZE);
    robots[blockedId].py = TO_FP(pushY * TILE_SIZE);
    robots[blockedId].targetPx = robots[blockedId].px;
    robots[blockedId].targetPy = robots[blockedId].py;
    robots[blockedId].moving = FALSE;

    /* A push bypasses the normal tile-arrival path (FinishRobotTileMove),
     * which is the only other place charging starts, so start it here too
     * if the shove happens to land the stranded robot on a dock. */
    if (map[pushY][pushX] == TILE_DOCK) {
        if (robots[blockedId].battery <= 0) {
            robots[blockedId].chargeTicks = DOCK_CHARGE_TICKS;
        } else {
            robots[blockedId].battery = maxBattery;
            robots[blockedId].emergencyMovesLeft = EMERGENCY_DOCK_MOVES;
            robots[blockedId].chargeTicks = 0;
        }
    }
    return TRUE;
}

static BOOL MoveRobotToNearestFreeTile(WORD id)
{
    WORD radius;
    WORD bestX = -1;
    WORD bestY = -1;

    if (id < 0 || id >= robotCount) return FALSE;
    if (!IsBlocked(robots[id].tileX, robots[id].tileY)) return FALSE;

    for (radius = 1; radius < MAP_W + MAP_H; radius++) {
        WORD dy;
        for (dy = -radius; dy <= radius; dy++) {
            WORD dx;
            for (dx = -radius; dx <= radius; dx++) {
                WORD tx;
                WORD ty;

                if (AbsW(dx) + AbsW(dy) != radius) continue;
                tx = robots[id].tileX + dx;
                ty = robots[id].tileY + dy;
                if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
                if (IsBlocked(tx, ty)) continue;
                if (RobotAtTile(tx, ty, id)) continue;
                bestX = tx;
                bestY = ty;
                break;
            }
            if (bestX >= 0) break;
        }
        if (bestX >= 0) break;
    }

    if (bestX < 0) return FALSE;

    robots[id].tileX = bestX;
    robots[id].tileY = bestY;
    robots[id].targetX = bestX;
    robots[id].targetY = bestY;
    robots[id].px = TO_FP(bestX * TILE_SIZE);
    robots[id].py = TO_FP(bestY * TILE_SIZE);
    robots[id].targetPx = robots[id].px;
    robots[id].targetPy = robots[id].py;
    robots[id].moving = FALSE;
    snprintf(lastPowerText, sizeof(lastPowerText), "%s PHASED FREE", RobotTag(id));
    lastPowerTicks = 60;
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Tile cache
 * ------------------------------------------------------------------------- */

static void DrawTileIntoCache(UBYTE tileType)
{
    WORD sx = tileType * TILE_SIZE;
    WORD sy = 0;
    WORD ex = sx + TILE_SIZE - 1;
    WORD ey = TILE_SIZE - 1;

    if (tileType == TILE_WALL) {
        SetAPen(&tileRP, 1);
        RectFill(&tileRP, sx, sy, ex, ey);
        SetAPen(&tileRP, 8);
        RectFill(&tileRP, sx, sy, ex, sy + 1);
        RectFill(&tileRP, sx, sy, sx + 1, ey);
        SetAPen(&tileRP, 0);
        RectFill(&tileRP, ex - 1, sy, ex, ey);
        RectFill(&tileRP, sx, ey - 1, ex, ey);
        return;
    }

    SetAPen(&tileRP, 9);
    RectFill(&tileRP, sx, sy, ex, ey);

    SetAPen(&tileRP, 8);
    WritePixel(&tileRP, sx, sy);
    WritePixel(&tileRP, ex, sy);
    WritePixel(&tileRP, sx, ey);
    WritePixel(&tileRP, ex, ey);

    if (tileType == TILE_TABLE) {
        SetAPen(&tileRP, 5);
        RectFill(&tileRP, sx + 2, sy + 2, ex - 2, ey - 2);
        SetAPen(&tileRP, 6);
        RectFill(&tileRP, sx + 4, sy + 4, ex - 4, ey - 4);
    } else if (tileType == TILE_DOCK) {
        SetAPen(&tileRP, 4);
        RectFill(&tileRP, sx + 2, sy + 2, ex - 2, ey - 2);
        SetAPen(&tileRP, 7);
        Move(&tileRP, sx + 4, sy + 8);
        Draw(&tileRP, sx + 11, sy + 8);
        Move(&tileRP, sx + 8, sy + 4);
        Draw(&tileRP, sx + 8, sy + 12);
    } else if (tileType == TILE_DIRT) {
        SetAPen(&tileRP, 2);
        RectFill(&tileRP, sx + 5, sy + 5, sx + 10, sy + 10);
        SetAPen(&tileRP, 3);
        WritePixel(&tileRP, sx + 4, sy + 7);
        WritePixel(&tileRP, sx + 11, sy + 9);
        WritePixel(&tileRP, sx + 8, sy + 4);
    }
}


static void AudioSafeWait(void)
{
    volatile WORD i;

    for (i = 0; i < 64; i++) {
        ;
    }
}

static void AudioDmaLatchWait(void)
{
    volatile WORD i;

    for (i = 0; i < 1024; i++) {
        ;
    }
}

static void AudioPrepareChannel(WORD channel, UBYTE owner)
{
    UWORD dmaBit;

    if (channel < 0 || channel > 3) return;

    dmaBit = AudioDmaBit(channel);
    custom.dmacon = dmaBit;
    custom.aud[channel].ac_vol = 0;
    AudioSafeWait();
    custom.aud[channel].ac_len = 1;
    if (audioSilenceWord) custom.aud[channel].ac_ptr = audioSilenceWord;
    audioChannelOwner[channel] = owner;
}

static void AudioReleaseChannel(WORD channel, UBYTE owner)
{
    UWORD dmaBit;

    if (channel < 0 || channel > 3) return;

    dmaBit = AudioDmaBit(channel);
    custom.dmacon = dmaBit;
    custom.aud[channel].ac_vol = 0;
    AudioSafeWait();
    custom.aud[channel].ac_len = 1;
    if (audioSilenceWord) custom.aud[channel].ac_ptr = audioSilenceWord;
    if (audioChannelOwner[channel] == owner || owner == AUDIO_OWNER_NONE) {
        audioChannelOwner[channel] = AUDIO_OWNER_NONE;
    }
}

static UBYTE SampleAudioOwner(struct OneShotSample *sample)
{
    if (sample == &menuMusicSample) return AUDIO_OWNER_MENU_MUSIC;
    if (sample == &getReadySample || sample == &countdownSample || sample == &goSample) return AUDIO_OWNER_ROUND_VOICE;
    if (sample == &mainMusicSample) return AUDIO_OWNER_MAIN_MUSIC;
    if (sample == &hooverMoveSample) return AUDIO_OWNER_HOOVER_LOOP;
    if (sample == &boltFireSample) return AUDIO_OWNER_BOLT_FIRE;
    return AUDIO_OWNER_NONE;
}

static void StopOneShotSample(struct OneShotSample *sample, WORD channel)
{
    if (!sample->loaded && !sample->playing) return;

    AudioReleaseChannel(channel, SampleAudioOwner(sample));
    sample->playing = FALSE;
    sample->ticksRemaining = 0;
}

static void PlayOneShotSample(struct OneShotSample *sample, WORD channel)
{
    UWORD dmaBit;
    UBYTE owner;

    if (!sample->loaded || !sample->data || sample->lengthWords == 0) return;

    owner = SampleAudioOwner(sample);
    AudioPrepareChannel(channel, owner);
    dmaBit = AudioDmaBit(channel);
    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;
    custom.aud[channel].ac_per = sample->period;
    custom.aud[channel].ac_vol = sample->volume;
    custom.dmacon = DMAF_SETCLR | dmaBit;
    AudioDmaLatchWait();

    if (audioSilenceWord) {
        custom.aud[channel].ac_ptr = audioSilenceWord;
        custom.aud[channel].ac_len = 1;
    }

    sample->ticksRemaining = sample->playbackTicks;
    sample->playing = TRUE;
}

static void StepOneShotSample(struct OneShotSample *sample, WORD channel)
{
    if (!sample->playing) return;
    if (sample->ticksRemaining > 0) sample->ticksRemaining--;
    if (sample->ticksRemaining <= 0) StopOneShotSample(sample, channel);
}

static void PlayLoopedSample(struct OneShotSample *sample, WORD channel)
{
    UWORD dmaBit;

    if (!sample->loaded || !sample->data || sample->lengthWords == 0) return;

    AudioPrepareChannel(channel, SampleAudioOwner(sample));
    dmaBit = AudioDmaBit(channel);
    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;
    custom.aud[channel].ac_per = sample->period;
    custom.aud[channel].ac_vol = sample->volume;
    custom.dmacon = DMAF_SETCLR | dmaBit;
    AudioDmaLatchWait();

    if (sample->loopLengthWords > 0) {
        custom.aud[channel].ac_ptr = (UWORD *)(sample->data + ((ULONG)sample->loopStartWords * 2UL));
        custom.aud[channel].ac_len = sample->loopLengthWords;
    } else {
        custom.aud[channel].ac_ptr = (UWORD *)sample->data;
        custom.aud[channel].ac_len = sample->lengthWords;
    }

    sample->ticksRemaining = 0;
    sample->playing = TRUE;
}

static void PlayFullLoopedSample(struct OneShotSample *sample, WORD channel)
{
    UWORD dmaBit;

    if (!sample->loaded || !sample->data || sample->lengthWords == 0) return;

    AudioPrepareChannel(channel, SampleAudioOwner(sample));
    dmaBit = AudioDmaBit(channel);
    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;
    custom.aud[channel].ac_per = sample->period;
    custom.aud[channel].ac_vol = sample->volume;
    custom.dmacon = DMAF_SETCLR | dmaBit;
    AudioDmaLatchWait();

    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;

    sample->ticksRemaining = 0;
    sample->playing = TRUE;
}

static void StopGetReadySample(void)
{
    StopOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
}

static void StopCountdownSample(void)
{
    StopOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
}

static void StopGoSample(void)
{
    StopOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}

static void StopRoundStartSamples(void)
{
    StopGetReadySample();
    StopCountdownSample();
    StopGoSample();
}

static void StartRoundCountdownAudio(void)
{
    StopGameplaySamples();
    StopMenuMusic();
    StopRoundStartSamples();
    PlayGetReadySample();
}

static void PlayGetReadySample(void)
{
    PlayOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
}

static void PlayCountdownSample(void)
{
    StopCountdownSample();
    PlayOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
    if (countdownSample.playing && countdownSample.ticksRemaining > ROUND_COUNTDOWN_STEP_FRAMES) {
        countdownSample.ticksRemaining = ROUND_COUNTDOWN_STEP_FRAMES;
    }
}

static void PlayGoSample(void)
{
    StopRoundStartSamples();
    PlayOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}

static void StepRoundStartSamples(void)
{
    StepOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
    StepOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
    StepOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}

static BOOL AnyHooverMoving(void)
{
    WORD i;

    if ((gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) || roundCountdownTicks > 0 || pauseMenuOpen) return FALSE;
    for (i = 0; i < robotCount; i++) {
        if (robots[i].moving) return TRUE;
    }
    return FALSE;
}

static void StartMainGameMusic(void)
{
    if (mainMusicSample.playing) return;
    StopMenuMusic();
    StopGetReadySample();
    StopCountdownSample();
    PlayLoopedSample(&mainMusicSample, MAIN_MUSIC_LEFT_AUDIO_CHANNEL);
    PlayLoopedSample(&mainMusicSample, MAIN_MUSIC_RIGHT_AUDIO_CHANNEL);
}

static void StopGameplaySamples(void)
{
    StopOneShotSample(&mainMusicSample, MAIN_MUSIC_LEFT_AUDIO_CHANNEL);
    StopOneShotSample(&mainMusicSample, MAIN_MUSIC_RIGHT_AUDIO_CHANNEL);
    StopOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
    StopOneShotSample(&boltFireSample, BOLT_FIRE_AUDIO_CHANNEL);
}

static void ServiceHooverMoveSample(void)
{
    if (AnyHooverMoving()) {
        if (!hooverMoveSample.playing) {
            StopOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
            PlayLoopedSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
        }
    } else if (hooverMoveSample.playing) {
        StopOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
    }
}

static void PlayBoltFireSample(void)
{
    PlayOneShotSample(&boltFireSample, BOLT_FIRE_AUDIO_CHANNEL);
}

static void FreeOneShotSample(struct OneShotSample *sample, WORD channel)
{
    StopOneShotSample(sample, channel);
    if (sample->data) {
        FreeMem(sample->data, sample->dataSize);
    }
    memset(sample, 0, sizeof(*sample));
}

static BOOL LoadOneShotSample(const char *path, struct OneShotSample *sample, const char *description)
{
    BPTR fh;
    LONG size;
    LONG readSize;
    UBYTE *fileData;
    LONG pos;
    LONG bodyOffset = -1;
    LONG bodySize = 0;
    UWORD sampleRate = 0;
    UBYTE compression = 0;
    ULONG volume = 0x10000UL;
    ULONG oneShotSamples = 0;
    ULONG repeatSamples = 0;
    LONG allocSize;
    ULONG playbackTicks;

    memset(sample, 0, sizeof(*sample));

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        printf("Optional %s not loaded; %s disabled\n", path, description);
        return FALSE;
    }

    size = GetFileSize(fh);
    if (size < 20) {
        Close(fh);
        printf("%s is too small for an 8SVX sample\n", path);
        return FALSE;
    }

    fileData = (UBYTE *)AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!fileData) {
        Close(fh);
        printf("Could not allocate memory for %s\n", path);
        return FALSE;
    }

    readSize = Read(fh, fileData, size);
    Close(fh);
    if (readSize != size) {
        FreeMem(fileData, size);
        printf("Could not read %s\n", path);
        return FALSE;
    }

    if (memcmp(fileData, "FORM", 4) != 0 || memcmp(fileData + 8, "8SVX", 4) != 0) {
        FreeMem(fileData, size);
        printf("%s is not an IFF 8SVX sample\n", path);
        return FALSE;
    }

    pos = 12;
    while (pos + 8 <= size) {
        ULONG chunkSize = ReadBE32(fileData + pos + 4);
        LONG dataPos = pos + 8;
        if (dataPos + (LONG)chunkSize > size) break;

        if (memcmp(fileData + pos, "VHDR", 4) == 0 && chunkSize >= 20) {
            oneShotSamples = ReadBE32(fileData + dataPos);
            repeatSamples = ReadBE32(fileData + dataPos + 4);
            sampleRate = ReadBE16(fileData + dataPos + 12);
            compression = fileData[dataPos + 15];
            volume = ReadBE32(fileData + dataPos + 16);
        } else if (memcmp(fileData + pos, "BODY", 4) == 0) {
            bodyOffset = dataPos;
            bodySize = chunkSize;
        }

        pos = dataPos + (LONG)chunkSize + (chunkSize & 1);
    }

    if (bodyOffset < 0 || bodySize <= 1 || sampleRate == 0 || compression != 0) {
        FreeMem(fileData, size);
        printf("%s is not a playable uncompressed 8SVX sample\n", path);
        return FALSE;
    }

    allocSize = bodySize + (bodySize & 1);
    sample->data = (UBYTE *)AllocMem(allocSize, MEMF_CHIP | MEMF_PUBLIC | MEMF_CLEAR);
    if (!sample->data) {
        FreeMem(fileData, size);
        printf("Could not allocate chip RAM for %s\n", path);
        return FALSE;
    }

    CopyMem(fileData + bodyOffset, sample->data, bodySize);
    FreeMem(fileData, size);

    sample->dataSize = allocSize;
    sample->lengthWords = (UWORD)(allocSize / 2);
    sample->period = (UWORD)(PAULA_CLOCK_HZ / sampleRate);
    if (sample->period < 124) sample->period = 124;
    sample->sampleRate = (UWORD)sampleRate;
    playbackTicks = (((ULONG)bodySize * 50UL) + (ULONG)sampleRate - 1UL) / (ULONG)sampleRate;
    if (playbackTicks < 1UL) playbackTicks = 1UL;
    if (playbackTicks > 32767UL) playbackTicks = 32767UL;
    sample->playbackTicks = (UWORD)playbackTicks;
    sample->ticksRemaining = 0;
    if (repeatSamples > 1UL && repeatSamples < (ULONG)bodySize) {
        ULONG loopStartBytes;
        ULONG loopLengthBytes;

        if (oneShotSamples + repeatSamples <= (ULONG)bodySize) {
            loopStartBytes = oneShotSamples;
        } else {
            loopStartBytes = (ULONG)bodySize - repeatSamples;
        }
        loopLengthBytes = repeatSamples;
        loopStartBytes &= ~1UL;
        loopLengthBytes &= ~1UL;
        if (loopLengthBytes >= 2UL && loopStartBytes + loopLengthBytes <= (ULONG)allocSize) {
            sample->loopStartWords = (UWORD)(loopStartBytes / 2UL);
            sample->loopLengthWords = (UWORD)(loopLengthBytes / 2UL);
        }
    }
    sample->volume = (volume >= 0x10000UL) ? 64 : (UBYTE)((volume * 64UL) / 0x10000UL);
    if (sample->volume == 0) sample->volume = 64;
    sample->loaded = TRUE;
    return TRUE;
}

static BOOL LoadRoundStartSamples(void)
{
    BOOL loaded = FALSE;

    loaded |= LoadOneShotSample(GET_READY_SAMPLE_PATH, &getReadySample, "round countdown voice");
    loaded |= LoadOneShotSample(COUNTDOWN_SAMPLE_PATH, &countdownSample, "countdown number voice");
    loaded |= LoadOneShotSample(GO_SAMPLE_PATH, &goSample, "go voice");
    return loaded;
}

static void FreeRoundStartSamples(void)
{
    FreeOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
    FreeOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
    FreeOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}

/* Keep the complete 8SVX BODY in ordinary/Fast RAM, but feed Paula through
 * two small chip-RAM buffers. Paula itself tells us when a block has finished
 * through INTREQR, so no guessed 50 Hz chunk timer is involved and AUDxLEN
 * never has to describe more than one legal DMA block. */
static BOOL LoadMenuMusicStream(void)
{
    BPTR fh;
    UBYTE header[12];
    UBYTE chunkHeader[8];
    LONG fileSize;
    LONG pos;
    LONG bodyStart = 0;
    ULONG bodySize = 0;
    UWORD sampleRate = 0;
    UBYTE compression = 0;
    ULONG volume = 0x10000UL;
    BOOL haveVHDR = FALSE;
    BOOL haveBODY = FALSE;
    ULONG allocSize;
    UBYTE *data;
    ULONG got;
    LONG readSize;
    WORD i;

    memset(&menuMusicSample, 0, sizeof(menuMusicSample));
    menuMusicSourceBytes = 0;
    menuMusicNextOffsetBytes = 0;
    menuMusicCurrentBuf = 0;

    fh = Open((STRPTR)MENU_MUSIC_SAMPLE_PATH, MODE_OLDFILE);
    if (!fh) {
        printf("Optional %s not loaded; menu music disabled\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    fileSize = GetFileSize(fh);
    if (fileSize < 20 || Read(fh, header, 12) != 12 ||
        memcmp(header, "FORM", 4) != 0 || memcmp(header + 8, "8SVX", 4) != 0) {
        Close(fh);
        printf("%s is not an IFF 8SVX sample\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    pos = 12;
    while (pos + 8 <= fileSize) {
        ULONG chunkSize;
        LONG dataPos;

        if (Seek(fh, pos, OFFSET_BEGINNING) == -1) break;
        if (Read(fh, chunkHeader, 8) != 8) break;
        chunkSize = ReadBE32(chunkHeader + 4);
        dataPos = pos + 8;
        if (dataPos + (LONG)chunkSize > fileSize) break;

        if (memcmp(chunkHeader, "VHDR", 4) == 0 && chunkSize >= 20) {
            UBYTE vhdr[20];
            if (Read(fh, vhdr, 20) == 20) {
                sampleRate = ReadBE16(vhdr + 12);
                compression = vhdr[15];
                volume = ReadBE32(vhdr + 16);
                haveVHDR = TRUE;
            }
        } else if (memcmp(chunkHeader, "BODY", 4) == 0) {
            bodyStart = dataPos;
            bodySize = chunkSize;
            haveBODY = TRUE;
            break;
        }

        pos = dataPos + (LONG)chunkSize + ((LONG)chunkSize & 1);
    }

    if (!haveVHDR || !haveBODY || bodySize <= 1 || sampleRate == 0 || compression != 0) {
        Close(fh);
        printf("%s is not a playable uncompressed 8SVX sample\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    /* Reserve the DMA-visible buffers first so a no-Fast-RAM fallback cannot
     * consume all chip RAM before Paula gets its two streaming buffers. */
    for (i = 0; i < 2; i++) {
        if (!menuMusicChunkBuf[i]) {
            menuMusicChunkBuf[i] = (UBYTE *)AllocMem(MENU_MUSIC_STREAM_CHUNK_BYTES,
                                                     MEMF_CHIP | MEMF_PUBLIC);
        }
    }
    if (!menuMusicChunkBuf[0] || !menuMusicChunkBuf[1]) {
        Close(fh);
        FreeMenuMusicSample();
        printf("Could not allocate chip RAM buffers for %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    allocSize = bodySize & ~1UL;
    data = (UBYTE *)AllocMem(allocSize, MEMF_FAST | MEMF_PUBLIC);
    if (!data) {
        /* Machines with no Fast RAM can still try the old all-public-memory
         * route; the two chip streaming buffers are already safely reserved. */
        data = (UBYTE *)AllocMem(allocSize, MEMF_PUBLIC);
    }
    if (!data) {
        Close(fh);
        FreeMenuMusicSample();
        printf("Could not allocate source RAM for %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }
    menuMusicSample.data = data;
    menuMusicSample.dataSize = (LONG)allocSize;

    if (Seek(fh, bodyStart, OFFSET_BEGINNING) == -1) {
        Close(fh);
        FreeMenuMusicSample();
        printf("Could not read %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    got = 0;
    while (got < allocSize) {
        readSize = Read(fh, data + got, (LONG)(allocSize - got));
        if (readSize <= 0) break;
        got += (ULONG)readSize;
    }
    Close(fh);
    got &= ~1UL;

    if (got < 2UL) {
        FreeMenuMusicSample();
        printf("Could not read %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    menuMusicSourceBytes = got;
    /* lengthWords deliberately stays zero: the source is not itself a legal
     * Paula DMA block and must never be handed to PlayLoopedSample(). */
    menuMusicSample.lengthWords = 0;
    menuMusicSample.period = (UWORD)(PAULA_CLOCK_HZ / sampleRate);
    if (menuMusicSample.period < 124) menuMusicSample.period = 124;
    menuMusicSample.sampleRate = sampleRate;
    menuMusicSample.volume = (volume >= 0x10000UL) ? 64 : (UBYTE)((volume * 64UL) / 0x10000UL);
    if (menuMusicSample.volume == 0) menuMusicSample.volume = 64;
    menuMusicSample.loaded = TRUE;
    return TRUE;
}

static UWORD FillMenuMusicChunk(WORD bufferIndex)
{
    ULONG remaining;
    ULONG chunkBytes;

    if (bufferIndex < 0 || bufferIndex > 1) return 0;
    if (!menuMusicSample.loaded || !menuMusicSample.data || !menuMusicChunkBuf[bufferIndex]) return 0;
    if (menuMusicSourceBytes < 2UL) return 0;

    if (menuMusicNextOffsetBytes >= menuMusicSourceBytes) menuMusicNextOffsetBytes = 0;
    remaining = menuMusicSourceBytes - menuMusicNextOffsetBytes;
    chunkBytes = (remaining > MENU_MUSIC_STREAM_CHUNK_BYTES) ?
                 MENU_MUSIC_STREAM_CHUNK_BYTES : remaining;
    chunkBytes &= ~1UL;

    if (chunkBytes < 2UL) {
        menuMusicNextOffsetBytes = 0;
        remaining = menuMusicSourceBytes;
        chunkBytes = (remaining > MENU_MUSIC_STREAM_CHUNK_BYTES) ?
                     MENU_MUSIC_STREAM_CHUNK_BYTES : remaining;
        chunkBytes &= ~1UL;
    }
    if (chunkBytes < 2UL) return 0;

    CopyMem(menuMusicSample.data + menuMusicNextOffsetBytes,
            menuMusicChunkBuf[bufferIndex], chunkBytes);
    menuMusicNextOffsetBytes += chunkBytes;
    if (menuMusicNextOffsetBytes >= menuMusicSourceBytes) menuMusicNextOffsetBytes = 0;

    return (UWORD)(chunkBytes / 2UL);
}

static void StopMenuMusic(void)
{
    StopOneShotSample(&menuMusicSample, MENU_MUSIC_LEFT_AUDIO_CHANNEL);
    StopOneShotSample(&menuMusicSample, MENU_MUSIC_RIGHT_AUDIO_CHANNEL);
    custom.intreq = MENU_MUSIC_INT_MASK;

    if (menuMusicIntenaSaved) {
        if (menuMusicSavedAudioIntena != 0) {
            custom.intena = INTF_SETCLR | menuMusicSavedAudioIntena;
        }
        menuMusicSavedAudioIntena = 0;
        menuMusicIntenaSaved = FALSE;
    }

    menuMusicNextOffsetBytes = 0;
    menuMusicCurrentBuf = 0;
}

static void StartMenuMusic(void)
{
    UWORD firstWords;
    UWORD secondWords;
    UWORD leftDmaBit;
    UWORD rightDmaBit;

    if (menuMusicSample.playing) return;
    if (!menuMusicSample.loaded || !menuMusicSample.data || menuMusicSourceBytes < 2UL) return;
    if (!menuMusicChunkBuf[0] || !menuMusicChunkBuf[1]) return;

    menuMusicNextOffsetBytes = 0;
    firstWords = FillMenuMusicChunk(0);
    secondWords = FillMenuMusicChunk(1);
    if (firstWords == 0 || secondWords == 0) return;

    menuMusicSavedAudioIntena = custom.intenar & MENU_MUSIC_INT_MASK;
    menuMusicIntenaSaved = TRUE;
    /* Poll INTREQR ourselves; do not let a level-4 audio ISR consume the
     * block-finished flag before the title loop sees it. */
    custom.intena = MENU_MUSIC_INT_MASK;
    custom.intreq = MENU_MUSIC_INT_MASK;

    AudioPrepareChannel(MENU_MUSIC_LEFT_AUDIO_CHANNEL, AUDIO_OWNER_MENU_MUSIC);
    AudioPrepareChannel(MENU_MUSIC_RIGHT_AUDIO_CHANNEL, AUDIO_OWNER_MENU_MUSIC);
    leftDmaBit = AudioDmaBit(MENU_MUSIC_LEFT_AUDIO_CHANNEL);
    rightDmaBit = AudioDmaBit(MENU_MUSIC_RIGHT_AUDIO_CHANNEL);

    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[0];
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_len = firstWords;
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_per = menuMusicSample.period;
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_vol = menuMusicSample.volume;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[0];
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_len = firstWords;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_per = menuMusicSample.period;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_vol = menuMusicSample.volume;

    custom.dmacon = DMAF_SETCLR | leftDmaBit | rightDmaBit;
    AudioDmaLatchWait();

    /* Paula has latched buffer 0. These visible pointer/length registers are
     * now the reload block that becomes active when buffer 0 finishes. */
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[1];
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_len = secondWords;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[1];
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_len = secondWords;

    /* Discard any start-up request; the next pair of flags means block 0 has
     * genuinely completed on both stereo channels. */
    custom.intreq = MENU_MUSIC_INT_MASK;
    menuMusicCurrentBuf = 0;
    menuMusicSample.playing = TRUE;
}

static void ServiceMenuMusicStream(void)
{
    UWORD pending;
    WORD refillBuf;
    UWORD words;

    if (!menuMusicSample.playing) return;

    pending = custom.intreqr & MENU_MUSIC_INT_MASK;
    /* Both channels play the same buffer. Wait until both have crossed the
     * boundary before overwriting the buffer they just stopped using. */
    if (pending != MENU_MUSIC_INT_MASK) return;
    custom.intreq = MENU_MUSIC_INT_MASK;

    menuMusicCurrentBuf ^= 1;
    refillBuf = menuMusicCurrentBuf ^ 1;
    words = FillMenuMusicChunk(refillBuf);
    if (words == 0) {
        StopMenuMusic();
        return;
    }

    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[refillBuf];
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_len = words;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[refillBuf];
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_len = words;
}

static void ServiceMenuMusicForState(void)
{
    if (gameState == GAME_INTRO || gameState == GAME_TITLE) {
        StartMenuMusic();
        ServiceMenuMusicStream();
    } else if (menuMusicSample.playing) {
        StopMenuMusic();
    }
}

static void FreeMenuMusicSample(void)
{
    WORD i;

    StopMenuMusic();
    if (menuMusicSample.data) {
        FreeMem(menuMusicSample.data, menuMusicSample.dataSize);
    }
    for (i = 0; i < 2; i++) {
        if (menuMusicChunkBuf[i]) {
            FreeMem(menuMusicChunkBuf[i], MENU_MUSIC_STREAM_CHUNK_BYTES);
            menuMusicChunkBuf[i] = NULL;
        }
    }
    menuMusicSourceBytes = 0;
    menuMusicNextOffsetBytes = 0;
    menuMusicCurrentBuf = 0;
    memset(&menuMusicSample, 0, sizeof(menuMusicSample));
}

static BOOL LoadGameplaySamples(void)
{
    BOOL loaded = FALSE;

    loaded |= LoadOneShotSample(MAIN_MUSIC_SAMPLE_PATH, &mainMusicSample, "main game music");
    loaded |= LoadOneShotSample(BOLT_FIRE_SAMPLE_PATH, &boltFireSample, "bolt fire effect");
    loaded |= LoadOneShotSample(HOOVER_MOVE_SAMPLE_PATH, &hooverMoveSample, "hoover movement loop");
    if (hooverMoveSample.loaded) {
        hooverMoveSample.lengthWords = (UWORD)((hooverMoveSample.dataSize & ~1L) / 2L);
        if (hooverMoveSample.loopLengthWords > 0 &&
            ((ULONG)hooverMoveSample.loopStartWords + (ULONG)hooverMoveSample.loopLengthWords >
             (ULONG)hooverMoveSample.lengthWords)) {
            hooverMoveSample.loopStartWords = 0;
            hooverMoveSample.loopLengthWords = hooverMoveSample.lengthWords;
        }
    }
    return loaded;
}

static void FreeGameplaySamples(void)
{
    StopOneShotSample(&mainMusicSample, MAIN_MUSIC_RIGHT_AUDIO_CHANNEL);
    FreeOneShotSample(&mainMusicSample, MAIN_MUSIC_LEFT_AUDIO_CHANNEL);
    FreeOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
    FreeOneShotSample(&boltFireSample, BOLT_FIRE_AUDIO_CHANNEL);
}

static UWORD ReadBE16(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | p[1]);
}

static LONG GetFileSize(BPTR fh)
{
    LONG size;

    if (Seek(fh, 0, OFFSET_END) == -1) return -1;
    size = Seek(fh, 0, OFFSET_CURRENT);
    if (size == -1) return -1;
    if (Seek(fh, 0, OFFSET_BEGINNING) == -1) return -1;
    return size;
}


static ULONG ReadBE32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

static BOOL RoundStartLocked(void)
{
    return ((gameState == GAME_PLAYING || gameState == GAME_BONUS_PLAYING) &&
            roundCountdownTicks > 0) ? TRUE : FALSE;
}

static UWORD AudioDmaBit(WORD channel)
{
    switch (channel) {
        case 0: return DMAF_AUD0;
        case 1: return DMAF_AUD1;
        case 2: return DMAF_AUD2;
        default: return DMAF_AUD3;
    }
}

static void ServiceTitleMusicForState(void)
{
    if (gameState == GAME_TITLE && !titleFirstFullPresentDone) return;
    ServiceMenuMusicForState();
}

static BOOL LoadTileSheetIntoCache(void)
{
    Object *dto;
    struct BitMapHeader *bmhd = NULL;
    struct BitMap *srcBM = NULL;
    ULONG *cRegs = NULL;
    ULONG numColors = 0;
    WORD i;

    dto = NewDTObject("PROGDIR:tiles/world-tile.iff",
                      DTA_GroupID, GID_PICTURE,
                      PDTA_Remap, FALSE,
                      TAG_DONE);
    if (!dto) {
        return FALSE;
    }

    if (!DoDTMethod(dto, NULL, NULL, DTM_PROCLAYOUT, 0L, TRUE)) {
        DisposeDTObject(dto);
        return FALSE;
    }

    if (!GetDTAttrs(dto,
                    PDTA_BitMapHeader, (ULONG)&bmhd,
                    PDTA_DestBitMap, (ULONG)&srcBM,
                    TAG_DONE) || !bmhd || !srcBM) {
        DisposeDTObject(dto);
        return FALSE;
    }

    if (bmhd->bmh_Width < (TILE_SIZE * TILE_COUNT) || bmhd->bmh_Height < TILE_SIZE) {
        DisposeDTObject(dto);
        return FALSE;
    }

    for (i = 0; i < TILE_COUNT; i++) {
        BltBitMap(srcBM, i * TILE_SIZE, 0,
                  tileCacheBM, i * TILE_SIZE, 0,
                  TILE_SIZE, TILE_SIZE,
                  0xC0, 0xFF, NULL);
    }

    if (GetDTAttrs(dto,
                   PDTA_CRegs, (ULONG)&cRegs,
                   PDTA_NumColors, (ULONG)&numColors,
                   TAG_DONE) && cRegs && numColors) {
        ULONG maxCols = (numColors > 16) ? 16 : numColors;
        for (i = 0; i < (WORD)maxCols; i++) {
            palette[i] = (UWORD)(((cRegs[i * 3 + 0] >> 28) << 8) |
                                 ((cRegs[i * 3 + 1] >> 28) << 4) |
                                 (cRegs[i * 3 + 2] >> 28));
        }
    }

    DisposeDTObject(dto);
    return TRUE;
}

static void BuildWallRotationsInCache(void)
{
    WORD srcBaseX = TILE_WALL * TILE_SIZE;
    UBYTE rot;

    for (rot = 0; rot < WALL_ROTATION_COUNT; rot++) {
        WORD dstBaseX = (TILE_COUNT + rot) * TILE_SIZE;
        WORD x;
        WORD y;

        if (rot == 0) {
            BltBitMap(tileCacheBM, srcBaseX, 0,
                      tileCacheBM, dstBaseX, 0,
                      TILE_SIZE, TILE_SIZE,
                      0xC0, 0xFF, NULL);
            continue;
        }

        for (y = 0; y < TILE_SIZE; y++) {
            for (x = 0; x < TILE_SIZE; x++) {
                WORD sx;
                WORD sy;
                LONG pen;

                if (rot == 1) {
                    sx = y;
                    sy = TILE_SIZE - 1 - x;
                } else if (rot == 2) {
                    sx = TILE_SIZE - 1 - x;
                    sy = TILE_SIZE - 1 - y;
                } else {
                    sx = TILE_SIZE - 1 - y;
                    sy = x;
                }

                pen = ReadPixel(&tileRP, srcBaseX + sx, sy);
                SetAPen(&tileRP, (UBYTE)pen);
                WritePixel(&tileRP, dstBaseX + x, y);
            }
        }
    }
}

static BOOL InitTileCache(void)
{
    UBYTE i;
    tileCacheBM = AllocBitMap(TILE_SIZE * TILE_CACHE_COUNT, TILE_SIZE, DEPTH,
                              BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    if (!tileCacheBM) {
        printf("Could not allocate tile cache bitmap\n");
        return FALSE;
    }

    InitRastPort(&tileRP);
    tileRP.BitMap = tileCacheBM;

    if (!LoadTileSheetIntoCache()) {
        for (i = 0; i < TILE_COUNT; i++) {
            DrawTileIntoCache(i);
        }
    }

    BuildWallRotationsInCache();

    return TRUE;
}

static void BlitTileTo(struct RastPort *rp, UBYTE tileType, WORD tx, WORD ty)
{
    WORD srcX;
    WORD dstX;
    WORD dstY;

    if (!tileCacheBM) return;
    if (tileType >= TILE_COUNT) tileType = TILE_FLOOR;

    srcX = tileType * TILE_SIZE;
    dstX = MAP_X + tx * TILE_SIZE;
    dstY = MAP_Y + ty * TILE_SIZE;

    BltBitMapRastPort(tileCacheBM, srcX, 0,
                      rp, dstX, dstY,
                      TILE_SIZE, TILE_SIZE,
                      0xC0);
}

static BOOL IsWallTileAt(WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return FALSE;
    return map[ty][tx] == TILE_WALL;
}

static UBYTE GetWallRotation(WORD tx, WORD ty)
{
    if (!IsWallTileAt(tx - 1, ty)) return 1; /* 90 degrees */
    if (!IsWallTileAt(tx, ty - 1)) return 2; /* 180 degrees */
    if (!IsWallTileAt(tx + 1, ty)) return 3; /* 270 degrees */
    return 0; /* default: bottom edge */
}

static void BlitWallRotatedTo(struct RastPort *rp, WORD tx, WORD ty)
{
    UBYTE rot = GetWallRotation(tx, ty);
    WORD srcX = (TILE_COUNT + rot) * TILE_SIZE;
    WORD dstX = MAP_X + tx * TILE_SIZE;
    WORD dstY = MAP_Y + ty * TILE_SIZE;

    if (!tileCacheBM) return;

    BltBitMapRastPort(tileCacheBM, srcX, 0,
                      rp, dstX, dstY,
                      TILE_SIZE, TILE_SIZE,
                      0xC0);
}

static void UpdateRoomTile(WORD tx, WORD ty)
{
    if (!roomBM || tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return;
    if (map[ty][tx] == TILE_WALL) {
        BlitWallRotatedTo(&roomRP, tx, ty);
    } else {
        BlitTileTo(&roomRP, map[ty][tx], tx, ty);
    }
    AddDirtyTile(tx, ty);
}

static void BuildRoomBuffer(void)
{
    WORD x;
    WORD y;

    if (!roomBM) return;

    SetAPen(&roomRP, 0);
    RectFill(&roomRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            UpdateRoomTile(x, y);
        }
    }
}

/* -------------------------------------------------------------------------
 * Robot BOB cache
 * ------------------------------------------------------------------------- */

static void CopyRobotPixel(struct RastPort *srcRP, struct RastPort *dstRP, struct RastPort *maskRP,
                           WORD srcX, WORD srcY, WORD dstX, WORD dstY)
{
    LONG srcPen = ReadPixel(srcRP, srcX, srcY);

    if (srcPen <= 0) {
        SetAPen(maskRP, 0);
        WritePixel(maskRP, dstX, dstY);
        return;
    }

    if (srcPen > 15) srcPen = 15;

    SetAPen(dstRP, (UBYTE)(srcPen + 16));
    WritePixel(dstRP, dstX, dstY);

    SetAPen(maskRP, 1);
    WritePixel(maskRP, dstX, dstY);
}

static void BlitRobotFrameRotated(struct RastPort *srcRP, struct RastPort *dstRP, struct RastPort *maskRP,
                                  WORD srcBaseX, WORD dstBaseX, WORD rotation)
{
    WORD x, y;

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            WORD dx = x;
            WORD dy = y;

            if (rotation == 90) {
                dx = ROBOT_W - 1 - y;
                dy = x;
            } else if (rotation == 180) {
                dx = ROBOT_W - 1 - x;
                dy = ROBOT_H - 1 - y;
            } else if (rotation == 270) {
                dx = y;
                dy = ROBOT_H - 1 - x;
            }

            CopyRobotPixel(srcRP, dstRP, maskRP,
                           srcBaseX + x, y,
                           dstBaseX + dx, dy);
        }
    }
}

static void BlitRobotVariant(Object *dto, struct RastPort *dstRP, struct RastPort *maskRP,
                             WORD variantIndex, WORD baseRotation)
{
    struct BitMapHeader *bmhd = NULL;
    struct BitMap *srcBM = NULL;
    struct RastPort srcRP;
    WORD variantBaseX = variantIndex * SPR_STATE_COUNT * ROBOT_W;
    LONG layoutResult;

    if (!dto) return;
    layoutResult = DoDTMethod(dto, NULL, NULL, DTM_PROCLAYOUT, 0L, TRUE);
    if (layoutResult == 0) return;

    GetDTAttrs(dto,
               PDTA_BitMapHeader, (ULONG)&bmhd,
               PDTA_BitMap, (ULONG)&srcBM,
               TAG_DONE);
    if (!bmhd || !srcBM || bmhd->bmh_Width < ROBOT_W || bmhd->bmh_Height < ROBOT_H) return;

    InitRastPort(&srcRP);
    srcRP.BitMap = srcBM;

    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_UP * ROBOT_W), (baseRotation + 0) % 360);
    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_RIGHT * ROBOT_W), (baseRotation + 90) % 360);
    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_DOWN * ROBOT_W), (baseRotation + 180) % 360);
    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_LEFT * ROBOT_W), (baseRotation + 270) % 360);

    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_READY * ROBOT_W), baseRotation % 360);
    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_CHARGING * ROBOT_W), baseRotation % 360);
    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_LOW_BATTERY * ROBOT_W), baseRotation % 360);
    BlitRobotFrameRotated(&srcRP, dstRP, maskRP, 0, variantBaseX + (SPR_ENERGY_BOLT * ROBOT_W), baseRotation % 360);
}


static BOOL ReadExact(BPTR file, void *buffer, LONG size)
{
    return Read(file, buffer, size) == size;
}

static BOOL SkipBytes(BPTR file, ULONG size)
{
    UBYTE scratch[64];
    ULONG remaining = size;

    while (remaining > 0) {
        LONG chunk = (remaining > sizeof(scratch)) ? sizeof(scratch) : remaining;
        if (Read(file, scratch, chunk) != chunk) return FALSE;
        remaining -= chunk;
    }

    return TRUE;
}

static ULONG ReadBigEndian32(const UBYTE *b)
{
    return ((ULONG)b[0] << 24) | ((ULONG)b[1] << 16) | ((ULONG)b[2] << 8) | (ULONG)b[3];
}

static BOOL LoadPaletteCMapInto(const char *path, UWORD *destPalette, WORD firstPen, WORD maxPens)
{
    BPTR file;
    UBYTE header[12];
    UBYTE chunkHeader[8];
    BOOL loaded = FALSE;

    if (!destPalette || firstPen < 0 || maxPens <= 0 || firstPen >= 32) return FALSE;
    if (firstPen + maxPens > 32) maxPens = 32 - firstPen;

    file = Open((STRPTR)path, MODE_OLDFILE);
    if (!file) return FALSE;

    if (!ReadExact(file, header, sizeof(header)) ||
        header[0] != 'F' || header[1] != 'O' || header[2] != 'R' || header[3] != 'M') {
        Close(file);
        return FALSE;
    }

    while (Read(file, chunkHeader, sizeof(chunkHeader)) == sizeof(chunkHeader)) {
        ULONG chunkSize = ReadBigEndian32(chunkHeader + 4);
        ULONG paddedSize = chunkSize + (chunkSize & 1);

        if (chunkHeader[0] == 'C' && chunkHeader[1] == 'M' && chunkHeader[2] == 'A' && chunkHeader[3] == 'P') {
            ULONG entries = chunkSize / 3;
            ULONG i;

            for (i = 0; i < entries; i++) {
                UBYTE rgb[3];
                if (!ReadExact(file, rgb, sizeof(rgb))) {
                    Close(file);
                    return loaded;
                }
                if (i < (ULONG)maxPens) {
                    destPalette[firstPen + i] = (UWORD)(((rgb[0] >> 4) << 8) |
                                                        ((rgb[1] >> 4) << 4) |
                                                         (rgb[2] >> 4));
                    loaded = TRUE;
                }
            }

            if (chunkSize > entries * 3 && !SkipBytes(file, chunkSize - entries * 3)) {
                Close(file);
                return loaded;
            }
            if ((chunkSize & 1) && !SkipBytes(file, 1)) {
                Close(file);
                return loaded;
            }
            break;
        }

        if (!SkipBytes(file, paddedSize)) break;
    }

    Close(file);
    return loaded;
}

static BOOL LoadPaletteCMap(const char *path, WORD firstPen, WORD maxPens)
{
    return LoadPaletteCMapInto(path, palette, firstPen, maxPens);
}

static void DisableTitleCopperGradient(BOOL reloadPalette)
{
    struct ViewPort *viewPort;
    BOOL detached = FALSE;

    if (!scr || !titleCopperActive) return;

    viewPort = win ? ViewPortAddress(win) : &scr->ViewPort;
    Forbid();
    if (viewPort->UCopIns == titleUCopList) {
        viewPort->UCopIns = NULL;
        detached = TRUE;
    }
    Permit();

    if (detached) {
        RethinkDisplay();
    }
    titleCopperActive = FALSE;

    if (reloadPalette) {
        LoadRGB4(&scr->ViewPort, palette, 32);
        ForceGameplayFullPresent();
    }
}

static BOOL EnableTitleCopperGradient(void)
{
    struct ViewPort *viewPort;
    struct TagItem uCopTags[] = {
        { VTAG_USERCLIP_SET, TRUE },
        { VTAG_END_CM, 0 }
    };
    WORD band;
    WORD top = TITLE_CAROUSEL_Y - 8;
    WORD height = SCREEN_H - top;

    if (!scr) return FALSE;
    if (titleCopperActive) return TRUE;

    if (!titleUCopList) {
        titleUCopList = (struct UCopList *)AllocMem(sizeof(struct UCopList), MEMF_PUBLIC | MEMF_CLEAR);
        if (!titleUCopList) return FALSE;

        CINIT(titleUCopList, TITLE_COPPER_BANDS);

        for (band = 0; band < TITLE_COPPER_BANDS; band++) {
            WORD y = top + ((band * height) / TITLE_COPPER_BANDS);
            WORD r;
            WORD g;
            WORD b;
            UWORD color;

            if (band < (TITLE_COPPER_BANDS / 2)) {
                WORD level = 12 - ((12 * band) / ((TITLE_COPPER_BANDS / 2) - 1));
                r = level;
                g = level;
                b = level;
            } else {
                WORD level = (15 * (band - (TITLE_COPPER_BANDS / 2))) / ((TITLE_COPPER_BANDS / 2) - 1);
                r = level;
                g = level;
                b = level;
            }

            color = (UWORD)((r << 8) | (g << 4) | b);
            CWAIT(titleUCopList, y, 0);
            CMOVE(titleUCopList, custom.color[TITLE_COPPER_PEN], color);
        }

        CEND(titleUCopList);
    }

    viewPort = win ? ViewPortAddress(win) : &scr->ViewPort;
    Forbid();
    viewPort->UCopIns = titleUCopList;
    Permit();

    VideoControl(viewPort->ColorMap, uCopTags);
    RethinkDisplay();
    titleCopperActive = TRUE;
    return TRUE;
}


static void FreeTitleCopperGradient(void)
{
    struct ViewPort *viewPort;

    if (!scr || !titleUCopList) return;

    viewPort = win ? ViewPortAddress(win) : &scr->ViewPort;
    Forbid();
    if (viewPort->UCopIns == titleUCopList) viewPort->UCopIns = NULL;
    Permit();
    RethinkDisplay();

    FreeVPortCopLists(viewPort);
    titleUCopList = NULL;
    titleCopperActive = FALSE;
}

static WORD EmpPalettePhase(void)
{
    WORD phase;

    if (empCountdownTicks <= 0) return 0;
    phase = (WORD)((POWERUP_EMP_TICKS - empCountdownTicks) / EMP_PALETTE_CYCLE_FRAMES);
    phase %= 5;
    if (phase < 0) phase = 0;
    return phase;
}

static UBYTE EmpWarningPen(void)
{
    static const UBYTE warningPens[5] = { 13, 10, 13, 13, 12 };
    return warningPens[EmpPalettePhase()];
}

static void StopEmpPaletteCycle(void)
{
    if (!empPaletteCycleActive && empPaletteCyclePhase < 0) return;
    empPaletteCycleActive = FALSE;
    empPaletteCyclePhase = -1;
    if (scr) LoadRGB4(&scr->ViewPort, palette, 32);
}

static void UpdateEmpPaletteCycle(void)
{
    static const UWORD floorCycle[5] = { 0xBDB, 0x8B8, 0x585, 0x353, 0x171 };
    static const UWORD lightCycleA[5] = { 0xFD0, 0x0F8, 0xFD0, 0xF72, 0xF22 };
    static const UWORD lightCycleB[5] = { 0xFF6, 0x7F7, 0xFF6, 0xFA4, 0xF44 };
    UWORD cycled[32];
    WORD phase;
    WORD i;

    if (!scr) return;

    if (gameState == GAME_BONUS_PLAYING && bonusBossExplosionTicks > 0) {
        static const UWORD epicCycle[6][4] = {
            { 0x000, 0xF22, 0xFA0, 0xFFF },
            { 0x112, 0xF6F, 0xFF4, 0x6FF },
            { 0x201, 0xF80, 0xFD0, 0xF4F },
            { 0x003, 0x44F, 0x8FF, 0xFFF },
            { 0x210, 0xF44, 0xFF8, 0x4F8 },
            { 0x000, 0xF0F, 0xF84, 0x8FF }
        };
        phase = (WORD)(((BONUS_BOSS_EXPLOSION_TICKS - bonusBossExplosionTicks) / 3) % 6);
        if (!empPaletteCycleActive || empPaletteCyclePhase != phase) {
            for (i = 0; i < 32; i++) cycled[i] = palette[i];
            cycled[0] = epicCycle[phase][0];
            cycled[7] = epicCycle[phase][1];
            cycled[12] = epicCycle[phase][2];
            cycled[13] = epicCycle[phase][3];
            LoadRGB4(&scr->ViewPort, cycled, 32);
            empPaletteCycleActive = TRUE;
            empPaletteCyclePhase = phase;
        }
        return;
    }

    if ((gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) || empCountdownTicks <= 0) {
        StopEmpPaletteCycle();
        return;
    }

    phase = EmpPalettePhase();

    if (empPaletteCycleActive && empPaletteCyclePhase == phase) return;

    for (i = 0; i < 32; i++) cycled[i] = palette[i];

    cycled[EMP_FLOOR_PEN] = floorCycle[phase];
    cycled[EMP_ROBOT_LIGHT_PEN_A] = lightCycleA[phase];
    cycled[EMP_ROBOT_LIGHT_PEN_B] = lightCycleB[phase];

    LoadRGB4(&scr->ViewPort, cycled, 32);
    empPaletteCycleActive = TRUE;
    empPaletteCyclePhase = phase;
}

static void StopNightMode(void)
{
    if (!nightModeActive) return;
    nightModeActive = FALSE;
    if (scr) LoadRGB4(&scr->ViewPort, palette, 32);
}

/* Randomly dims the bonus-boss arena to near black for a few seconds at a
 * time, leaving only the robots' light pens (and the status-text pens drawn
 * over them) bright, then restores the normal palette. Uses a countdown
 * timer the same way StepBonusBoss's pattern picker does, rather than a
 * per-tick chance, so the on/off durations stay predictable and bounded. */
static void UpdateNightMode(void)
{
    if (gameState != GAME_BONUS_PLAYING || bonusBossHealth <= 0 ||
        bonusBossExplosionTicks > 0 || empCountdownTicks > 0) {
        StopNightMode();
        nightModeTicks = 0;
        return;
    }

    if (nightModeTicks > 0) {
        if (!nightModeActive) {
            UWORD dimmed[32];
            WORD i;

            for (i = 0; i < 32; i++) {
                UWORD c = palette[i];
                UWORD r = (c >> 8) & 0xF;
                UWORD g = (c >> 4) & 0xF;
                UWORD b = c & 0xF;
                dimmed[i] = (UWORD)(((r / NIGHT_MODE_DIM_DIVISOR) << 8) |
                                     ((g / NIGHT_MODE_DIM_DIVISOR) << 4) |
                                      (b / NIGHT_MODE_DIM_DIVISOR));
            }
            /* Pens 7 and 10-15 are the vivid accent colours the robot sprite
             * sheets actually paint their small highlight/sensor-light
             * details with (confirmed by decoding the airobotN.iff art —
             * most variants' bright pixels land on 7, 10, 12, or 14; a
             * couple of variants barely have any bright pixels to begin
             * with, which no palette trick can invent). Keep the whole
             * cluster lit, plus the EMP system's own light pens for
             * consistency, so as many variants as possible actually glow. */
            for (i = 0; i < 32; i++) {
                BOOL keepLit = (i == 7) || (i >= 10 && i <= 15) ||
                                (i == EMP_ROBOT_LIGHT_PEN_A) || (i == EMP_ROBOT_LIGHT_PEN_B);
                if (keepLit) dimmed[i] = palette[i];
            }
            dimmed[EMP_ROBOT_LIGHT_PEN_A] = 0xFFF;
            dimmed[EMP_ROBOT_LIGHT_PEN_B] = 0xFFF;
            if (scr) LoadRGB4(&scr->ViewPort, dimmed, 32);
            nightModeActive = TRUE;
        }
        nightModeTicks--;
        if (nightModeTicks <= 0) {
            StopNightMode();
            nightModeCooldownTicks = NIGHT_MODE_MIN_GAP_TICKS +
                (WORD)RandRange(NIGHT_MODE_MAX_GAP_TICKS - NIGHT_MODE_MIN_GAP_TICKS + 1);
        }
        return;
    }

    if (nightModeCooldownTicks > 0) {
        nightModeCooldownTicks--;
        return;
    }

    nightModeTicks = NIGHT_MODE_MIN_DURATION_TICKS +
        (WORD)RandRange(NIGHT_MODE_MAX_DURATION_TICKS - NIGHT_MODE_MIN_DURATION_TICKS + 1);
}

static void LoadGamePalette(void)
{
    DisableTitleCopperGradient(FALSE);
    empPaletteCycleActive = FALSE;
    nightModeActive = FALSE;
    empPaletteCyclePhase = -1;
    LoadRGB4(&scr->ViewPort, palette, 32);
    introPaletteActive = FALSE;
    ForceGameplayFullPresent();
}

static void LoadIntroPaletteLevel(WORD level)
{
    UWORD faded[32];
    WORD i;

    if (!scr) return;
    if (level < 0) level = 0;
    if (level > INTRO_EFFECT_FRAMES) level = INTRO_EFFECT_FRAMES;

    for (i = 0; i < 32; i++) {
        WORD r = (introPalette[i] >> 8) & 0xF;
        WORD g = (introPalette[i] >> 4) & 0xF;
        WORD b = introPalette[i] & 0xF;
        r = (r * level) / INTRO_EFFECT_FRAMES;
        g = (g * level) / INTRO_EFFECT_FRAMES;
        b = (b * level) / INTRO_EFFECT_FRAMES;
        faded[i] = (UWORD)((r << 8) | (g << 4) | b);
    }

    LoadRGB4(&scr->ViewPort, faded, 32);
    introPaletteActive = TRUE;
}

static void CaptureIntroPalette(ULONG *cRegs, ULONG numColors)
{
    WORD i;

    for (i = 0; i < 32; i++) introPalette[i] = 0x000;

    if (cRegs && numColors) {
        ULONG maxCols = (numColors > 32) ? 32 : numColors;
        for (i = 0; i < (WORD)maxCols; i++) {
            introPalette[i] = (UWORD)(((cRegs[i * 3 + 0] >> 28) << 8) |
                                      ((cRegs[i * 3 + 1] >> 28) << 4) |
                                      (cRegs[i * 3 + 2] >> 28));
        }
    } else {
        for (i = 0; i < 32; i++) introPalette[i] = palette[i];
    }
}


static WORD IntroEffectSin(WORD phase)
{
    static const WORD sinTable[32] = {
         0,  3,  6,  8, 11, 12, 13, 12,
        11,  8,  6,  3,  0, -3, -6, -8,
       -11,-12,-13,-12,-11, -8, -6, -3,
         0,  3,  6,  8, 11, 12, 13, 12
    };

    return sinTable[phase & 31];
}

static void DrawIntroTitleBobEffect(WORD dstX, WORD dstY, WORD effectTick)
{
    WORD progress;
    WORD sliceY;
    WORD centerY;
    WORD squeeze;
    WORD twist;

    if (!introTitleBM) return;

    if (effectTick < 0) effectTick = 0;
    if (effectTick > INTRO_EFFECT_FRAMES) effectTick = INTRO_EFFECT_FRAMES;

    progress = (effectTick * 64) / INTRO_EFFECT_FRAMES;
    centerY = introTitleH / 2;
    squeeze = (introTitleH * progress) / 160;
    twist = 2 + (progress / 3);

    for (sliceY = 0; sliceY < introTitleH; sliceY += INTRO_SLICE_H) {
        WORD h = INTRO_SLICE_H;
        WORD relY;
        WORD absRelY;
        WORD dstSliceY;
        WORD wave;
        WORD sliceScale;
        WORD margin;
        WORD srcX;
        WORD srcY;
        WORD srcW;
        WORD outX;

        if (sliceY + h > introTitleH) h = introTitleH - sliceY;

        relY = sliceY + (h / 2) - centerY;
        absRelY = relY < 0 ? -relY : relY;
        if (squeeze > 0 && absRelY > centerY - squeeze) continue;

        sliceScale = 64 - ((progress * absRelY) / (centerY ? centerY : 1));
        if (sliceScale < 24) sliceScale = 24;
        margin = (introTitleW * (64 - sliceScale)) / 128;
        srcX = margin;
        srcY = sliceY;
        srcW = introTitleW - (margin * 2);
        if (srcW <= 0) continue;

        wave = (IntroEffectSin((sliceY / INTRO_SLICE_H) + (effectTick / 2)) * twist) / 13;
        outX = dstX + margin + wave;
        dstSliceY = dstY + centerY + ((relY * (64 - (progress / 3))) / 64);

        if (dstSliceY < 0) {
            WORD clip = -dstSliceY;
            if (clip >= h) continue;
            srcY += clip;
            h -= clip;
            dstSliceY = 0;
        }
        if (dstSliceY + h > SCREEN_H) h = SCREEN_H - dstSliceY;
        if (h <= 0) continue;
        if (outX < 0) {
            WORD clip = -outX;
            if (clip >= srcW) continue;
            srcX += clip;
            srcW -= clip;
            outX = 0;
        }
        if (outX + srcW > SCREEN_W) srcW = SCREEN_W - outX;
        if (srcW <= 0) continue;

        BltBitMapRastPort(introTitleBM, srcX, srcY,
                          &renderRP, outX, dstSliceY,
                          srcW, h,
                          0xC0);
    }
}


static BOOL LoadIntroTitleImage(void)
{
    Object *dto;
    struct BitMapHeader *bmhd = NULL;
    struct BitMap *srcBM = NULL;
    ULONG *cRegs = NULL;
    ULONG numColors = 0;

    dto = NewDTObject("PROGDIR:tiles/robovac-title.iff",
                      DTA_GroupID, GID_PICTURE,
                      PDTA_Remap, FALSE,
                      TAG_DONE);
    if (!dto) return FALSE;

    if (!DoDTMethod(dto, NULL, NULL, DTM_PROCLAYOUT, 0L, TRUE)) {
        DisposeDTObject(dto);
        return FALSE;
    }

    if (!GetDTAttrs(dto,
                    PDTA_BitMapHeader, (ULONG)&bmhd,
                    PDTA_BitMap, (ULONG)&srcBM,
                    PDTA_CRegs, (ULONG)&cRegs,
                    PDTA_NumColors, (ULONG)&numColors,
                    TAG_DONE) || !bmhd || !srcBM) {
        DisposeDTObject(dto);
        return FALSE;
    }

    introTitleW = bmhd->bmh_Width;
    introTitleH = bmhd->bmh_Height;
    if (introTitleW <= 0 || introTitleH <= 0 || introTitleW > SCREEN_W || introTitleH > SCREEN_H) {
        DisposeDTObject(dto);
        introTitleW = 0;
        introTitleH = 0;
        return FALSE;
    }

    introTitleBM = AllocBitMap(introTitleW, introTitleH, DEPTH,
                               BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    if (!introTitleBM) {
        DisposeDTObject(dto);
        introTitleW = 0;
        introTitleH = 0;
        return FALSE;
    }

    BltBitMap(srcBM, 0, 0,
              introTitleBM, 0, 0,
              introTitleW, introTitleH,
              0xC0, 0xFF, NULL);

    InitRastPort(&introTitleRP);
    introTitleRP.BitMap = introTitleBM;

    CaptureIntroPalette(cRegs, numColors);

    DisposeDTObject(dto);
    return TRUE;
}

static void FreeIntroTitleImage(void)
{
    if (introTitleBM) {
        FreeBitMap(introTitleBM);
        introTitleBM = NULL;
    }
    introTitleRP.BitMap = NULL;
    introTitleW = 0;
    introTitleH = 0;
}

static BOOL LoadRobotSheetIntoCache(void)
{
    Object *dto = NULL;
    Object *dto2 = NULL;
    Object *dto3 = NULL;
    Object *dto4 = NULL;
    Object *dto5 = NULL;
    Object *dto6 = NULL;
    Object *dto7 = NULL;
    Object *boltDto = NULL;
    struct BitMapHeader *bmhd = NULL;
    struct BitMapHeader *boltBmhd = NULL;
    struct BitMap *srcBM = NULL;
    struct BitMap *boltSrcBM = NULL;
    ULONG *cRegs = NULL;
    LONG numCols = 0;
    LONG boltLayoutResult = 0;
    LONG i;
    struct RastPort srcRP;
    struct RastPort boltSrcRP;
    struct RastPort dstRP;
    struct RastPort maskRP;
    WORD boltDstX;

    dto = NewDTObject("PROGDIR:tiles/airobot1.iff",
                      DTA_GroupID, GID_PICTURE,
                      PDTA_Remap, FALSE,
                      TAG_DONE);
    dto2 = NewDTObject("PROGDIR:tiles/airobot2.iff",
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Remap, FALSE,
                       TAG_DONE);
    dto3 = NewDTObject("PROGDIR:tiles/airobot3.iff",
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Remap, FALSE,
                       TAG_DONE);
    dto4 = NewDTObject("PROGDIR:tiles/airobot4.iff",
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Remap, FALSE,
                       TAG_DONE);
    dto5 = NewDTObject("PROGDIR:tiles/airobot5.iff",
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Remap, FALSE,
                       TAG_DONE);
    dto6 = NewDTObject("PROGDIR:tiles/airobot6.iff",
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Remap, FALSE,
                       TAG_DONE);
    dto7 = NewDTObject("PROGDIR:tiles/airobot7.iff",
                       DTA_GroupID, GID_PICTURE,
                       PDTA_Remap, FALSE,
                       TAG_DONE);
    if (!dto || !dto2 || !dto3 || !dto4 || !dto5 || !dto6 || !dto7) {
        printf("LoadRobotSheetIntoCache: NewDTObject failed\n");
        if (dto) DisposeDTObject(dto);
        if (dto2) DisposeDTObject(dto2);
        if (dto3) DisposeDTObject(dto3);
        if (dto4) DisposeDTObject(dto4);
        if (dto5) DisposeDTObject(dto5);
        if (dto6) DisposeDTObject(dto6);
        if (dto7) DisposeDTObject(dto7);
        return FALSE;
    }

    DoDTMethod(dto, NULL, NULL, DTM_PROCLAYOUT, 0L, TRUE);
    GetDTAttrs(dto,
               PDTA_BitMapHeader, (ULONG)&bmhd,
               PDTA_BitMap, (ULONG)&srcBM,
               PDTA_CRegs, (ULONG)&cRegs,
               PDTA_NumColors, (ULONG)&numCols,
               TAG_DONE);

    if (!bmhd || !srcBM || bmhd->bmh_Width < ROBOT_W || bmhd->bmh_Height < ROBOT_H) {
        DisposeDTObject(dto);
        DisposeDTObject(dto2);
        DisposeDTObject(dto3);
        DisposeDTObject(dto4);
        DisposeDTObject(dto5);
        DisposeDTObject(dto6);
        DisposeDTObject(dto7);
        return FALSE;
    }

    InitRastPort(&srcRP);
    srcRP.BitMap = srcBM;

    InitRastPort(&dstRP);
    dstRP.BitMap = robotCacheBM;

    InitRastPort(&maskRP);
    maskRP.BitMap = robotMaskBM;

    BlitRobotVariant(dto, &dstRP, &maskRP, 0, 0);
    /* airobot2 source art faces down, so rotate 180 first to map to "up". */
    BlitRobotVariant(dto2, &dstRP, &maskRP, 1, 180);
    /* airobot3 source art faces up. */
    BlitRobotVariant(dto3, &dstRP, &maskRP, 2, 0);
    /* airobot4 source art faces up. */
    BlitRobotVariant(dto4, &dstRP, &maskRP, 3, 0);
    /* airobot5 source art faces up (90=right,180=down,270=left). */
    BlitRobotVariant(dto5, &dstRP, &maskRP, 4, 0);
    /* airobot6 source art faces down (90=left,180=up,270=right). */
    BlitRobotVariant(dto6, &dstRP, &maskRP, 5, 180);
    /* airobot7 source art faces down (90=left,180=up,270=right). */
    BlitRobotVariant(dto7, &dstRP, &maskRP, 6, 180);
    boltDstX = SPR_ENERGY_BOLT * ROBOT_W;

    boltDto = NewDTObject("PROGDIR:tiles/robotvac-tiles.iff",
                          DTA_GroupID, GID_PICTURE,
                          PDTA_Remap, FALSE,
                          TAG_DONE);
    if (boltDto) {
        boltLayoutResult = DoDTMethod(boltDto, NULL, NULL, DTM_PROCLAYOUT, 0L, TRUE);
        if (boltLayoutResult != 0) {
            GetDTAttrs(boltDto,
                       PDTA_BitMapHeader, (ULONG)&boltBmhd,
                       PDTA_BitMap, (ULONG)&boltSrcBM,
                       TAG_DONE);
            if (boltBmhd && boltSrcBM && boltBmhd->bmh_Width >= (8 * ROBOT_W) && boltBmhd->bmh_Height >= ROBOT_H) {
                InitRastPort(&boltSrcRP);
                boltSrcRP.BitMap = boltSrcBM;
                /* Clear destination frame first so old robot pixels don't remain under transparent bolt areas. */
                SetAPen(&dstRP, 0);
                RectFill(&dstRP, boltDstX, 0, boltDstX + ROBOT_W - 1, ROBOT_H - 1);
                SetAPen(&maskRP, 0);
                RectFill(&maskRP, boltDstX, 0, boltDstX + ROBOT_W - 1, ROBOT_H - 1);
                /* Override with bolt artwork from sprite 7 in robotvac-tiles.iff. */
                BlitRobotFrameRotated(&boltSrcRP, &dstRP, &maskRP, 7 * ROBOT_W, boltDstX, 0);
            }
        }
        DisposeDTObject(boltDto);
    }

    if (cRegs && numCols > 0) {
        LONG maxCols = (numCols > 16) ? 16 : numCols;
        for (i = 0; i < maxCols; i++) {
            palette[16 + i] = (UWORD)(((cRegs[i * 3 + 0] >> 28) << 8) |
                                      ((cRegs[i * 3 + 1] >> 28) << 4) |
                                       (cRegs[i * 3 + 2] >> 28));
        }
    }

    LoadPaletteCMap("PROGDIR:tiles/robopal2.pal", 16, 16);

    DisposeDTObject(dto);
    DisposeDTObject(dto2);
    DisposeDTObject(dto3);
    DisposeDTObject(dto4);
    DisposeDTObject(dto5);
    DisposeDTObject(dto6);
    DisposeDTObject(dto7);
    return TRUE;
}

static void FreeBoltCache(void)
{
    if (boltCacheBM) {
        FreeBitMap(boltCacheBM);
        boltCacheBM = NULL;
    }

    if (boltMaskBM) {
        FreeBitMap(boltMaskBM);
        boltMaskBM = NULL;
    }

    boltRP.BitMap = NULL;
}

static void CacheBoltPixel(struct RastPort *maskRP, WORD srcBaseX, WORD srcX, WORD srcY,
                           WORD dstBaseX, WORD dstX, WORD dstY)
{
    LONG pen = ReadPixel(&robotRP, srcBaseX + srcX, srcY);

    if (pen <= 0) return;
    if (pen > 31) pen = 31;

    SetAPen(&boltRP, (UBYTE)pen);
    WritePixel(&boltRP, dstBaseX + dstX, dstY);
    SetAPen(maskRP, 1);
    WritePixel(maskRP, dstBaseX + dstX, dstY);
}

static void BuildBoltCacheFrame(struct RastPort *maskRP, WORD frame, WORD rotation)
{
    WORD x;
    WORD y;
    WORD srcBaseX = SPR_ENERGY_BOLT * ROBOT_W;
    WORD dstBaseX = frame * ROBOT_W;

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            WORD dx = x;
            WORD dy = y;

            if (rotation == 90) {
                dx = ROBOT_W - 1 - y;
                dy = x;
            } else if (rotation == 180) {
                dx = ROBOT_W - 1 - x;
                dy = ROBOT_H - 1 - y;
            } else if (rotation == 270) {
                dx = y;
                dy = ROBOT_H - 1 - x;
            }

            CacheBoltPixel(maskRP, srcBaseX, x, y, dstBaseX, dx, dy);
        }
    }
}

static BOOL BuildBoltCache(void)
{
    struct RastPort maskRP;
    WORD cacheW = ROBOT_W * BOLT_FRAME_COUNT;

    if (!robotCacheBM) return FALSE;

    boltCacheBM = AllocBitMap(cacheW, ROBOT_H, DEPTH,
                              BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    boltMaskBM = AllocBitMap(cacheW, ROBOT_H, 1,
                             BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);

    if (!boltCacheBM || !boltMaskBM) {
        FreeBoltCache();
        return FALSE;
    }

    InitRastPort(&boltRP);
    boltRP.BitMap = boltCacheBM;
    InitRastPort(&maskRP);
    maskRP.BitMap = boltMaskBM;

    /* Match the old runtime path: vertical bolts used one rotated frame, horizontal bolts used the source frame. */
    BuildBoltCacheFrame(&maskRP, BOLT_FRAME_UP, 270);
    BuildBoltCacheFrame(&maskRP, BOLT_FRAME_DOWN, 270);
    BuildBoltCacheFrame(&maskRP, BOLT_FRAME_LEFT, 0);
    BuildBoltCacheFrame(&maskRP, BOLT_FRAME_RIGHT, 0);

    return TRUE;
}

static void FreeRobotScaledCache(void)
{
    if (robotScaledCacheBM) {
        FreeBitMap(robotScaledCacheBM);
        robotScaledCacheBM = NULL;
    }

    if (robotScaledMaskBM) {
        FreeBitMap(robotScaledMaskBM);
        robotScaledMaskBM = NULL;
    }

    robotScaledRP.BitMap = NULL;
}

static BOOL BuildRobotScaledCache(void)
{
    struct RastPort maskRP;
    WORD frame;
    WORD x;
    WORD y;
    WORD cacheW = ROBOT_SCALE2_W * SPR_STATE_COUNT * ROBOT_VARIANTS;

    if (!robotCacheBM || !robotMaskBM) return FALSE;

    robotScaledCacheBM = AllocBitMap(cacheW, ROBOT_SCALE2_H, DEPTH,
                                     BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    robotScaledMaskBM = AllocBitMap(cacheW, ROBOT_SCALE2_H, 1,
                                    BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);

    if (!robotScaledCacheBM || !robotScaledMaskBM) {
        FreeRobotScaledCache();
        return FALSE;
    }

    InitRastPort(&robotScaledRP);
    robotScaledRP.BitMap = robotScaledCacheBM;
    InitRastPort(&maskRP);
    maskRP.BitMap = robotScaledMaskBM;

    for (frame = 0; frame < (SPR_STATE_COUNT * ROBOT_VARIANTS); frame++) {
        WORD srcX = frame * ROBOT_W;
        WORD dstX = frame * ROBOT_SCALE2_W;

        for (y = 0; y < ROBOT_H; y++) {
            for (x = 0; x < ROBOT_W; x++) {
                LONG p = ReadPixel(&robotRP, srcX + x, y);
                WORD dx;
                WORD dy;

                if (p <= 0) continue;
                dx = dstX + (x * 2);
                dy = y * 2;

                SetAPen(&robotScaledRP, (UBYTE)p);
                WritePixel(&robotScaledRP, dx, dy);
                WritePixel(&robotScaledRP, dx + 1, dy);
                WritePixel(&robotScaledRP, dx, dy + 1);
                WritePixel(&robotScaledRP, dx + 1, dy + 1);

                SetAPen(&maskRP, 1);
                WritePixel(&maskRP, dx, dy);
                WritePixel(&maskRP, dx + 1, dy);
                WritePixel(&maskRP, dx, dy + 1);
                WritePixel(&maskRP, dx + 1, dy + 1);
            }
        }
    }

    return TRUE;
}


static void FreeTitleCarouselCache(void)
{
    if (titleCarouselBM) {
        FreeBitMap(titleCarouselBM);
        titleCarouselBM = NULL;
    }

    if (titleCarouselMaskBM) {
        FreeBitMap(titleCarouselMaskBM);
        titleCarouselMaskBM = NULL;
    }

    titleCarouselRP.BitMap = NULL;
    titleCarouselFrameCount = 0;
    {
        WORD phase;
        for (phase = 0; phase < TITLE_SPIN_STEPS; phase++) titleCarouselPhaseFrame[phase] = 0;
    }
}

static void FreeTitleStaticCache(void)
{
    if (titleStaticBM) {
        FreeBitMap(titleStaticBM);
        titleStaticBM = NULL;
    }

    titleStaticRP.BitMap = NULL;
    titleStaticDirty = TRUE;
    titlePanelDirty = TRUE;
    RequestTitleFullPresents();
}

static BOOL AllocTitleStaticCache(void)
{
    if (titleStaticBM) return TRUE;

    titleStaticBM = AllocBitMap(SCREEN_W, SCREEN_H, DEPTH,
                                BMF_CLEAR | BMF_DISPLAYABLE,
                                scr->RastPort.BitMap);
    if (!titleStaticBM) {
        if (!titleStaticCacheAllocFailedLogged) {
            printf("Could not allocate title static cache; direct title rendering fallback enabled\n");
            titleStaticCacheAllocFailedLogged = TRUE;
        }
        return FALSE;
    }

    titleStaticCacheAllocFailedLogged = FALSE;
    InitRastPort(&titleStaticRP);
    titleStaticRP.BitMap = titleStaticBM;
    titleStaticDirty = TRUE;
    titlePanelDirty = TRUE;
    RequestTitleFullPresents();
    return TRUE;
}

static BOOL AllocTitleCarouselCache(WORD frameCount)
{
    WORD cacheW = TITLE_ROT_W * frameCount * ROBOT_VARIANTS;

    titleCarouselBM = AllocBitMap(cacheW, TITLE_ROT_H, DEPTH,
                                  BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    titleCarouselMaskBM = AllocBitMap(cacheW, TITLE_ROT_H, 1,
                                      BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);

    if (!titleCarouselBM || !titleCarouselMaskBM) {
        FreeTitleCarouselCache();
        return FALSE;
    }

    InitRastPort(&titleCarouselRP);
    titleCarouselRP.BitMap = titleCarouselBM;
    titleCarouselFrameCount = frameCount;
    {
        WORD phase;
        for (phase = 0; phase < TITLE_SPIN_STEPS; phase++) {
            WORD frame = (phase * titleCarouselFrameCount) / TITLE_SPIN_STEPS;
            if (frame >= titleCarouselFrameCount) frame = titleCarouselFrameCount - 1;
            titleCarouselPhaseFrame[phase] = frame;
        }
    }
    return TRUE;
}

static WORD RoundedDiv(WORD value, WORD divisor)
{
    if (value >= 0) return (value + (divisor / 2)) / divisor;
    return -((-value + (divisor / 2)) / divisor);
}

static void BuildTitleCarouselRotationFrame(struct RastPort *maskRP, WORD variant, WORD frame)
{
    static const WORD sinTable[TITLE_SPIN_STEPS] = {
        0,12,24,36,45,53,59,63,64,63,59,53,45,36,24,12,
        0,-12,-24,-36,-45,-53,-59,-63,-64,-63,-59,-53,-45,-36,-24,-12
    };
    static const WORD cosTable[TITLE_SPIN_STEPS] = {
        64,63,59,53,45,36,24,12,0,-12,-24,-36,-45,-53,-59,-63,
        -64,-63,-59,-53,-45,-36,-24,-12,0,12,24,36,45,53,59,63
    };
    WORD phase = (frame * TITLE_SPIN_STEPS) / titleCarouselFrameCount;
    WORD sinv = sinTable[phase & (TITLE_SPIN_STEPS - 1)];
    WORD cosv = cosTable[phase & (TITLE_SPIN_STEPS - 1)];
    WORD srcBaseX = (variant * SPR_STATE_COUNT + SPR_READY) * ROBOT_W;
    WORD dstBaseX = (variant * titleCarouselFrameCount + frame) * TITLE_ROT_W;
    WORD dstCentre = (TITLE_ROT_W - 1) / 2;
    WORD srcCentre = ROBOT_W / 2;
    WORD px;
    WORD py;

    /*
     * Draw the cached spin with inverse mapping from each destination pixel back
     * to the nearest source pixel. The old forward mapping rotated source pixels
     * into the 2x destination and left occasional unmapped holes, visible as
     * missing dots during the carousel spin.
     */
    for (py = 0; py < TITLE_ROT_H; py++) {
        for (px = 0; px < TITLE_ROT_W; px++) {
            WORD rx = px - dstCentre;
            WORD ry = py - dstCentre;
            WORD srcScaledX = ((rx * cosv) + (ry * sinv)) / 64;
            WORD srcScaledY = ((ry * cosv) - (rx * sinv)) / 64;
            WORD sx = srcCentre + RoundedDiv(srcScaledX, TITLE_ROBOT_SCALE);
            WORD sy = srcCentre + RoundedDiv(srcScaledY, TITLE_ROBOT_SCALE);
            LONG pen;

            if (sx < 0 || sy < 0 || sx >= ROBOT_W || sy >= ROBOT_H) continue;

            pen = ReadPixel(&robotRP, srcBaseX + sx, sy);
            if (pen <= 0) continue;

            SetAPen(&titleCarouselRP, (UBYTE)pen);
            WritePixel(&titleCarouselRP, dstBaseX + px, py);
            SetAPen(maskRP, 1);
            WritePixel(maskRP, dstBaseX + px, py);
        }
    }
}

static BOOL BuildTitleCarouselRotationCache(void)
{
    struct RastPort maskRP;
    WORD variant;
    WORD frame;

    if (!robotCacheBM || !robotMaskBM) return FALSE;

    if (!AllocTitleCarouselCache(titleCarouselDesiredFrameCount)) {
        printf("Could not allocate %ld-frame title carousel rotation cache\n",
               (LONG)titleCarouselDesiredFrameCount);
        return FALSE;
    }

    printf("Using %ld title carousel rotation frames per robot (%s title FX)\n",
           (LONG)titleCarouselFrameCount, titleFxModeName);

    InitRastPort(&maskRP);
    maskRP.BitMap = titleCarouselMaskBM;

    for (variant = 0; variant < ROBOT_VARIANTS; variant++) {
        for (frame = 0; frame < titleCarouselFrameCount; frame++) {
            BuildTitleCarouselRotationFrame(&maskRP, variant, frame);
        }
    }

    return TRUE;
}

static BOOL InitRobotBobs(void)
{

    robotCacheBM = AllocBitMap(ROBOT_W * SPR_STATE_COUNT * ROBOT_VARIANTS, ROBOT_H, DEPTH,
                               BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    robotMaskBM = AllocBitMap(ROBOT_W * SPR_STATE_COUNT * ROBOT_VARIANTS, ROBOT_H, 1,
                              BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);

    if (!robotCacheBM || !robotMaskBM) {
        printf("Could not allocate robot BOB cache/mask\n");
        if (robotCacheBM) {
            FreeBitMap(robotCacheBM);
            robotCacheBM = NULL;
        }
        if (robotMaskBM) {
            FreeBitMap(robotMaskBM);
            robotMaskBM = NULL;
        }
        robotRP.BitMap = NULL;
        return FALSE;
    }

    InitRastPort(&robotRP);
    robotRP.BitMap = robotCacheBM;

    if (!LoadRobotSheetIntoCache()) {
        printf("Could not load PROGDIR:tiles/airobot1.iff through airobot7.iff (need at least 16x16, 16 colours)\n");
        FreeBoltCache();
        FreeRobotScaledCache();
        FreeBitMap(robotCacheBM);
        FreeBitMap(robotMaskBM);
        robotCacheBM = NULL;
        robotMaskBM = NULL;
        robotRP.BitMap = NULL;
        return FALSE;
    }

    if (!BuildBoltCache()) {
        printf("Could not allocate bolt sprite cache; bolts will use slower fallback drawing\n");
    }

    if (!BuildRobotScaledCache()) {
        printf("Could not allocate scaled robot BOB cache; quad robot will use slower fallback drawing\n");
    }

    if (!BuildTitleCarouselRotationCache()) {
        FreeBoltCache();
        FreeRobotScaledCache();
        FreeBitMap(robotCacheBM);
        FreeBitMap(robotMaskBM);
        robotCacheBM = NULL;
        robotMaskBM = NULL;
        robotRP.BitMap = NULL;
        return FALSE;
    }

    LoadGamePalette();

    return TRUE;
}

static void PlotRobotPixel(WORD x, WORD y, UBYTE pen)
{
    if (x < 0 || y < HUD_H || x >= SCREEN_W || y >= SCREEN_H) return;
    SetAPen(&renderRP, pen);
    WritePixel(&renderRP, x, y);
}

static void DrawRobotBobScaled2Cpu(WORD srcX, WORD sx, WORD sy)
{
    WORD x;
    WORD y;
    WORD drawX = sx - (ROBOT_W / 2);
    WORD drawY = sy - (ROBOT_H / 2);

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            LONG p = ReadPixel(&robotRP, srcX + x, y);
            WORD dx;
            WORD dy;

            if (p <= 0) continue;
            dx = drawX + (x * 2);
            dy = drawY + (y * 2);
            PlotRobotPixel(dx, dy, (UBYTE)p);
            PlotRobotPixel(dx + 1, dy, (UBYTE)p);
            PlotRobotPixel(dx, dy + 1, (UBYTE)p);
            PlotRobotPixel(dx + 1, dy + 1, (UBYTE)p);
        }
    }
}

static void DrawRobotBobScaled2(WORD srcX, WORD sx, WORD sy)
{
    WORD scaledSrcX;

    if (robotScaledCacheBM && robotScaledMaskBM && robotScaledMaskBM->Planes[0]) {
        scaledSrcX = (srcX / ROBOT_W) * ROBOT_SCALE2_W;
        BltMaskBitMapRastPort(robotScaledCacheBM, scaledSrcX, 0,
                              &renderRP, sx - (ROBOT_W / 2), sy - (ROBOT_H / 2),
                              ROBOT_SCALE2_W, ROBOT_SCALE2_H,
                              (ABC | ABNC | ANBC),
                              robotScaledMaskBM->Planes[0]);
        return;
    }

    DrawRobotBobScaled2Cpu(srcX, sx, sy);
}

static void DrawRobotBobTurn45(WORD srcX, WORD sx, WORD sy, WORD turnDirection)
{
    WORD x;
    WORD y;

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            WORD dx = x - (ROBOT_W / 2);
            WORD dy = y - (ROBOT_H / 2);
            WORD sampleX;
            WORD sampleY;
            LONG p;

            if (turnDirection >= 0) {
                sampleX = (WORD)((((LONG)dx + (LONG)dy) * 181L) >> 8) + (ROBOT_W / 2);
                sampleY = (WORD)((((LONG)dy - (LONG)dx) * 181L) >> 8) + (ROBOT_H / 2);
            } else {
                sampleX = (WORD)((((LONG)dx - (LONG)dy) * 181L) >> 8) + (ROBOT_W / 2);
                sampleY = (WORD)((((LONG)dx + (LONG)dy) * 181L) >> 8) + (ROBOT_H / 2);
            }

            if (sampleX < 0 || sampleY < 0 || sampleX >= ROBOT_W || sampleY >= ROBOT_H) continue;
            p = ReadPixel(&robotRP, srcX + sampleX, sampleY);
            if (p <= 0) continue;
            PlotRobotPixel(sx + x, sy + y, (UBYTE)p);
        }
    }
}

static void DrawRobotBob(WORD id)
{
    WORD sx;
    WORD sy;
    WORD srcX;

    if (id < 0 || id >= robotCount) return;

    sx = MAP_X + FP_TO_INT(robots[id].px);
    sy = MAP_Y + FP_TO_INT(robots[id].py);
    srcX = (robots[id].spriteVariant * SPR_STATE_COUNT + robots[id].spriteIndex) * ROBOT_W;

    SetAPen(&renderRP, 6);
    if (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) {
        RectFill(&renderRP, sx, sy + 21, sx + 23, sy + 24);
    } else {
        RectFill(&renderRP, sx + 4, sy + 13, sx + 12, sy + 14);
    }

    if (robotCacheBM && robotMaskBM && robotMaskBM->Planes[0]) {
        if (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) {
            DrawRobotBobScaled2(srcX, sx, sy);
        } else if (robots[id].turnTicks > 0 && robots[id].prevSpriteIndex != robots[id].spriteIndex) {
            WORD turnSrcX = (robots[id].spriteVariant * SPR_STATE_COUNT + robots[id].prevSpriteIndex) * ROBOT_W;
            DrawRobotBobTurn45(turnSrcX, sx, sy, robots[id].turnDirection);
        } else {
            BltMaskBitMapRastPort(robotCacheBM, srcX, 0,
                                  &renderRP, sx, sy,
                                  ROBOT_W, ROBOT_H,
                                  (ABC | ABNC | ANBC),
                                  robotMaskBM->Planes[0]);
        }
    } else {
        SetAPen(&renderRP, 16 + 1);
        RectFill(&renderRP, sx + 2, sy + 2, sx + 13, sy + 13);
    }

    if (robots[id].stunTicks > 0) {
        UBYTE warningPen = EmpWarningPen();
        SetAPen(&renderRP, warningPen);
        RectFill(&renderRP, sx + 5, sy + 4, sx + 6, sy + 5);
        RectFill(&renderRP, sx + 10, sy + 4, sx + 11, sy + 5);
    }
}

static WORD BoltFrameForDirection(struct Bolt *bolt)
{
    if (bolt->dirY < 0) return BOLT_FRAME_UP;
    if (bolt->dirY > 0) return BOLT_FRAME_DOWN;
    if (bolt->dirX < 0) return BOLT_FRAME_LEFT;
    return BOLT_FRAME_RIGHT;
}

static void DrawBoltSprite(struct Bolt *bolt)
{
    WORD sx;
    WORD sy;
    WORD frame;
    WORD srcX;

    if (!bolt || !bolt->active) return;

    sx = MAP_X + FP_TO_INT(bolt->px);
    sy = MAP_Y + FP_TO_INT(bolt->py);

    if (boltCacheBM && boltMaskBM && boltMaskBM->Planes[0]) {
        frame = BoltFrameForDirection(bolt);
        srcX = frame * ROBOT_W;
        BltMaskBitMapRastPort(boltCacheBM, srcX, 0,
                              &renderRP, sx, sy,
                              ROBOT_W, ROBOT_H,
                              (ABC | ABNC | ANBC),
                              boltMaskBM->Planes[0]);
        return;
    }

    if (robotCacheBM && robotMaskBM && robotMaskBM->Planes[0]) {
        BltMaskBitMapRastPort(robotCacheBM, SPR_ENERGY_BOLT * ROBOT_W, 0,
                              &renderRP, sx, sy,
                              ROBOT_W, ROBOT_H,
                              (ABC | ABNC | ANBC),
                              robotMaskBM->Planes[0]);
    }
}

static void DrawPlayerBolt(WORD playerId)
{
    if (playerId < 0 || playerId >= robotCount) return;
    DrawBoltSprite(&playerBolts[playerId]);
}

static void DrawBossBolts(void)
{
    WORD i;
    for (i = 0; i < MAX_BOSS_BOLTS; i++) {
        DrawBoltSprite(&bossBolts[i]);
    }
}

static WORD EmpRobotCountdownNumber(WORD id)
{
    WORD secondsLeft;

    if (id < 0 || id >= robotCount) return 0;
    if (robots[id].stunTicks <= 0) return 0;

    secondsLeft = ((robots[id].stunTicks - 1) / BOLT_STUN_STEP_FRAMES) + 1;
    if (secondsLeft < 1) secondsLeft = 1;
    if (secondsLeft > 5) secondsLeft = 5;
    return secondsLeft;
}

static BOOL RobotLowBatteryWarningActive(WORD id)
{
    if (id < 0 || id >= robotCount) return FALSE;
    if (robots[id].battery > 25) return FALSE;
    /* Sitting on the dock already means it's recharging, so the reminder to
     * go dock would be pointless there. */
    if (map[robots[id].tileY][robots[id].tileX] == TILE_DOCK) return FALSE;
    return TRUE;
}

/* Shares the EMP stun countdown's above-head box and dirty-rect tracking:
 * 1..5 = seconds left stunned (drawn as a number), -1 = battery low (drawn
 * as a warning icon), 0 = nothing to show. Stun takes priority since it is
 * the more urgent state and the two never need to show at once. */
static WORD EmpRobotVisualState(WORD id)
{
    WORD countdown = EmpRobotCountdownNumber(id);

    if (countdown > 0) return countdown;
    if (RobotLowBatteryWarningActive(id)) return -1;
    return 0;
}

static void GetEmpRobotVisualRectFromScreen(WORD sx, WORD sy, struct DirtyRect *rect)
{
    WORD left;
    WORD top;
    WORD width;
    WORD height;

    if (!rect) return;

    left = sx + ((ROBOT_W - EMP_ROBOT_VISUAL_W) / 2);
    top = sy - EMP_ROBOT_VISUAL_H - 2;
    width = EMP_ROBOT_VISUAL_W;
    height = EMP_ROBOT_VISUAL_H;

    if (top < 0) top = 0;
    if (left < 0) {
        width += left;
        left = 0;
    }
    if (left >= SCREEN_W || top >= SCREEN_H || width <= 0 || height <= 0) {
        rect->x = 0;
        rect->y = 0;
        rect->w = 0;
        rect->h = 0;
        return;
    }
    if (left + width > SCREEN_W) width = SCREEN_W - left;
    if (top + height > SCREEN_H) height = SCREEN_H - top;

    rect->x = left;
    rect->y = top;
    rect->w = width;
    rect->h = height;
}

static BOOL GetEmpRobotVisualRect(WORD id, struct DirtyRect *rect)
{
    WORD sx;
    WORD sy;

    if (!rect || id < 0 || id >= robotCount) return FALSE;
    if (EmpRobotVisualState(id) == 0) return FALSE;

    sx = MAP_X + FP_TO_INT(robots[id].px);
    sy = MAP_Y + FP_TO_INT(robots[id].py);
    GetEmpRobotVisualRectFromScreen(sx, sy, rect);
    return rect->w > 0 && rect->h > 0;
}

static void DrawEmpRobotVisual(WORD id)
{
    struct DirtyRect rect;
    WORD state;
    WORD pen;
    WORD textX;
    WORD textY;
    char b[4];

    state = EmpRobotVisualState(id);
    if (state == 0) return;
    if (!GetEmpRobotVisualRect(id, &rect)) return;

    if (state < 0) {
        /* Low battery: a plain "!" above the head, since there is no stun
         * countdown to show and the reminder just needs to catch the eye. */
        b[0] = '!';
        b[1] = '\0';
        pen = 14;
    } else {
        snprintf(b, sizeof(b), "%d", state);
        pen = (robots[id].stunTicks & 4) ? 14 : 10;
    }
    /* Pens 10/14 already stay lit in night mode's protected cluster, but
     * pin the text to pure white there instead of their usual accent
     * colours so it reads clearly as urgent against the dimmed room. */
    if (nightModeActive) pen = 7;

    textX = rect.x + ((rect.w - MiniTextWidth(b, 1)) / 2);
    textY = rect.y + 1;

    MiniText(&renderRP, textX + 1, textY + 1, b, 1);
    MiniText(&renderRP, textX, textY, b, pen);
}

static void DrawEmpRobotVisuals(void)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        DrawEmpRobotVisual(i);
    }
}

/* -------------------------------------------------------------------------
 * Gameplay
 * ------------------------------------------------------------------------- */

static void SetRobotTile(WORD id, WORD tx, WORD ty)
{
    robots[id].tileX = tx;
    robots[id].tileY = ty;
    robots[id].targetX = tx;
    robots[id].targetY = ty;
    robots[id].px = TO_FP(tx * TILE_SIZE);
    robots[id].py = TO_FP(ty * TILE_SIZE);
    robots[id].targetPx = robots[id].px;
    robots[id].targetPy = robots[id].py;
    robots[id].moving = FALSE;
    robots[id].spriteIndex = SPR_READY;
    robots[id].prevSpriteIndex = SPR_READY;
    robots[id].turnTicks = 0;
    robots[id].turnDirection = 1;
    robots[id].spriteVariant = 0;
}

static void InitRobots(void)
{
    WORD i;

    robotCount = humanPlayers + aiRivals;
    /* Demo/attract mode deliberately runs with zero human players so every
     * robot is AI-controlled; every other caller still needs at least 1. */
    if (humanPlayers < 1 && !demoModeActive) humanPlayers = 1;
    if (humanPlayers < 0) humanPlayers = 0;
    if (humanPlayers > MAX_HUMAN_PLAYERS) humanPlayers = MAX_HUMAN_PLAYERS;
    if (robotCount < humanPlayers) robotCount = humanPlayers;
    if (robotCount > MAX_ROBOTS) robotCount = MAX_ROBOTS;

    for (i = 0; i < robotCount; i++) {
        SetRobotTile(i, RobotStartX(i), RobotStartY(i));
        aiPrevTileX[i] = RobotStartX(i);
        aiPrevTileY[i] = RobotStartY(i);
        {
            WORD h;
            for (h = 0; h < AI_RECENT_TILE_COUNT; h++) {
                aiRecentTileX[i][h] = RobotStartX(i);
                aiRecentTileY[i][h] = RobotStartY(i);
            }
        }
        aiTargetDirtX[i] = -1;
        aiTargetDirtY[i] = -1;
        robots[i].battery = maxBattery;
        robots[i].score = 0;
        robots[i].stunTicks = 0;
        robots[i].boltImmuneTicks = 0;
        robots[i].boltStunned = FALSE;
        robots[i].emergencyMovesLeft = EMERGENCY_DOCK_MOVES;
        robots[i].chargeTicks = 0;
        robots[i].cleanStreak = 0;
        robots[i].powerCleanTarget = POWERUP_CLEAN_TARGET;
        robots[i].powerUseCount = 0;
        robots[i].powerMovesLeft = 0;
        robots[i].powerType = POWER_NONE;
        robots[i].ai = (i >= humanPlayers) ? TRUE : FALSE;
        robots[i].spriteIndex = SPR_READY;
        robots[i].prevSpriteIndex = SPR_READY;
        robots[i].turnTicks = 0;
        robots[i].turnDirection = 1;
        if (i < humanPlayers) {
            robots[i].spriteVariant = (UBYTE)selectedPlayerVariant[i];
        } else {
            robots[i].spriteVariant = (UBYTE)((selectedPlayerVariant[0] + i) % ROBOT_VARIANTS);
        }
    }

    moves = 0;
}



static WORD CleanTileForRobot(WORD id, WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
    if (map[ty][tx] != TILE_DIRT) return 0;

    map[ty][tx] = TILE_FLOOR;
    RemoveDirtListTile(tx, ty);
    robots[id].score++;
    UpdateRoomTile(tx, ty);
    return 1;
}

static void CleanQuadArea(WORD id)
{
    WORD ox;
    WORD oy;
    WORD cleaned = 0;

    for (oy = -POWERUP_QUAD_RADIUS; oy <= POWERUP_QUAD_RADIUS; oy++) {
        for (ox = -POWERUP_QUAD_RADIUS; ox <= POWERUP_QUAD_RADIUS; ox++) {
            cleaned += CleanTileForRobot(id, robots[id].tileX + ox, robots[id].tileY + oy);
        }
    }

    if (cleaned > 0) {
        snprintf(lastPowerText, sizeof(lastPowerText), "%s QUAD CLEAN +%d", RobotTag(id), cleaned);
        lastPowerTicks = 120;
    }
}

static WORD NextPowerCleanTarget(WORD useCount)
{
    if (useCount <= 0) return POWERUP_CLEAN_TARGET;
    if (useCount == 1) return POWERUP_CLEAN_TARGET_2;
    return POWERUP_CLEAN_TARGET_3;
}

static WORD SpriteDirectionIndex(UBYTE spriteIndex)
{
    if (spriteIndex == SPR_UP) return 0;
    if (spriteIndex == SPR_RIGHT) return 1;
    if (spriteIndex == SPR_DOWN) return 2;
    if (spriteIndex == SPR_LEFT) return 3;
    return -1;
}

static void SetRobotMoveSprite(WORD id, UBYTE newSpriteIndex)
{
    WORD oldDir;
    WORD newDir;
    WORD delta;

    if (id < 0 || id >= robotCount) return;

    oldDir = SpriteDirectionIndex(robots[id].spriteIndex);
    newDir = SpriteDirectionIndex(newSpriteIndex);
    robots[id].prevSpriteIndex = robots[id].spriteIndex;
    robots[id].spriteIndex = newSpriteIndex;

    if (oldDir >= 0 && newDir >= 0 && oldDir != newDir) {
        delta = (newDir - oldDir + 4) & 3;
        robots[id].turnDirection = (delta == 3) ? -1 : 1;
        robots[id].turnTicks = ROBOT_TURN_TICKS;
    } else {
        robots[id].turnTicks = 0;
        robots[id].turnDirection = 1;
    }
}

static BOOL ApplyBoltStun(WORD id, WORD damage)
{
    if (id < 0 || id >= robotCount) return FALSE;
    if (robots[id].stunTicks > 0 || robots[id].boltImmuneTicks > 0) return FALSE;

    robots[id].stunTicks = BOLT_STUN_TICKS;
    robots[id].boltStunned = TRUE;
    robots[id].boltImmuneTicks = 0;
    robots[id].battery -= damage;
    if (robots[id].battery < 0) robots[id].battery = 0;
    return TRUE;
}

static void TriggerRobotPower(WORD id)
{
    WORD i;
    UBYTE variant;

    if (id < 0 || id >= robotCount) return;
    variant = robots[id].spriteVariant;
    if (variant >= ROBOT_VARIANTS) variant = 0;

    robots[id].powerType = (UBYTE)(variant + 1);
    robots[id].powerMovesLeft = POWERUP_DURATION_MOVES;
    snprintf(lastPowerText, sizeof(lastPowerText), "%s: %s!", RobotTag(id), powerNames[variant]);
    lastPowerTicks = 180;

    if (robots[id].powerType == POWER_EMP || robots[id].powerType == POWER_DIRT_DROP) {
        robots[id].powerUseCount++;
        robots[id].powerCleanTarget = NextPowerCleanTarget(robots[id].powerUseCount);
    }

    if (robots[id].powerType == POWER_BOLT) {
        robots[id].powerMovesLeft = POWERUP_BOLT_MOVES;
    } else if (robots[id].powerType == POWER_EMP) {
        empCountdownTicks = POWERUP_EMP_TICKS;
        empCountdownOwner = id;
        for (i = 0; i < robotCount; i++) {
            if (i != id) {
                robots[i].stunTicks = POWERUP_EMP_TICKS;
                robots[i].boltStunned = FALSE;
                robots[i].boltImmuneTicks = 0;
                if (robots[i].moving) {
                    robots[i].tileX = robots[i].targetX;
                    robots[i].tileY = robots[i].targetY;
                    robots[i].px = TO_FP(robots[i].tileX * TILE_SIZE);
                    robots[i].py = TO_FP(robots[i].tileY * TILE_SIZE);
                    robots[i].targetPx = robots[i].px;
                    robots[i].targetPy = robots[i].py;
                    robots[i].moving = FALSE;
                }
                CleanTileForRobot(i, robots[i].tileX, robots[i].tileY);
            }
        }
        CountDirt();
        UpdateEmpPaletteCycle();
        ForceGameplayFullPresent();
        robots[id].powerMovesLeft = 0;
        robots[id].powerType = POWER_NONE;
    } else if (robots[id].powerType == POWER_DIRT_DROP) {
        WORD dropped = SpawnDirtTiles(POWERUP_DIRT_DROP);
        snprintf(lastPowerText, sizeof(lastPowerText), "%s DIRT BOMB +%d NEXT %d", RobotTag(id), dropped, robots[id].powerCleanTarget);
        robots[id].powerMovesLeft = 0;
        robots[id].powerType = POWER_NONE;
    } else if (robots[id].powerType == POWER_BATTERY_BURST) {
        robots[id].battery = maxBattery;
        robots[id].emergencyMovesLeft = EMERGENCY_DOCK_MOVES;
    } else if (robots[id].powerType == POWER_QUAD) {
        CleanQuadArea(id);
    }
}

static BOOL IsRobotDock(WORD id, WORD tx, WORD ty)
{
    if (id < 0 || id >= MAX_ROBOTS) return FALSE;
    return (tx == RobotDockX(id) && ty == RobotDockY(id)) ? TRUE : FALSE;
}

static BOOL ValidDirtTile(WORD tx, WORD ty)
{
    WORD i;
    if (map[ty][tx] != TILE_FLOOR) return FALSE;
    for (i = 0; i < robotCount; i++) {
        if (tx == RobotDockX(i) && ty == RobotDockY(i)) return FALSE;
    }
    return TRUE;
}

static WORD SpawnDirtTiles(WORD count)
{
    WORD placed = 0, tries = 0;
    while (placed < count && tries < 2000) {
        WORD x = 1 + RandRange(MAP_W - 2);
        WORD y = 1 + RandRange(MAP_H - 2);
        if (ValidDirtTile(x, y) && !RobotAtTile(x, y, -1)) {
            map[y][x] = TILE_DIRT;
            AddDirtListTile(x, y);
            UpdateRoomTile(x, y);
            placed++;
        }
        tries++;
    }
    if (placed > 0) EnsureDirtListValid();
    return placed;
}

static void SpawnRoundDirt(WORD count)
{
    WORD placed = 0, tries = 0;
    while (placed < count && tries < 2000) {
        WORD x = 1 + RandRange(MAP_W - 2);
        WORD y = 1 + RandRange(MAP_H - 2);
        if (ValidDirtTile(x, y)) {
            map[y][x] = TILE_DIRT;
            AddDirtListTile(x, y);
            placed++;
        }
        tries++;
    }
}

static void ResetLevel(void)
{
    WORD x, y;
    const char **layout;

    StopMenuMusic();
    StopGameplaySamples();
    StopRoundStartSamples();

    roomType = RandRange(5);
    MarkHudStatusTextDirty();
    layout = roomLayouts[roomType];

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            char c = layout[y][x];
            switch (c) {
                case '#': map[y][x] = TILE_WALL; break;
                case 'D': map[y][x] = TILE_DOCK; break;
                case 'T': map[y][x] = TILE_TABLE; break;
                default:  map[y][x] = TILE_FLOOR; break;
            }
        }
    }

    map[1][1] = TILE_DOCK;
    map[1][18] = TILE_DOCK;
    map[11][1] = TILE_DOCK;
    map[11][18] = TILE_DOCK;

    ClearDirtList();
    SpawnRoundDirt(RoundDirtTarget(roundIndex));
    ClearMovementKeys();
    { WORD bi; for (bi = 0; bi < MAX_ROBOTS; bi++) playerBolts[bi].active = FALSE; }
    { WORD bi; for (bi = 0; bi < MAX_BOSS_BOLTS; bi++) bossBolts[bi].active = FALSE; }
    lastPowerText[0] = '\0';
    lastPowerTicks = 0;
    bonusBossExplosionTicks = 0;
    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundCountdownSoundNumber = ROUND_COUNTDOWN_SECONDS;
    roundCountdownLastSoundNumber = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    empCountdownTicks = 0;
    empCountdownOwner = -1;
    gameState = GAME_PLAYING;
    ClosePauseMenu();

    InitRobots();
    CountDirt();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
    roundCountdownLastSoundNumber = 0;
}

static BOOL StartRobotMove(WORD id, WORD dx, WORD dy)
{
    WORD nx;
    WORD ny;
    WORD dockDistNow;
    WORD dockDistNext;

    if (id < 0 || id >= robotCount) return FALSE;
    if (robots[id].moving) return FALSE;
    if (robots[id].battery < batteryCostPerMove) {
        if (robots[id].battery > 0) return FALSE;
        if (robots[id].emergencyMovesLeft <= 0) return FALSE;
    }

    nx = robots[id].tileX + dx;
    ny = robots[id].tileY + dy;

    if (!RobotCanPassTile(id, nx, ny)) return FALSE;
    if (RobotAtTile(nx, ny, id)) {
        /* During the bonus boss fight, walking into a teammate who is out of
         * charge shoves them one tile further along instead of just
         * blocking, the same way the boss shoves a grazed robot, so a
         * stranded robot can be nudged back toward a dock instead of
         * getting stuck. */
        if (gameState != GAME_BONUS_PLAYING ||
            !TryPushStrandedRobot(RobotIdAtTile(nx, ny, id), dx, dy)) {
            return FALSE;
        }
    }

    if (robots[id].powerType == POWER_WALL_SMASH && robots[id].powerMovesLeft > 0 && map[ny][nx] == TILE_WALL) {
        map[ny][nx] = TILE_FLOOR;
        UpdateRoomTile(nx, ny);
    }

    if (robots[id].battery < batteryCostPerMove) {
        dockDistNow = AbsW(robots[id].tileX - RobotDockX(id)) + AbsW(robots[id].tileY - RobotDockY(id));
        dockDistNext = AbsW(nx - RobotDockX(id)) + AbsW(ny - RobotDockY(id));
        if (dockDistNext >= dockDistNow) return FALSE;
    }

    robots[id].targetX = nx;
    robots[id].targetY = ny;
    robots[id].targetPx = TO_FP(nx * TILE_SIZE);
    robots[id].targetPy = TO_FP(ny * TILE_SIZE);
    robots[id].moving = TRUE;
    if (dx < 0) SetRobotMoveSprite(id, SPR_LEFT);
    else if (dx > 0) SetRobotMoveSprite(id, SPR_RIGHT);
    else if (dy < 0) SetRobotMoveSprite(id, SPR_UP);
    else if (dy > 0) SetRobotMoveSprite(id, SPR_DOWN);
    if (id >= 0 && id < humanPlayers) {
        playerFacingX[id] = dx;
        playerFacingY[id] = dy;
    }
    if (robots[id].battery >= batteryCostPerMove) {
        robots[id].battery -= batteryCostPerMove;
    } else {
        robots[id].battery = 0;
        robots[id].emergencyMovesLeft--;
    }

    if (robots[id].powerMovesLeft > 0) {
        robots[id].powerMovesLeft--;
        if (robots[id].powerMovesLeft <= 0) robots[id].powerType = POWER_NONE;
    }

    if (id >= 0 && id < humanPlayers) {
        moves++;
    }

    return TRUE;
}

static void FinishRobotTileMove(WORD id)
{
    WORD tx;
    WORD ty;

    tx = robots[id].targetX;
    ty = robots[id].targetY;

    if (robots[id].ai) {
        WORD h;
        for (h = AI_RECENT_TILE_COUNT - 1; h > 0; h--) {
            aiRecentTileX[id][h] = aiRecentTileX[id][h - 1];
            aiRecentTileY[id][h] = aiRecentTileY[id][h - 1];
        }
        aiRecentTileX[id][0] = robots[id].tileX;
        aiRecentTileY[id][0] = robots[id].tileY;
        aiPrevTileX[id] = robots[id].tileX;
        aiPrevTileY[id] = robots[id].tileY;
    }

    robots[id].tileX = tx;
    robots[id].tileY = ty;
    robots[id].px = robots[id].targetPx;
    robots[id].py = robots[id].targetPy;
    robots[id].moving = FALSE;

    if ((robots[id].powerType != POWER_QUAD || robots[id].powerMovesLeft <= 0) &&
        (map[ty][tx] == TILE_WALL || map[ty][tx] == TILE_TABLE)) {
        MoveRobotToNearestFreeTile(id);
        tx = robots[id].tileX;
        ty = robots[id].tileY;
    }

    if (CleanTileForRobot(id, tx, ty)) {
        robots[id].cleanStreak++;
        if (robots[id].cleanStreak >= robots[id].powerCleanTarget) {
            robots[id].cleanStreak = 0;
            TriggerRobotPower(id);
        }
    }

    if (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) {
        CleanQuadArea(id);
    }

    if (map[ty][tx] == TILE_DOCK) {
        if (robots[id].battery <= 0) {
            robots[id].chargeTicks = DOCK_CHARGE_TICKS;
        } else {
            robots[id].battery = maxBattery;
            robots[id].emergencyMovesLeft = EMERGENCY_DOCK_MOVES;
            robots[id].chargeTicks = 0;
        }
        if (id >= 0 && id < humanPlayers) robots[id].spriteIndex = SPR_CHARGING;
    }
}

static void StepRobotMovement(WORD id)
{
    LONG targetPx;
    LONG targetPy;
    LONG dx;
    LONG dy;

    if (!robots[id].moving) return;

    targetPx = robots[id].targetPx;
    targetPy = robots[id].targetPy;

    dx = targetPx - robots[id].px;
    dy = targetPy - robots[id].py;

    {
        LONG stepSpeed = (robots[id].battery <= 0) ? EMERGENCY_MOVE_SPEED : MOVE_SPEED;
        if (robots[id].powerType == POWER_DOUBLE_SPEED && robots[id].powerMovesLeft > 0 && robots[id].battery > 0) {
            stepSpeed = DOUBLE_SPEED_MOVE_SPEED;
        }

    if (dx > 0) {
        robots[id].px += (dx < stepSpeed) ? dx : stepSpeed;
    } else if (dx < 0) {
        robots[id].px -= ((-dx) < stepSpeed) ? (-dx) : stepSpeed;
    }

    if (dy > 0) {
        robots[id].py += (dy < stepSpeed) ? dy : stepSpeed;
    } else if (dy < 0) {
        robots[id].py -= ((-dy) < stepSpeed) ? (-dy) : stepSpeed;
    }
    }

    if (robots[id].px == targetPx && robots[id].py == targetPy) {
        FinishRobotTileMove(id);
    }
}

static void ShuffleAiDirs(WORD dirs[4])
{
    WORD i;

    for (i = 0; i < 4; i++) dirs[i] = i;
    for (i = 3; i > 0; i--) {
        WORD j = (WORD)RandRange((UWORD)(i + 1));
        WORD t = dirs[i];
        dirs[i] = dirs[j];
        dirs[j] = t;
    }
}

static WORD AiRecentVisitPenalty(WORD id, WORD tileX, WORD tileY)
{
    WORD h;
    WORD penalty = 0;

    if (id < humanPlayers || id >= robotCount) return 0;
    for (h = 0; h < AI_RECENT_TILE_COUNT; h++) {
        if (aiRecentTileX[id][h] == tileX && aiRecentTileY[id][h] == tileY) {
            penalty += (WORD)(AI_RECENT_TILE_COUNT - h) * 3;
        }
    }

    return penalty;
}

static void OrderAiPathDirs(WORD id, WORD curX, WORD curY, WORD targetX, WORD targetY, WORD dirs[4])
{
    WORD i;
    WORD j;
    static const WORD dirX[4] = {1, 0, -1, 0};   /* right, down, left, up */
    static const WORD dirY[4] = {0, 1, 0, -1};

    for (i = 0; i < 4; i++) dirs[i] = i;

    for (i = 0; i < 3; i++) {
        for (j = i + 1; j < 4; j++) {
            WORD ia = dirs[i];
            WORD ib = dirs[j];
            WORD ax = curX + dirX[ia];
            WORD ay = curY + dirY[ia];
            WORD bx = curX + dirX[ib];
            WORD by = curY + dirY[ib];
            WORD scoreA = AbsW(targetX - ax) + AbsW(targetY - ay);
            WORD scoreB = AbsW(targetX - bx) + AbsW(targetY - by);

            if (ax == aiPrevTileX[id] && ay == aiPrevTileY[id]) scoreA += 8;
            if (bx == aiPrevTileX[id] && by == aiPrevTileY[id]) scoreB += 8;
            scoreA += AiRecentVisitPenalty(id, ax, ay);
            scoreB += AiRecentVisitPenalty(id, bx, by);

            if (scoreB < scoreA) {
                WORD t = dirs[i];
                dirs[i] = dirs[j];
                dirs[j] = t;
            }
        }
    }
}


static void OrderAiExploreDirs(WORD id, WORD curX, WORD curY, WORD dirs[4])
{
    WORD i;
    WORD j;
    static const WORD dirX[4] = {1, 0, -1, 0};   /* right, down, left, up */
    static const WORD dirY[4] = {0, 1, 0, -1};

    for (i = 0; i < 4; i++) dirs[i] = i;

    for (i = 0; i < 3; i++) {
        for (j = i + 1; j < 4; j++) {
            WORD ia = dirs[i];
            WORD ib = dirs[j];
            WORD ax = curX + dirX[ia];
            WORD ay = curY + dirY[ia];
            WORD bx = curX + dirX[ib];
            WORD by = curY + dirY[ib];
            WORD scoreA = 0;
            WORD scoreB = 0;

            if (ax < 0 || ay < 0 || ax >= MAP_W || ay >= MAP_H || !RobotCanPassTile(id, ax, ay) || RobotAtTile(ax, ay, id)) scoreA += 100;
            if (bx < 0 || by < 0 || bx >= MAP_W || by >= MAP_H || !RobotCanPassTile(id, bx, by) || RobotAtTile(bx, by, id)) scoreB += 100;
            if (ax == aiPrevTileX[id] && ay == aiPrevTileY[id]) scoreA += 8;
            if (bx == aiPrevTileX[id] && by == aiPrevTileY[id]) scoreB += 8;
            scoreA += AiRecentVisitPenalty(id, ax, ay);
            scoreB += AiRecentVisitPenalty(id, bx, by);

            if (scoreB < scoreA) {
                WORD t = dirs[i];
                dirs[i] = dirs[j];
                dirs[j] = t;
            }
        }
    }
}

static BOOL AiFindNearestReachableDirt(WORD id, WORD *outX, WORD *outY, WORD *outDx, WORD *outDy)
{
    static const WORD dirX[4] = {1, 0, -1, 0};   /* right, down, left, up */
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD queueX[MAP_W * MAP_H];
    WORD queueY[MAP_W * MAP_H];
    UBYTE visited[MAP_H][MAP_W];
    WORD firstDir[MAP_H][MAP_W];
    WORD dirs[4];
    WORD head = 0;
    WORD tail = 0;
    WORD x;
    WORD y;
    WORD i;
    WORD curX;
    WORD curY;

    if (outX) *outX = -1;
    if (outY) *outY = -1;
    if (outDx) *outDx = 0;
    if (outDy) *outDy = 0;
    if (id < humanPlayers || id >= robotCount) return FALSE;

    curX = robots[id].tileX;
    curY = robots[id].tileY;
    if (curX < 0 || curY < 0 || curX >= MAP_W || curY >= MAP_H) return FALSE;

    if (map[curY][curX] == TILE_DIRT) {
        if (outX) *outX = curX;
        if (outY) *outY = curY;
        return TRUE;
    }

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            visited[y][x] = FALSE;
            firstDir[y][x] = -1;
        }
    }

    OrderAiExploreDirs(id, curX, curY, dirs);
    visited[curY][curX] = TRUE;
    queueX[tail] = curX;
    queueY[tail] = curY;
    tail++;

    while (head < tail) {
        WORD fromX = queueX[head];
        WORD fromY = queueY[head];
        head++;

        for (i = 0; i < 4; i++) {
            WORD dir = dirs[i];
            WORD nx = fromX + dirX[dir];
            WORD ny = fromY + dirY[dir];
            WORD stepDir;

            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
            if (visited[ny][nx]) continue;
            if (!RobotCanPassTile(id, nx, ny)) continue;
            if (RobotAtTile(nx, ny, id)) continue;

            stepDir = firstDir[fromY][fromX];
            if (stepDir < 0) stepDir = dir;
            visited[ny][nx] = TRUE;
            firstDir[ny][nx] = stepDir;

            if (map[ny][nx] == TILE_DIRT) {
                if (outX) *outX = nx;
                if (outY) *outY = ny;
                if (outDx) *outDx = dirX[stepDir];
                if (outDy) *outDy = dirY[stepDir];
                return TRUE;
            }

            if (tail < MAP_W * MAP_H) {
                queueX[tail] = nx;
                queueY[tail] = ny;
                tail++;
            }
        }
    }

    return FALSE;
}

static BOOL AiFindPathStep(WORD id, WORD targetX, WORD targetY, WORD *outDx, WORD *outDy, WORD *outDist)
{
    static const WORD dirX[4] = {1, 0, -1, 0};   /* right, down, left, up */
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD queueX[MAP_W * MAP_H];
    WORD queueY[MAP_W * MAP_H];
    WORD dist[MAP_H][MAP_W];
    WORD firstDir[MAP_H][MAP_W];
    WORD dirs[4];
    WORD head = 0;
    WORD tail = 0;
    WORD x;
    WORD y;
    WORD i;
    WORD curX;
    WORD curY;

    if (outDx) *outDx = 0;
    if (outDy) *outDy = 0;
    if (outDist) *outDist = 32767;
    if (id < humanPlayers || id >= robotCount) return FALSE;
    if (targetX < 0 || targetY < 0 || targetX >= MAP_W || targetY >= MAP_H) return FALSE;

    curX = robots[id].tileX;
    curY = robots[id].tileY;
    if (curX == targetX && curY == targetY) {
        if (outDist) *outDist = 0;
        return TRUE;
    }

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            dist[y][x] = 32767;
            firstDir[y][x] = -1;
        }
    }

    OrderAiPathDirs(id, curX, curY, targetX, targetY, dirs);
    dist[curY][curX] = 0;
    queueX[tail] = curX;
    queueY[tail] = curY;
    tail++;

    while (head < tail) {
        WORD fromX = queueX[head];
        WORD fromY = queueY[head];
        WORD nextDist = dist[fromY][fromX] + 1;
        head++;

        for (i = 0; i < 4; i++) {
            WORD dir = dirs[i];
            WORD nx = fromX + dirX[dir];
            WORD ny = fromY + dirY[dir];
            WORD stepDir;

            if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
            if (dist[ny][nx] != 32767) continue;
            if (!RobotCanPassTile(id, nx, ny)) continue;
            if (RobotAtTile(nx, ny, id)) continue;

            stepDir = firstDir[fromY][fromX];
            if (stepDir < 0) stepDir = dir;
            dist[ny][nx] = nextDist;
            firstDir[ny][nx] = stepDir;

            if (nx == targetX && ny == targetY) {
                if (outDx) *outDx = dirX[stepDir];
                if (outDy) *outDy = dirY[stepDir];
                if (outDist) *outDist = nextDist;
                return TRUE;
            }

            if (tail < MAP_W * MAP_H) {
                queueX[tail] = nx;
                queueY[tail] = ny;
                tail++;
            }
        }
    }

    return FALSE;
}

static void ChooseAiMove(WORD id)
{
    WORD x;
    WORD y;
    WORD bestX = -1;
    WORD bestY = -1;
    WORD bestMoveDx = 0;
    WORD bestMoveDy = 0;
    WORD curX;
    WORD curY;
    WORD cachedTargetX;
    WORD cachedTargetY;
    BOOL targetingDirt = FALSE;
    BOOL bestMoveReady = FALSE;
    static const WORD dirX[4] = {1, 0, -1, 0};   /* right, down, left, up */
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD dir;

    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].moving) return;
    if (robots[id].stunTicks > 0) return;

    curX = robots[id].tileX;
    curY = robots[id].tileY;
    cachedTargetX = aiTargetDirtX[id];
    cachedTargetY = aiTargetDirtY[id];

    if (robots[id].battery <= 25 || (robots[id].battery <= 0 && (AbsW(curX - RobotDockX(id)) + AbsW(curY - RobotDockY(id)) <= 4))) {
        bestX = RobotDockX(id);
        bestY = RobotDockY(id);
        bestMoveReady = AiFindPathStep(id, bestX, bestY, &bestMoveDx, &bestMoveDy, NULL);
    } else if (robots[id].battery < batteryCostPerMove) {
        aiTargetDirtX[id] = -1;
        aiTargetDirtY[id] = -1;
        return;
    }

    /* Easy AI still docks sensibly on low battery, but otherwise plays badly:
     * most turns it shuffles off in a random passable direction instead of
     * beelining for the nearest dirt, so it cleans far slower than Normal/
     * Hard and a human can comfortably out-clean it. */
    if (bestX < 0 && aiDifficulty <= 0 && RandRange(EASY_AI_CONFUSION_CHANCE) < EASY_AI_CONFUSION_ROLL) {
        WORD wanderDirs[4];

        aiTargetDirtX[id] = -1;
        aiTargetDirtY[id] = -1;
        ShuffleAiDirs(wanderDirs);
        for (dir = 0; dir < 4; dir++) {
            WORD r = wanderDirs[dir];
            if (curX + dirX[r] == aiPrevTileX[id] && curY + dirY[r] == aiPrevTileY[id]) continue;
            if (StartRobotMove(id, dirX[r], dirY[r])) return;
        }
        for (dir = 0; dir < 4; dir++) {
            WORD r = wanderDirs[dir];
            if (StartRobotMove(id, dirX[r], dirY[r])) return;
        }
        return;
    }

    if (bestX < 0) {
        x = aiTargetDirtX[id];
        y = aiTargetDirtY[id];
        if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && map[y][x] == TILE_DIRT) {
            if (AiFindPathStep(id, x, y, &bestMoveDx, &bestMoveDy, NULL)) {
                bestX = x;
                bestY = y;
                targetingDirt = TRUE;
                bestMoveReady = (bestMoveDx != 0 || bestMoveDy != 0);
            } else {
                aiTargetDirtX[id] = -1;
                aiTargetDirtY[id] = -1;
            }
        } else {
            aiTargetDirtX[id] = -1;
            aiTargetDirtY[id] = -1;
        }
    }

    if (bestX < 0) {
        if (AiFindNearestReachableDirt(id, &bestX, &bestY, &bestMoveDx, &bestMoveDy)) {
            targetingDirt = TRUE;
            bestMoveReady = (bestMoveDx != 0 || bestMoveDy != 0);
            if (bestX == cachedTargetX && bestY == cachedTargetY && bestMoveReady) {
                aiTargetDirtX[id] = bestX;
                aiTargetDirtY[id] = bestY;
            }
        }
    }

    if (!targetingDirt) {
        aiTargetDirtX[id] = -1;
        aiTargetDirtY[id] = -1;
    }

    if (bestX >= 0 && curX == bestX && curY == bestY) return;

    if (bestX >= 0 && !bestMoveReady && bestMoveDx == 0 && bestMoveDy == 0) {
        bestMoveReady = AiFindPathStep(id, bestX, bestY, &bestMoveDx, &bestMoveDy, NULL);
    }

    if (bestX >= 0 && (bestMoveDx != 0 || bestMoveDy != 0)) {
        if (StartRobotMove(id, bestMoveDx, bestMoveDy)) {
            if (targetingDirt) {
                aiTargetDirtX[id] = bestX;
                aiTargetDirtY[id] = bestY;
            }
            return;
        }
    }

    /* Final fallback random movement if no pathable target is available. */
    {
        WORD dirs[4];

        aiTargetDirtX[id] = -1;
        aiTargetDirtY[id] = -1;
        ShuffleAiDirs(dirs);
        for (dir = 0; dir < 4; dir++) {
            WORD r = dirs[dir];
            if (curX + dirX[r] == aiPrevTileX[id] && curY + dirY[r] == aiPrevTileY[id]) continue;
            if (AiRecentVisitPenalty(id, curX + dirX[r], curY + dirY[r]) > 0) continue;
            if (StartRobotMove(id, dirX[r], dirY[r])) return;
        }
        for (dir = 0; dir < 4; dir++) {
            WORD r = dirs[dir];
            if (StartRobotMove(id, dirX[r], dirY[r])) return;
        }
    }
}

static void ChoosePlayerMove(WORD id)
{
    if (id < 0 || id >= humanPlayers) return;
    if (robots[id].moving) return;
    if (robots[id].stunTicks > 0) return;

    if (keyLeft[id]) {
        StartRobotMove(id, -1, 0);
    } else if (keyRight[id]) {
        StartRobotMove(id, 1, 0);
    } else if (keyUp[id]) {
        StartRobotMove(id, 0, -1);
    } else if (keyDown[id]) {
        StartRobotMove(id, 0, 1);
    } else if (joyLeft[id]) {
        StartRobotMove(id, -1, 0);
    } else if (joyRight[id]) {
        StartRobotMove(id, 1, 0);
    } else if (joyUp[id]) {
        StartRobotMove(id, 0, -1);
    } else if (joyDown[id]) {
        StartRobotMove(id, 0, 1);
    }
}

static BOOL AnyRobotCanMove(void)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (robots[i].moving) return TRUE;
        /* Match StartRobotMove: a move needs a full battery step, or an
           emergency move while completely flat. A robot holding less than a
           step's worth of battery (possible on Normal/Hard where a move
           costs more than 1) can never move again, so it must not keep the
           round from ending. */
        if (robots[i].battery >= batteryCostPerMove) return TRUE;
        if (robots[i].battery == 0 && robots[i].emergencyMovesLeft > 0) return TRUE;
    }

    return FALSE;
}

/* Longer matches for more robots on the field: a 1-player match keeps the
 * original 5 rounds, and every player/AI rival added beyond a 2-competitor
 * match adds 2 more rounds (2p=6, 3p=8, 4p=10, ...). */
static WORD MatchRoundCount(void)
{
    WORD rounds = (robotCount * 2) + 2;
    if (rounds < 5) rounds = 5;
    return rounds;
}

static void CheckEndState(void)
{
    WORD i;
    WORD best = -1;

    if (gameState == GAME_BONUS_PLAYING) {
        if (bonusBossHealth > 0 || bonusBossExplosionTicks > 0) return;
        FinishBonusRound();
        return;
    }

    if (gameState != GAME_PLAYING) return;
    if (dirtLeft > 0 && AnyRobotCanMove()) return;

    for (i = 0; i < robotCount; i++) {
        roundScores[i] = robots[i].score;
        totalScores[i] += robots[i].score;
        if (robots[i].score > best) {
            best = robots[i].score;
            roundWinner = i;
        }
    }
    roundWins[roundWinner]++;

    /* Attract mode has no one to press "continue", so a finished demo round
     * just rolls straight into a fresh one instead of showing the human
     * round/match-end screens; any real input exits back to the title
     * screen long before this would normally come up. */
    if (demoModeActive) {
        StartDemoMode();
        return;
    }

    if (roundIndex >= MatchRoundCount() - 1) {
        finalWinner = 0;
        for (i = 1; i < robotCount; i++) {
            if (totalScores[i] > totalScores[finalWinner]) {
                finalWinner = i;
            }
        }
        bonusAvailable = FALSE;
        for (i = 0; i < robotCount; i++) {
            if (totalScores[i] > BONUS_SCORE_THRESHOLD) bonusAvailable = TRUE;
        }
        gameState = GAME_MATCH_END;
    } else {
        gameState = GAME_ROUND_END;
    }
}



static void FreeBonusBossCache(void)
{
    if (bonusBossCacheBM) {
        FreeBitMap(bonusBossCacheBM);
        bonusBossCacheBM = NULL;
    }

    if (bonusBossCacheMaskBM) {
        FreeBitMap(bonusBossCacheMaskBM);
        bonusBossCacheMaskBM = NULL;
    }

    bonusBossCacheRP.BitMap = NULL;
}

static BOOL BuildBonusBossCache(void)
{
    struct RastPort maskRP;
    WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
    WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;
    WORD variant;
    WORD srcX;
    WORD px;
    WORD py;
    WORD robotId = (finalWinner >= 0 && finalWinner < robotCount) ? finalWinner : 0;

    FreeBonusBossCache();

    if (!robotCacheBM || robotId < 0 || robotId >= robotCount) return FALSE;

    bonusBossCacheBM = AllocBitMap(bossW, bossH, DEPTH,
                                   BMF_CLEAR | BMF_DISPLAYABLE,
                                   scr ? scr->RastPort.BitMap : NULL);
    bonusBossCacheMaskBM = AllocBitMap(bossW, bossH, 1,
                                       BMF_CLEAR | BMF_DISPLAYABLE,
                                       scr ? scr->RastPort.BitMap : NULL);

    if (!bonusBossCacheBM || !bonusBossCacheMaskBM) {
        FreeBonusBossCache();
        return FALSE;
    }

    InitRastPort(&bonusBossCacheRP);
    bonusBossCacheRP.BitMap = bonusBossCacheBM;
    InitRastPort(&maskRP);
    maskRP.BitMap = bonusBossCacheMaskBM;

    variant = robots[robotId].spriteVariant;
    if (variant >= ROBOT_VARIANTS) variant = 0;
    srcX = (variant * SPR_STATE_COUNT + SPR_DOWN) * ROBOT_W;

    for (py = 0; py < ROBOT_H; py++) {
        for (px = 0; px < ROBOT_W; px++) {
            LONG pen = ReadPixel(&robotRP, srcX + px, py);
            if (pen <= 0) continue;
            SetAPen(&bonusBossCacheRP, (UBYTE)pen);
            RectFill(&bonusBossCacheRP,
                     px * BONUS_BOSS_SCALE,
                     py * BONUS_BOSS_SCALE,
                     ((px + 1) * BONUS_BOSS_SCALE) - 1,
                     ((py + 1) * BONUS_BOSS_SCALE) - 1);
            SetAPen(&maskRP, 1);
            RectFill(&maskRP,
                     px * BONUS_BOSS_SCALE,
                     py * BONUS_BOSS_SCALE,
                     ((px + 1) * BONUS_BOSS_SCALE) - 1,
                     ((py + 1) * BONUS_BOSS_SCALE) - 1);
        }
    }

    return TRUE;
}

static void ResetBonusBoss(void)
{
    bonusBossHealth = BONUS_BOSS_MAX_HEALTH;
    MarkBossHpTextDirty();
    MarkHudStatusTextDirty();
    bonusBossX = (SCREEN_W - (ROBOT_W * BONUS_BOSS_SCALE)) / 2;
    bonusBossY = MAP_Y + ((MAP_H * TILE_SIZE) - (ROBOT_H * BONUS_BOSS_SCALE)) / 2;
    bonusBossDx = 1;
    bonusBossDy = 1;
    bonusBossPatternMode = BOSS_PATTERN_DIAGONAL;
    bonusBossPatternTicks = BOSS_PATTERN_MIN_TICKS;
    bonusBossSpinAngle = 0;
    bonusBossSpinRadiusStep = 0;
    bonusBossSpinRadiusDir = 1;
    bonusBossSpinRadiusTickCounter = 0;
    bonusBossPhase = 0;
    bonusBossFacingState = SPR_DOWN;
    bonusBossFireTicks = BONUS_BOSS_FIRE_INTERVAL_TICKS;
    bonusAiFireTicks = BONUS_AI_FIRE_INTERVAL_TICKS;
    bonusBossExplosionTicks = 0;
    nightModeActive = FALSE;
    nightModeTicks = 0;
    nightModeCooldownTicks = NIGHT_MODE_MIN_GAP_TICKS +
        (WORD)RandRange(NIGHT_MODE_MAX_GAP_TICKS - NIGHT_MODE_MIN_GAP_TICKS + 1);
    { WORD i; for (i = 0; i < MAX_ROBOTS; i++) { bonusBossTouchCooldown[i] = 0; bonusBossTouching[i] = FALSE; } }
}

static void BossStunRobot(WORD id, const char *label)
{
    if (id < 0 || id >= robotCount) return;

    robots[id].stunTicks = BONUS_BOSS_TOUCH_STUN_TICKS;
    robots[id].boltStunned = FALSE;
    robots[id].boltImmuneTicks = 0;
    if (robots[id].moving) {
        robots[id].tileX = robots[id].targetX;
        robots[id].tileY = robots[id].targetY;
        robots[id].px = TO_FP(robots[id].tileX * TILE_SIZE);
        robots[id].py = TO_FP(robots[id].tileY * TILE_SIZE);
        robots[id].targetPx = robots[id].px;
        robots[id].targetPy = robots[id].py;
        robots[id].moving = FALSE;
    }
    snprintf(lastPowerText, sizeof(lastPowerText), "%s %s STUN 5S", RobotTag(id), label);
    lastPowerTicks = 60;
}

static BOOL RectsOverlap(WORD ax, WORD ay, WORD aw, WORD ah, WORD bx, WORD by, WORD bw, WORD bh)
{
    if (ax + aw <= bx || bx + bw <= ax) return FALSE;
    if (ay + ah <= by || by + bh <= ay) return FALSE;
    return TRUE;
}

/* How much of the robot's box the boss box currently covers, as a fraction
 * along each axis. A near-full run-over covers most of both axes; a glancing
 * touch on a corner/edge only covers a sliver of one or both. */
static BOOL RectOverlapCoversMost(WORD ax, WORD ay, WORD aw, WORD ah, WORD bx, WORD by, WORD bw, WORD bh)
{
    WORD ix0 = (ax > bx) ? ax : bx;
    WORD iy0 = (ay > by) ? ay : by;
    WORD ix1 = (ax + aw < bx + bw) ? (ax + aw) : (bx + bw);
    WORD iy1 = (ay + ah < by + bh) ? (ay + ah) : (by + bh);
    WORD overlapW = ix1 - ix0;
    WORD overlapH = iy1 - iy0;

    if (overlapW <= 0 || overlapH <= 0) return FALSE;
    return (overlapW * BONUS_BOSS_STUN_OVERLAP_DEN >= aw * BONUS_BOSS_STUN_OVERLAP_NUM) &&
           (overlapH * BONUS_BOSS_STUN_OVERLAP_DEN >= ah * BONUS_BOSS_STUN_OVERLAP_NUM);
}

/* A glancing hit only stops the robot and shoves it back one tile away from
 * the boss, unlike BossStunRobot's full multi-second stun. */
static void BossPushBackRobot(WORD id, WORD bossCenterX, WORD bossCenterY)
{
    WORD robotCenterX;
    WORD robotCenterY;
    WORD pushDx = 0;
    WORD pushDy = 0;
    WORD newX;
    WORD newY;

    if (id < 0 || id >= robotCount) return;

    if (robots[id].moving) {
        robots[id].tileX = robots[id].targetX;
        robots[id].tileY = robots[id].targetY;
        robots[id].px = TO_FP(robots[id].tileX * TILE_SIZE);
        robots[id].py = TO_FP(robots[id].tileY * TILE_SIZE);
        robots[id].targetPx = robots[id].px;
        robots[id].targetPy = robots[id].py;
        robots[id].moving = FALSE;
    }

    robotCenterX = MAP_X + FP_TO_INT(robots[id].px) + (ROBOT_W / 2);
    robotCenterY = MAP_Y + FP_TO_INT(robots[id].py) + (ROBOT_H / 2);

    if (AbsW(robotCenterX - bossCenterX) >= AbsW(robotCenterY - bossCenterY)) {
        pushDx = (robotCenterX >= bossCenterX) ? 1 : -1;
    } else {
        pushDy = (robotCenterY >= bossCenterY) ? 1 : -1;
    }

    newX = robots[id].tileX + pushDx;
    newY = robots[id].tileY + pushDy;
    if (RobotCanPassTile(id, newX, newY) && !RobotAtTile(newX, newY, id)) {
        robots[id].tileX = newX;
        robots[id].tileY = newY;
        robots[id].px = TO_FP(newX * TILE_SIZE);
        robots[id].py = TO_FP(newY * TILE_SIZE);
        robots[id].targetPx = robots[id].px;
        robots[id].targetPy = robots[id].py;
    }

    robots[id].stunTicks = 0;
    robots[id].boltStunned = FALSE;
    snprintf(lastPowerText, sizeof(lastPowerText), "%s BOSS PUSHED BACK", RobotTag(id));
    lastPowerTicks = 40;
}

static void FireBossBolt(void)
{
    static const WORD dirs[8][2] = {
        {-1, -1}, {0, -1}, {1, -1},
        {-1,  0},          {1,  0},
        {-1,  1}, {0,  1}, {1,  1}
    };
    WORD i;
    WORD d;
    WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
    WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;

    for (i = 0; i < MAX_BOSS_BOLTS; i++) {
        if (!bossBolts[i].active) {
            d = RandRange(8);
            bossBolts[i].active = TRUE;
            bossBolts[i].dirX = dirs[d][0];
            bossBolts[i].dirY = dirs[d][1];
            bossBolts[i].px = TO_FP(bonusBossX + (bossW / 2) - (ROBOT_W / 2) - MAP_X);
            bossBolts[i].py = TO_FP(bonusBossY + (bossH / 2) - (ROBOT_H / 2) - MAP_Y);
            bossBolts[i].ttl = 70;
            PlayBoltFireSample();
            return;
        }
    }
}

/* Rolls the boss's next movement pattern: a straight diagonal bounce (the
 * original behaviour), a single-axis horizontal or vertical sweep, or an
 * outward spiral from the arena centre. Always picking a different mode
 * from the current one keeps a run from feeling repetitive. */
static void ChooseBossPattern(void)
{
    WORD mode = (WORD)RandRange(BOSS_PATTERN_MODE_COUNT);

    if (BOSS_PATTERN_MODE_COUNT > 1 && mode == bonusBossPatternMode) {
        mode = (WORD)((mode + 1 + RandRange(BOSS_PATTERN_MODE_COUNT - 1)) % BOSS_PATTERN_MODE_COUNT);
    }
    bonusBossPatternMode = mode;
    bonusBossPatternTicks = BOSS_PATTERN_MIN_TICKS +
        (WORD)RandRange(BOSS_PATTERN_MAX_TICKS - BOSS_PATTERN_MIN_TICKS + 1);

    switch (mode) {
        case BOSS_PATTERN_HORIZONTAL:
            bonusBossDx = (RandRange(2) == 0) ? -1 : 1;
            bonusBossDy = 0;
            break;
        case BOSS_PATTERN_VERTICAL:
            bonusBossDx = 0;
            bonusBossDy = (RandRange(2) == 0) ? -1 : 1;
            break;
        case BOSS_PATTERN_SPIN:
            bonusBossSpinAngle = (WORD)RandRange(32);
            bonusBossSpinRadiusStep = 0;
            bonusBossSpinRadiusDir = 1;
            bonusBossSpinRadiusTickCounter = 0;
            break;
        case BOSS_PATTERN_DIAGONAL:
        default:
            bonusBossDx = (RandRange(2) == 0) ? -1 : 1;
            bonusBossDy = (RandRange(2) == 0) ? -1 : 1;
            break;
    }
}

static void StepBonusBoss(void)
{
    WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
    WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;
    WORD minX = TILE_SIZE;
    WORD maxX = SCREEN_W - TILE_SIZE - bossW;
    WORD minY = MAP_Y + TILE_SIZE;
    WORD maxY = SCREEN_H - TILE_SIZE - bossH;
    WORD i;

    if (gameState != GAME_BONUS_PLAYING || bonusBossHealth <= 0) return;

    bonusBossPhase = (bonusBossPhase + 1) & 31;

    if (bonusBossPatternTicks > 0) bonusBossPatternTicks--;
    if (bonusBossPatternTicks <= 0) ChooseBossPattern();

    if (bonusBossPatternMode == BOSS_PATTERN_SPIN) {
        WORD centerX = (minX + maxX) / 2;
        WORD centerY = (minY + maxY) / 2;
        WORD maxRadiusX = (maxX - minX) / 2;
        WORD maxRadiusY = (maxY - minY) / 2;
        WORD radiusX;
        WORD radiusY;

        bonusBossSpinRadiusTickCounter++;
        if (bonusBossSpinRadiusTickCounter >= BOSS_SPIN_RADIUS_GROW_TICKS) {
            bonusBossSpinRadiusTickCounter = 0;
            bonusBossSpinRadiusStep += bonusBossSpinRadiusDir;
            if (bonusBossSpinRadiusStep >= BOSS_SPIN_RADIUS_STEPS) {
                bonusBossSpinRadiusStep = BOSS_SPIN_RADIUS_STEPS;
                bonusBossSpinRadiusDir = -1;
            } else if (bonusBossSpinRadiusStep <= 0) {
                bonusBossSpinRadiusStep = 0;
                bonusBossSpinRadiusDir = 1;
            }
        }
        bonusBossSpinAngle = (bonusBossSpinAngle + 1) & 31;

        radiusX = (maxRadiusX * bonusBossSpinRadiusStep) / BOSS_SPIN_RADIUS_STEPS;
        radiusY = (maxRadiusY * bonusBossSpinRadiusStep) / BOSS_SPIN_RADIUS_STEPS;

        bonusBossX = centerX + (radiusX * IntroEffectSin(bonusBossSpinAngle)) / 13;
        bonusBossY = centerY + (radiusY * IntroEffectSin(bonusBossSpinAngle + 8)) / 13;
        if (bonusBossX < minX) bonusBossX = minX;
        if (bonusBossX > maxX) bonusBossX = maxX;
        if (bonusBossY < minY) bonusBossY = minY;
        if (bonusBossY > maxY) bonusBossY = maxY;
    } else {
        bonusBossX += bonusBossDx;
        bonusBossY += bonusBossDy;

        if (bonusBossX <= minX || bonusBossX >= maxX) {
            bonusBossDx = -bonusBossDx;
            bonusBossX += bonusBossDx;
        }
        if (bonusBossY <= minY || bonusBossY >= maxY) {
            bonusBossDy = -bonusBossDy;
            bonusBossY += bonusBossDy;
        }
    }

    for (i = 0; i < robotCount; i++) {
        WORD rx = MAP_X + FP_TO_INT(robots[i].px);
        WORD ry = MAP_Y + FP_TO_INT(robots[i].py);
        BOOL touching;

        if (bonusBossTouchCooldown[i] > 0) bonusBossTouchCooldown[i]--;

        touching = RectsOverlap(rx, ry, ROBOT_W, ROBOT_H, bonusBossX, bonusBossY, bossW, bossH);
        if (touching) {
            if (!bonusBossTouching[i] && bonusBossTouchCooldown[i] <= 0) {
                if (RectOverlapCoversMost(rx, ry, ROBOT_W, ROBOT_H, bonusBossX, bonusBossY, bossW, bossH)) {
                    BossStunRobot(i, "BOSS");
                    bonusBossTouchCooldown[i] = BONUS_BOSS_TOUCH_COOLDOWN_TICKS;
                } else {
                    BossPushBackRobot(i, bonusBossX + (bossW / 2), bonusBossY + (bossH / 2));
                    bonusBossTouchCooldown[i] = BONUS_BOSS_GRAZE_COOLDOWN_TICKS;
                }
            }
            bonusBossTouching[i] = TRUE;
        } else {
            bonusBossTouching[i] = FALSE;
        }
    }

    if (bonusBossFireTicks > 0) bonusBossFireTicks--;
    if (bonusBossFireTicks <= 0) {
        FireBossBolt();
        bonusBossFireTicks = BONUS_BOSS_FIRE_INTERVAL_TICKS;
    }
}


static void StartBonusBossExplosion(void)
{
    WORD i;

    if (gameState != GAME_BONUS_PLAYING || bonusBossExplosionTicks > 0) return;

    bonusBossExplosionX = bonusBossX;
    bonusBossExplosionY = bonusBossY;
    bonusBossHealth = 0;
    MarkBossHpTextDirty();
    MarkHudStatusTextDirty();
    bonusBossExplosionTicks = BONUS_BOSS_EXPLOSION_TICKS;
    for (i = 0; i < MAX_BOSS_BOLTS; i++) bossBolts[i].active = FALSE;
    for (i = 0; i < MAX_ROBOTS; i++) playerBolts[i].active = FALSE;
    snprintf(lastPowerText, sizeof(lastPowerText), "BOSS DEFEATED!");
    lastPowerTicks = BONUS_BOSS_EXPLOSION_TICKS;
}

static void FinishBonusRound(void)
{
    WORD i;

    finalWinner = 0;
    for (i = 1; i < robotCount; i++) {
        if (totalScores[i] > totalScores[finalWinner]) finalWinner = i;
    }
    bonusAvailable = FALSE;
    bonusBossHealth = 0;
    MarkBossHpTextDirty();
    MarkHudStatusTextDirty();
    bonusBossExplosionTicks = 0;
    StopNightMode();
    FreeBonusBossCache();
    gameState = GAME_BONUS_END;
    StopGameplaySamples();
    ClearMovementKeys();
}

static void StartBonusRound(void)
{
    WORD x;
    WORD y;

    StopGameplaySamples();
    StopRoundStartSamples();
    roundIndex = 5;
    roomType = 0;
    MarkHudStatusTextDirty();

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) {
                map[y][x] = TILE_WALL;
            } else {
                map[y][x] = TILE_FLOOR;
            }
        }
    }

    for (x = 2; x < MAP_W - 2; x += 3) {
        map[1][x] = TILE_DOCK;
        map[MAP_H - 2][x] = TILE_DOCK;
    }
    for (y = 2; y < MAP_H - 2; y += 3) {
        map[y][1] = TILE_DOCK;
        map[y][MAP_W - 2] = TILE_DOCK;
    }
    map[RobotDockY(0)][RobotDockX(0)] = TILE_DOCK;
    map[RobotDockY(1)][RobotDockX(1)] = TILE_DOCK;
    map[RobotDockY(2)][RobotDockX(2)] = TILE_DOCK;
    map[RobotDockY(3)][RobotDockX(3)] = TILE_DOCK;

    ClearMovementKeys();
    { WORD bi; for (bi = 0; bi < MAX_ROBOTS; bi++) playerBolts[bi].active = FALSE; }
    { WORD bi; for (bi = 0; bi < MAX_BOSS_BOLTS; bi++) bossBolts[bi].active = FALSE; }
    lastPowerText[0] = '\0';
    lastPowerTicks = 0;
    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundCountdownSoundNumber = ROUND_COUNTDOWN_SECONDS;
    roundCountdownLastSoundNumber = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    empCountdownTicks = 0;
    empCountdownOwner = -1;
    bonusAiFireTicks = BONUS_AI_FIRE_INTERVAL_TICKS;
    bonusBossExplosionTicks = 0;
    gameState = GAME_BONUS_PLAYING;
    ClosePauseMenu();

    InitRobots();
    BuildBonusBossCache();
    for (x = 0; x < robotCount; x++) {
        robots[x].score = 0;
        robots[x].cleanStreak = 0;
        robots[x].powerMovesLeft = 0;
        robots[x].powerType = POWER_NONE;
    }
    CountDirt();
    ResetBonusBoss();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
    roundCountdownLastSoundNumber = 0;
}

static void EnterTitleScreen(void)
{
    if (gameState != GAME_INTRO) {
        StopGameplaySamples();
        StopRoundStartSamples();
    }
    gameState = GAME_TITLE;
    introTicks = 0;
    humanPlayers = 1;
    demoModeActive = FALSE;
    demoJoyPrimed = FALSE;
    titleIdleTicks = 0;
    titleSelectPlayer = 0;
    titleTwoPlayerArmed = FALSE;
    titlePlayer2Locked = FALSE;
    CloseAiSelectMenu();
    CloseAiDifficultyMenu();
    ResetAllJoystickConfirmHolds();
    joyEnabled[0] = FALSE;
    joyEnabled[1] = FALSE;
    ClearMovementKeys();
#if USE_DIRTY_RECTS
    dirtyRectsBuiltForFrame = FALSE;
    dirtyRectsValid = FALSE;
    dirtyForceFullFrame = TRUE;
#endif
    titleStaticDirty = TRUE;
    titlePanelDirty = TRUE;
    RequestTitleFullPresents();
    LoadGamePalette();
    PrepareTitlePresentation();
}

static void StepIntro(void)
{
    WORD fadeLevel;

    if (gameState != GAME_INTRO) return;
    if (introTicks > INTRO_TOTAL_FRAMES) {
        EnterTitleScreen();
        return;
    }
    if (!introTitleBM) {
        EnterTitleScreen();
        return;
    }

    if (introTicks == 0 && !introPaletteActive) {
        LoadIntroPaletteLevel(INTRO_EFFECT_FRAMES);
    }

    if (introTicks >= INTRO_HOLD_FRAMES) {
        fadeLevel = INTRO_TOTAL_FRAMES - introTicks;
        LoadIntroPaletteLevel(fadeLevel);
    }

    introTicks++;
}

static void StepGame(void)
{
    WORD i;
    WORD frameTicks = 1;

    if (gameState == GAME_INTRO) {
        StepIntro();
        return;
    }

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) {
        StopEmpPaletteCycle();
        StopNightMode();
        return;
    }

    UpdateEmpPaletteCycle();
    UpdateNightMode();

    BeginGameplayDirtyRects();

    if (pauseMenuOpen) {
        ServiceHooverMoveSample();
        ForceGameplayFullPresent();
        FinishGameplayDirtyRects();
        return;
    }

    StepRoundStartSamples();

    if (roundCountdownTicks > 0) {
        if (frameTicks < roundCountdownTicks) {
            roundCountdownTicks -= frameTicks;
            roundCountdownSoundNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
            if (roundCountdownSoundNumber != roundCountdownLastSoundNumber) {
                PlayCountdownSample();
                roundCountdownLastSoundNumber = roundCountdownSoundNumber;
            }
            ForceGameplayFullPresent();
            FinishGameplayDirtyRects();
            return;
        }

        frameTicks -= roundCountdownTicks;
        roundCountdownTicks = 0;
        roundGoTicks = ROUND_GO_FRAMES;
        if (!roundGoSoundPlayed) {
            PlayGoSample();
            roundGoSoundPlayed = TRUE;
        }
        StartMainGameMusic();
        if (frameTicks < 1) frameTicks = 1;
    }
    if (roundGoTicks > 0) {
        if (frameTicks >= roundGoTicks) {
            roundGoTicks = 0;
        } else {
            roundGoTicks -= frameTicks;
        }
    }

    if (!ShouldAdvanceGameplayFrame()) {
        ServiceHooverMoveSample();
        FinishGameplayDirtyRects();
        return;
    }

    StepBonusBoss();

    for (i = 0; i < robotCount; i++) {
        StepRobotMovement(i);
        if (robots[i].turnTicks > 0) robots[i].turnTicks--;
        if (robots[i].stunTicks > 0) {
            robots[i].stunTicks--;
            if (robots[i].stunTicks <= 0 && robots[i].boltStunned) {
                robots[i].boltStunned = FALSE;
                robots[i].boltImmuneTicks = BOLT_ESCAPE_IMMUNE_TICKS;
            }
        } else if (robots[i].boltImmuneTicks > 0) {
            robots[i].boltImmuneTicks--;
        }
        if (robots[i].powerType == POWER_BOLT && robots[i].powerMovesLeft > 0 && (RandRange(24) == 0)) {
            WORD j;
            for (j = 0; j < robotCount; j++) {
                if (j != i && AbsW(robots[j].tileX - robots[i].tileX) + AbsW(robots[j].tileY - robots[i].tileY) <= 3) {
                    if (ApplyBoltStun(j, 3)) break;
                }
            }
        }
        if (!robots[i].moving && map[robots[i].tileY][robots[i].tileX] == TILE_DOCK && robots[i].chargeTicks > 0) {
            robots[i].chargeTicks--;
            if (robots[i].chargeTicks <= 0) {
                robots[i].battery = maxBattery;
                robots[i].emergencyMovesLeft = EMERGENCY_DOCK_MOVES;
            }
        }
        /* A hoover normally cleans a tile only when it finishes moving onto
           it, so any hoover left parked on a dirt tile leaves that dirt
           stuck: it can't clean the tile it is sitting on, and no rival can
           enter an occupied tile to reach the dirt underneath. This happens
           when a hoover runs flat, is frozen by an EMP, is bolt-stunned, or
           spawns on dirt. The invariant we want is simply that a hoover
           occupying a dirt tile cleans it, so take the tile here whenever a
           parked hoover is sitting on dirt. Fresh arrivals are already
           cleaned by FinishRobotTileMove in the same frame, so this only
           catches the stuck cases. */
        if (!robots[i].moving &&
            map[robots[i].tileY][robots[i].tileX] == TILE_DIRT) {
            CleanTileForRobot(i, robots[i].tileX, robots[i].tileY);
        }
    }
    StepBonusAiFire();
    StepMainGameAiFire();
    StepPlayerBolts();
    StepBossBolts();
    if (bonusBossExplosionTicks > 0) bonusBossExplosionTicks--;
    if (empCountdownTicks > 0) {
        empCountdownTicks--;
        if (empCountdownTicks <= 0) {
            empCountdownOwner = -1;
            StopEmpPaletteCycle();
        }
    } else {
        StopEmpPaletteCycle();
    }
    if (lastPowerTicks > 0) lastPowerTicks--;

    for (i = 0; i < humanPlayers; i++) {
        ChoosePlayerMove(i);
    }

    for (i = humanPlayers; i < robotCount; i++) {
        ChooseAiMove(i);
    }

    for (i = 0; i < humanPlayers; i++) {
        if (!robots[i].moving && robots[i].battery <= 25) {
            robots[i].spriteIndex = SPR_LOW_BATTERY;
        } else if (!robots[i].moving && map[robots[i].tileY][robots[i].tileX] == TILE_DOCK) {
            robots[i].spriteIndex = SPR_CHARGING;
        }
    }

    ServiceHooverMoveSample();
    CheckEndState();
    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) StopGameplaySamples();
    FinishGameplayDirtyRects();
}

/* -------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------- */

static const UBYTE *MiniGlyph(char ch)
{
    static const UBYTE glyphs[37][5] = {
        {7,5,7,5,5}, {6,5,6,5,6}, {7,4,4,4,7}, {6,5,5,5,6}, {7,4,6,4,7}, {7,4,6,4,4},
        {7,4,5,5,7}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,7}, {5,5,6,5,5}, {4,4,4,4,7},
        {5,7,7,5,5}, {5,7,7,7,5}, {7,5,5,5,7}, {7,5,7,4,4}, {7,5,5,7,1}, {7,5,7,6,5},
        {7,4,7,1,7}, {7,2,2,2,2}, {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5},
        {5,5,2,2,2}, {7,1,2,4,7}, {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
        {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
        {0,0,0,0,0}
    };
    static const UBYTE colon[5] = {0,2,0,2,0};
    static const UBYTE slash[5] = {1,1,2,4,4};
    static const UBYTE dash[5] = {0,0,7,0,0};
    static const UBYTE leftArrow[5] = {1,2,4,2,1};
    static const UBYTE rightArrow[5] = {4,2,1,2,4};
    static const UBYTE period[5] = {0,0,0,0,2};
    static const UBYTE exclaim[5] = {2,2,2,0,2};
    static const UBYTE question[5] = {7,1,3,0,2};

    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch >= 'A' && ch <= 'Z') return glyphs[ch - 'A'];
    if (ch >= '0' && ch <= '9') return glyphs[26 + ch - '0'];
    if (ch == ':') return colon;
    if (ch == '/') return slash;
    if (ch == '-') return dash;
    if (ch == '<') return leftArrow;
    if (ch == '>') return rightArrow;
    if (ch == '.') return period;
    if (ch == '!') return exclaim;
    if (ch == '?') return question;
    return glyphs[36];
}

static WORD MiniTextWidth(const char *s, WORD scale)
{
    return (WORD)(strlen(s) * 4 * scale);
}

static void MiniCharScaled(struct RastPort *rp, WORD x, WORD y, char ch, UBYTE pen, WORD scale)
{
    const UBYTE *glyph = MiniGlyph(ch);
    WORD row;
    WORD col;

    SetAPen(rp, pen);
    for (row = 0; row < 5; row++) {
        for (col = 0; col < 3; col++) {
            if (glyph[row] & (1 << (2 - col))) {
                if (scale <= 1) {
                    WritePixel(rp, x + col, y + row);
                } else {
                    RectFill(rp, x + col * scale, y + row * scale,
                             x + col * scale + scale - 1, y + row * scale + scale - 1);
                }
            }
        }
    }
}

static void MiniTextScaled(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen, WORD scale)
{
    while (*s) {
        MiniCharScaled(rp, x, y, *s, pen, scale);
        x += 4 * scale;
        s++;
    }
}

static void MiniText(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen)
{
    MiniTextScaled(rp, x, y, s, pen, 1);
}

static void MiniTextCentered(struct RastPort *rp, WORD y, const char *s, UBYTE pen, WORD scale)
{
    MiniTextScaled(rp, (SCREEN_W - MiniTextWidth(s, scale)) / 2, y, s, pen, scale);
}


static void MiniTextCenteredIn(struct RastPort *rp, WORD left, WORD width, WORD y, const char *s, UBYTE pen, WORD scale)
{
    MiniTextScaled(rp, left + ((width - MiniTextWidth(s, scale)) / 2), y, s, pen, scale);
}

static void BuildRoundStartOverlay(WORD index, const char *label)
{
    WORD left = index * ROUND_START_OVERLAY_W;
    WORD right = left + ROUND_START_OVERLAY_W - 1;
    WORD bottom = ROUND_START_OVERLAY_H - 1;

    SetAPen(&roundOverlayRP, 1);
    RectFill(&roundOverlayRP, left, 0, right, bottom);
    SetAPen(&roundOverlayRP, 13);
    RectFill(&roundOverlayRP, left + 2, 2, right - 2, bottom - 2);
    SetAPen(&roundOverlayRP, 0);
    RectFill(&roundOverlayRP, left + 4, 4, right - 4, bottom - 4);

    MiniTextCenteredIn(&roundOverlayRP, left, ROUND_START_OVERLAY_W, 18, "GET-READY", 7, 3);
    MiniTextCenteredIn(&roundOverlayRP, left, ROUND_START_OVERLAY_W, 46, "HOOVERS LOCKED", 8, 1);
    if (label[0] == 'G') {
        MiniTextCenteredIn(&roundOverlayRP, left, ROUND_START_OVERLAY_W, 63, label, 10, 6);
    } else {
        MiniTextCenteredIn(&roundOverlayRP, left, ROUND_START_OVERLAY_W, 58, label, 10, 8);
    }
}

static BOOL InitRoundStartOverlayCache(void)
{
    static const char *labels[ROUND_START_OVERLAY_COUNT] = { "3", "2", "1", "GO" };
    WORD i;

    roundOverlayBM = AllocBitMap(ROUND_START_OVERLAY_W * ROUND_START_OVERLAY_COUNT,
                                 ROUND_START_OVERLAY_H, DEPTH,
                                 BMF_CLEAR | BMF_DISPLAYABLE,
                                 scr ? scr->RastPort.BitMap : NULL);
    if (!roundOverlayBM) return FALSE;

    InitRastPort(&roundOverlayRP);
    roundOverlayRP.BitMap = roundOverlayBM;

    for (i = 0; i < ROUND_START_OVERLAY_COUNT; i++) {
        BuildRoundStartOverlay(i, labels[i]);
    }
    return TRUE;
}

static void FreeRoundStartOverlayCache(void)
{
    if (roundOverlayBM) {
        FreeBitMap(roundOverlayBM);
        roundOverlayBM = NULL;
    }
    roundOverlayRP.BitMap = NULL;
}

static void DrawCachedTitleRobotSpinFrame(WORD variant, WORD frame, WORD dstX, WORD dstY)
{
    WORD srcX;

    if (!titleCarouselBM || !titleCarouselMaskBM || !titleCarouselMaskBM->Planes[0]) return;
    if (variant < 0 || variant >= ROBOT_VARIANTS || titleCarouselFrameCount <= 0) return;
    if (frame < 0) frame = 0;
    if (frame >= titleCarouselFrameCount) frame = titleCarouselFrameCount - 1;
    srcX = (variant * titleCarouselFrameCount + frame) * TITLE_ROT_W;

    BltMaskBitMapRastPort(titleCarouselBM, srcX, 0,
                          &renderRP, dstX, dstY,
                          TITLE_ROT_W, TITLE_ROT_H,
                          (ABC | ABNC | ANBC),
                          titleCarouselMaskBM->Planes[0]);
}

static void DrawCachedTitleRobotSpin(WORD variant, WORD phase, WORD dstX, WORD dstY)
{
    phase &= (TITLE_SPIN_STEPS - 1);
    DrawCachedTitleRobotSpinFrame(variant, titleCarouselPhaseFrame[phase], dstX, dstY);
}


/* The winner robot reuses the cached title-carousel spin frames.  The
 * carousel path blits these frames as a masked BOB; this helper scales the
 * same cached frames for the larger winner presentation.
 */

static void DrawCachedTitleRobotSpinScaled(WORD variant, WORD phase, WORD dstX, WORD dstY, WORD scale)
{
    WORD frame;
    WORD srcX;
    WORD px;
    WORD py;

    if (!titleCarouselBM || variant < 0 || variant >= ROBOT_VARIANTS || titleCarouselFrameCount <= 0) return;
    if (scale <= 1) {
        DrawCachedTitleRobotSpin(variant, phase, dstX, dstY);
        return;
    }

    phase &= (TITLE_SPIN_STEPS - 1);
    frame = (phase * titleCarouselFrameCount) / TITLE_SPIN_STEPS;
    if (frame >= titleCarouselFrameCount) frame = titleCarouselFrameCount - 1;
    srcX = (variant * titleCarouselFrameCount + frame) * TITLE_ROT_W;

    for (py = 0; py < TITLE_ROT_H; py++) {
        for (px = 0; px < TITLE_ROT_W; px++) {
            LONG pen = ReadPixel(&titleCarouselRP, srcX + px, py);
            if (pen <= 0) continue;
            SetAPen(&renderRP, (UBYTE)pen);
            RectFill(&renderRP,
                     dstX + px * scale,
                     dstY + py * scale,
                     dstX + ((px + 1) * scale) - 1,
                     dstY + ((py + 1) * scale) - 1);
        }
    }
}

static void DrawTitlePanelBase(WORD top, WORD bottom)
{
    SetAPen(&renderRP, TITLE_COPPER_PEN);
    RectFill(&renderRP, 0, top, SCREEN_W - 1, bottom);
}

static void DrawTitleSelectorBox(WORD x, WORD y)
{
    WORD left = x - 2;
    WORD top = y - 2;
    WORD right = x + (ROBOT_W * TITLE_ROBOT_SCALE) + 1;
    WORD bottom = y + (ROBOT_H * TITLE_ROBOT_SCALE) + 1;

    SetAPen(&renderRP, 13);
    RectFill(&renderRP, left - 2, top - 2, right + 2, bottom + 2);
    SetAPen(&renderRP, 0);
    RectFill(&renderRP, left, top, right, bottom);
}

static void DrawTitleRobotStatic(WORD variant, WORD state, WORD dstX, WORD dstY)
{
    WORD srcX;
    WORD px;
    WORD py;

    if (variant < 0 || variant >= ROBOT_VARIANTS) return;
    if (state < 0 || state >= SPR_STATE_COUNT) state = SPR_READY;

    if (robotScaledCacheBM) {
        srcX = (variant * SPR_STATE_COUNT + state) * ROBOT_SCALE2_W;
        BltBitMapRastPort(robotScaledCacheBM, srcX, 0,
                          &renderRP, dstX, dstY,
                          ROBOT_SCALE2_W, ROBOT_SCALE2_H,
                          0xC0);
        return;
    }

    if (!robotCacheBM) return;
    srcX = (variant * SPR_STATE_COUNT + state) * ROBOT_W;
    for (py = 0; py < ROBOT_H; py++) {
        for (px = 0; px < ROBOT_W; px++) {
            LONG pen = ReadPixel(&robotRP, srcX + px, py);
            if (pen <= 0) continue;
            SetAPen(&renderRP, (UBYTE)pen);
            RectFill(&renderRP,
                     dstX + (px * TITLE_ROBOT_SCALE),
                     dstY + (py * TITLE_ROBOT_SCALE),
                     dstX + ((px + 1) * TITLE_ROBOT_SCALE) - 1,
                     dstY + ((py + 1) * TITLE_ROBOT_SCALE) - 1);
        }
    }
}

static BOOL BuildTitleStaticCache(void)
{
    struct BitMap *oldBM;
    BOOL usingCache;
    char b[80];
    WORD slot;
    static const WORD slotX[ROBOT_VARIANTS] = {24, 64, 104, 144, 184, 224, 264};

    usingCache = AllocTitleStaticCache();
    if (usingCache && !titleStaticDirty && !titlePanelDirty) return TRUE;

    oldBM = renderRP.BitMap;
    if (usingCache) renderRP.BitMap = titleStaticBM;

    if (titleStaticDirty) {
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
        DrawHud();
        MiniTextCentered(&renderRP, 86, "A TINY AMIGA ROBOT CLEANER", 7, 2);
        MiniTextCentered(&renderRP, 108, "CLEAN MORE DIRT THAN", 9, 2);
        MiniTextCentered(&renderRP, 122, "THE AI ROBOTS", 9, 2);
        MiniTextCentered(&renderRP, 134, "EMP/DIRT NEED 5 15 30", 14, 2);
        snprintf(b, sizeof(b), "SPEED 4 %s", gameSpeedNames[gameSpeed]);
        MiniTextCentered(&renderRP, 62, b, 13, 2);
    } else {
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, 0, TITLE_DIRTY_TOP, SCREEN_W - 1, SCREEN_H - 1);
    }

    DrawTitlePanelBase(TITLE_CAROUSEL_Y - 8, SCREEN_H - 1);
    SetAPen(&renderRP, 8);
    RectFill(&renderRP, 0, TITLE_CAROUSEL_Y - 8, SCREEN_W - 1, TITLE_CAROUSEL_Y - 6);

    MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y - 24, "SELECT YOUR ROBOVAC", 7, 2);

    for (slot = 0; slot < ROBOT_VARIANTS; slot++) {
        WORD variant = selectedPlayerVariant[titleSelectPlayer] + slot - (ROBOT_VARIANTS / 2);
        WORD x = slotX[slot];
        WORD y = TITLE_CAROUSEL_Y;
        while (variant < 0) variant += ROBOT_VARIANTS;
        while (variant >= ROBOT_VARIANTS) variant -= ROBOT_VARIANTS;

        if (effectQuality == EFFECT_LOW &&
            (slot < (ROBOT_VARIANTS / 2) - 1 || slot > (ROBOT_VARIANTS / 2) + 1)) {
            continue;
        }

        if (slot == (ROBOT_VARIANTS / 2)) {
            DrawTitleSelectorBox(x, y);
        } else {
            SetAPen(&renderRP, 8);
            RectFill(&renderRP, x - 2, y - 2, x + (ROBOT_W * TITLE_ROBOT_SCALE) + 1, y + (ROBOT_H * TITLE_ROBOT_SCALE) + 1);
            SetAPen(&renderRP, 0);
            RectFill(&renderRP, x, y, x + (ROBOT_W * TITLE_ROBOT_SCALE) - 1, y + (ROBOT_H * TITLE_ROBOT_SCALE) - 1);
            MiniText(&renderRP, x + 10, y + 35, robotVariantTags[variant], 7);
        }
    }

    snprintf(b, sizeof(b), "P%d %s", titleSelectPlayer + 1, robotVariantNames[selectedPlayerVariant[titleSelectPlayer]]);
    MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y + 50, b, 7, 2);
    if (!titleTwoPlayerArmed) {
        MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y + 70, "HOLD FIRE START  BLUE SELECT", 13, 2);
    } else if (!titlePlayer2Locked) {
        MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y + 70, "P2 FIRE JOINS  HOLD LOCK", 13, 2);
    } else {
        MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y + 70, "HOLD FIRE START  BLUE SELECT", 13, 2);
    }

    renderRP.BitMap = oldBM;
    if (usingCache) {
        titleStaticDirty = FALSE;
        titlePanelDirty = FALSE;
    }
    WaitBlit();
    return usingCache;
}

static void DrawTitleCarousel(void)
{
    WORD slot;
    WORD baseFrame;
    static const WORD slotX[ROBOT_VARIANTS] = {24, 64, 104, 144, 184, 224, 264};

    AdvanceTitleCarouselSpin();
    baseFrame = titleCarouselPhaseFrame[titleSpinPhase & (TITLE_SPIN_STEPS - 1)];

    if (titleStaticBM) {
        BltBitMap(titleStaticBM, 0, TITLE_DIRTY_TOP,
                  renderBM, 0, TITLE_DIRTY_TOP,
                  SCREEN_W, SCREEN_H - TITLE_DIRTY_TOP,
                  0xC0, 0xFF, NULL);
    }

    for (slot = 0; slot < ROBOT_VARIANTS; slot++) {
        WORD variant = selectedPlayerVariant[titleSelectPlayer] + slot - (ROBOT_VARIANTS / 2);
        WORD x = slotX[slot];
        WORD y = TITLE_CAROUSEL_Y;
        while (variant < 0) variant += ROBOT_VARIANTS;
        while (variant >= ROBOT_VARIANTS) variant -= ROBOT_VARIANTS;

        if (effectQuality == EFFECT_LOW) {
            if (slot < (ROBOT_VARIANTS / 2) - 1 || slot > (ROBOT_VARIANTS / 2) + 1) continue;
            if (slot == (ROBOT_VARIANTS / 2)) {
                DrawCachedTitleRobotSpinFrame(variant, baseFrame, x, y);
            } else {
                DrawTitleRobotStatic(variant, SPR_READY, x, y);
            }
        } else {
            WORD phase = (titleSpinPhase + slot * 2) & (TITLE_SPIN_STEPS - 1);
            DrawCachedTitleRobotSpinFrame(variant, titleCarouselPhaseFrame[phase], x, y);
        }
    }
}

static void DrawRobotHealthStrip(void)
{
    WORD i;
    WORD slotW = SCREEN_W / MAX_ROBOTS;

    for (i = 0; i < robotCount; i++) {
        WORD x = i * slotW;
        WORD fill;
        char scoreText[4];
        UBYTE pen = robots[i].battery > 25 ? 13 : 12;

        SetAPen(&renderRP, (i < humanPlayers) ? 4 : 8);
        RectFill(&renderRP, x, 12, x + slotW - 2, 29);
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, x + 1, 13, x + slotW - 3, 28);

        MiniText(&renderRP, x + 2, 14, RobotTag(i), (i < humanPlayers) ? 14 : 7);
        snprintf(scoreText, sizeof(scoreText), "%d", robots[i].score);
        MiniText(&renderRP, x + 21, 14, scoreText, 13);
        if (robots[i].powerMovesLeft > 0) {
            MiniText(&renderRP, x + 2, 26, "P", 14);
        } else if (robots[i].cleanStreak > 0) {
            char streakText[8];
            snprintf(streakText, sizeof(streakText), "%d/%d", robots[i].cleanStreak, robots[i].powerCleanTarget);
            MiniText(&renderRP, x + 2, 26, streakText, 7);
        }

        SetAPen(&renderRP, 1);
        RectFill(&renderRP, x + 2, 21, x + slotW - 5, 25);
        fill = (robots[i].battery * (slotW - 8)) / maxBattery;
        if (fill < 0) fill = 0;
        if (fill > slotW - 8) fill = slotW - 8;
        if (fill > 0) {
            SetAPen(&renderRP, pen);
            RectFill(&renderRP, x + 3, 22, x + 2 + fill, 24);
        }
    }
}

static void DrawJoystickIcons(void)
{
    WORD i;

    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        WORD x = SCREEN_W - 38 + (i * 18);
        WORD y = 4;

        if (!joyEnabled[i]) continue;
        SetAPen(&renderRP, 4);
        RectFill(&renderRP, x, y, x + 14, y + 10);
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, x + 1, y + 1, x + 13, y + 9);
        MiniText(&renderRP, x + 3, y + 3, (i == 0) ? "J1" : "J2", 14);
    }
}


static void DrawRobotLarge(WORD robotId, WORD x, WORD y, WORD scale, WORD phase)
{
    WORD px;
    WORD py;
    WORD dx;
    WORD dy;
    WORD variant;
    WORD state;
    WORD srcX;

    if (!robotCacheBM || robotId < 0 || robotId >= robotCount) return;
    variant = robots[robotId].spriteVariant;
    if (variant >= ROBOT_VARIANTS) variant = 0;

    if (scale == 4 && titleCarouselBM && titleCarouselFrameCount > 0) {
        DrawCachedTitleRobotSpinScaled(variant, phase, x, y, 2);
        return;
    }

    state = SPR_DOWN;
    switch ((phase >> 3) & 3) {
        case 0: state = SPR_DOWN; break;
        case 1: state = SPR_RIGHT; break;
        case 2: state = SPR_UP; break;
        default: state = SPR_LEFT; break;
    }
    srcX = (variant * SPR_STATE_COUNT + state) * ROBOT_W;

    for (py = 0; py < ROBOT_H; py++) {
        for (px = 0; px < ROBOT_W; px++) {
            LONG pen = ReadPixel(&robotRP, srcX + px, py);
            if (pen <= 0) continue;
            SetAPen(&renderRP, (UBYTE)pen);
            dx = x + px * scale;
            dy = y + py * scale;
            RectFill(&renderRP, dx, dy, dx + scale - 1, dy + scale - 1);
        }
    }
}

static void DrawRobotIcon(WORD robotId, WORD x, WORD y, UBYTE state)
{
    WORD variant;
    WORD srcX;

    if (!robotCacheBM || !robotMaskBM || !robotMaskBM->Planes[0]) return;
    if (robotId < 0 || robotId >= robotCount) return;

    variant = robots[robotId].spriteVariant;
    if (variant >= ROBOT_VARIANTS) variant = 0;
    srcX = (variant * SPR_STATE_COUNT + state) * ROBOT_W;
    BltMaskBitMapRastPort(robotCacheBM, srcX, 0,
                          &renderRP, x, y,
                          ROBOT_W, ROBOT_H,
                          (ABC | ABNC | ANBC),
                          robotMaskBM->Planes[0]);
}

static void BuildRankOrder(WORD *order)
{
    WORD i;
    WORD pass;

    for (i = 0; i < robotCount; i++) order[i] = i;
    for (pass = 0; pass < robotCount - 1; pass++) {
        for (i = 0; i < robotCount - 1 - pass; i++) {
            WORD a = order[i];
            WORD b = order[i + 1];
            if (totalScores[b] > totalScores[a]) {
                order[i] = b;
                order[i + 1] = a;
            }
        }
    }
}

static void DrawLeaderboardScreen(BOOL finalBoard)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    char b[80];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    BuildRankOrder(order);
    finalWinner = order[0];
    titleSpinPhase = (titleSpinPhase + 1) & (TITLE_SPIN_STEPS - 1);

    MiniTextCentered(&renderRP, 8, finalBoard ? "FINAL SCORE BOARD" : "CONGRATS WINNER", finalBoard ? 7 : 14, 2);
    snprintf(b, sizeof(b), "1ST %s %s", RobotControlLabel(order[0]), RobotTag(order[0]));
    MiniTextCentered(&renderRP, 30, b, 13, 2);
    DrawRobotLarge(order[0], (SCREEN_W - ROBOT_W * 4) / 2, 48, 4, titleSpinPhase);

    if (robotCount > 1) {
        DrawRobotIcon(order[1], 12, 112, SPR_DOWN);
        snprintf(b, sizeof(b), "2ND %s %s  %d", RobotControlLabel(order[1]), RobotTag(order[1]), totalScores[order[1]]);
        MiniText(&renderRP, 30, 120, b, 7);
    }
    if (robotCount > 2) {
        DrawRobotIcon(order[2], 156, 112, SPR_DOWN);
        snprintf(b, sizeof(b), "3RD %s %s  %d", RobotControlLabel(order[2]), RobotTag(order[2]), totalScores[order[2]]);
        MiniText(&renderRP, 174, 120, b, 7);
    }

    MiniTextCentered(&renderRP, 136, "BOARD", 8, 1);
    for (i = 0; i < robotCount && i < 10; i++) {
        WORD id = order[i];
        WORD col = i / 5;
        WORD row = i % 5;
        WORD x = 8 + (col * 154);
        WORD y = 150 + row * 17;
        DrawRobotIcon(id, x, y - 8, SPR_DOWN);
        snprintf(b, sizeof(b), "%d %s %s P:%d W:%d", i + 1, RobotControlLabel(id), RobotTag(id), totalScores[id], roundWins[id]);
        MiniText(&renderRP, x + 18, y, b, (i == 0) ? 14 : 7);
    }

    if (!finalBoard && bonusAvailable) {
        MiniTextCentered(&renderRP, 238, "OVER 50! SPACE/FIRE BONUS", 13, 1);
    } else {
        MiniTextCentered(&renderRP, 238, "SPACE/FIRE TITLE", 13, 1);
    }
}

static void DrawBossExplosion(void)
{
    static const WORD dirs[8][2] = {
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
        {1, 0}, {-1, 1}, {0, 1}, {1, 1}
    };
    WORD i;
    WORD spread;
    WORD centerX;
    WORD centerY;

    if (gameState != GAME_BONUS_PLAYING || bonusBossExplosionTicks <= 0) return;

    spread = (BONUS_BOSS_EXPLOSION_TICKS - bonusBossExplosionTicks) / 2;
    centerX = bonusBossExplosionX + ((ROBOT_W * BONUS_BOSS_SCALE) / 2) - (ROBOT_W / 2);
    centerY = bonusBossExplosionY + ((ROBOT_H * BONUS_BOSS_SCALE) / 2) - (ROBOT_H / 2);

    SetAPen(&renderRP, 13);
    RectFill(&renderRP, centerX - spread, centerY - 2, centerX + ROBOT_W + spread, centerY + ROBOT_H + 2);
    SetAPen(&renderRP, 12);
    RectFill(&renderRP, centerX - 2, centerY - spread, centerX + ROBOT_W + 2, centerY + ROBOT_H + spread);

    for (i = 0; i < 8; i++) {
        DrawRobotIcon(finalWinner >= 0 ? finalWinner : 0,
                      centerX + dirs[i][0] * spread,
                      centerY + dirs[i][1] * spread,
                      (UBYTE)(SPR_DOWN + (i & 1)));
    }
}

static void DrawBonusBoss(void)
{
    WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
    WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;

    if (gameState != GAME_BONUS_PLAYING || bonusBossHealth <= 0) return;

    if (bonusBossCacheBM && bonusBossCacheMaskBM && bonusBossCacheMaskBM->Planes[0]) {
        BltMaskBitMapRastPort(bonusBossCacheBM, 0, 0,
                              &renderRP, bonusBossX, bonusBossY,
                              bossW, bossH,
                              (ABC | ABNC | ANBC),
                              bonusBossCacheMaskBM->Planes[0]);
    } else {
        DrawRobotLarge(finalWinner >= 0 ? finalWinner : 0, bonusBossX, bonusBossY, BONUS_BOSS_SCALE, bonusBossPhase);
    }

    if (dirtyBossHpText) {
        snprintf(cachedBossHpText, sizeof(cachedBossHpText), "BOSS HP:%d", bonusBossHealth);
        dirtyBossHpText = FALSE;
    }
    MiniTextCentered(&renderRP, 40, cachedBossHpText, 14, 1);
}

static void DrawHud(void)
{
    char b[160];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    DrawJoystickIcons();

    if (gameState == GAME_TITLE) {
        MiniTextCentered(&renderRP, 4, "ROBOVAC RESCUE", 7, 2);
        MiniTextCentered(&renderRP, 20, "ARROWS/J1 P1  P2 FIRE JOINS", 13, 2);
        MiniTextCentered(&renderRP, 32, "P2 Z/C/J2 SELECT  V LOCK", 13, 2);
        MiniTextCentered(&renderRP, 44, "SPACE/RMB/BLUE/HOLD START", 14, 2);
        return;
    }

    if (gameState == GAME_ROUND_END) {
        snprintf(b, sizeof(b), "ROUND WINNER: %s", RobotName(roundWinner));
        PutText(&renderRP, 14, 10, b, 13);
        DrawRobotHealthStrip();
        PutText(&renderRP, 62, 30, "Press Space/Fire for next round", 7);
        return;
    }

    if (gameState == GAME_MATCH_END || gameState == GAME_BONUS_END) {
        snprintf(b, sizeof(b), "MATCH WINNER: %s", RobotName(finalWinner));
        PutText(&renderRP, 42, 10, b, 12);
        DrawRobotHealthStrip();
        PutText(&renderRP, 72, 30, bonusAvailable ? "Space/Fire bonus" : "Space/Fire", 7);
        return;
    }

    if (lastPowerTicks > 0 && lastPowerText[0]) {
        PutText(&renderRP, 4, 8, lastPowerText, 14);
    } else {
        if (dirtyHudStatusText) {
            if (gameState == GAME_BONUS_PLAYING) {
                snprintf(cachedHudStatusText, sizeof(cachedHudStatusText),
                         "R6 BONUS BOSS:%d MOVE:%d", bonusBossHealth, batteryCostPerMove);
            } else {
                snprintf(cachedHudStatusText, sizeof(cachedHudStatusText),
                         "R%d %s DIRT:%d MOVE:%d", roundIndex + 1, roomNames[roomType], dirtLeft, batteryCostPerMove);
            }
            dirtyHudStatusText = FALSE;
        }
        PutText(&renderRP, 4, 8, cachedHudStatusText, 7);
    }
    DrawRobotHealthStrip();
}


static void DrawIntroTitleImage(void)
{
    WORD dstX;
    WORD dstY;
    WORD effectTick;

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    if (!introTitleBM) return;

    dstX = (SCREEN_W - introTitleW) / 2;
    dstY = (SCREEN_H - introTitleH) / 2;
    effectTick = introTicks - INTRO_HOLD_FRAMES;

    if (effectTick > 0 && effectQuality != EFFECT_LOW) {
        DrawIntroTitleBobEffect(dstX, dstY, effectTick);
        return;
    }

    BltBitMapRastPort(introTitleBM, 0, 0,
                      &renderRP, dstX, dstY,
                      introTitleW, introTitleH,
                      0xC0);
}


static void DrawRoundStartOverlay(void)
{
    WORD index;

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) return;
    if (roundCountdownTicks <= 0 && roundGoTicks <= 0) return;

    if (roundCountdownTicks <= 0) {
        MiniTextScaled(&renderRP, ROUND_GO_TEXT_LEFT, ROUND_GO_TEXT_TOP,
                       "GO", 10, ROUND_GO_TEXT_SCALE);
        return;
    }

    {
        WORD activeNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
        index = ROUND_COUNTDOWN_SECONDS - activeNumber;
    }

    if (index < 0) index = 0;
    if (index >= ROUND_START_OVERLAY_COUNT) index = ROUND_START_OVERLAY_COUNT - 1;

    if (roundOverlayBM) {
        BltBitMapRastPort(roundOverlayBM, index * ROUND_START_OVERLAY_W, 0,
                          &renderRP, ROUND_START_OVERLAY_LEFT, ROUND_START_OVERLAY_TOP,
                          ROUND_START_OVERLAY_W, ROUND_START_OVERLAY_H,
                          0xC0);
        return;
    }

    {
        char digit[2];
        WORD activeNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
        digit[0] = (char)('0' + activeNumber);
        digit[1] = '\0';
        MiniTextCentered(&renderRP, 88, "GET-READY", 7, 3);
        MiniTextCentered(&renderRP, 116, digit, 10, 8);
    }
}

static void DrawPauseMenu(void)
{
    static const char *items[2] = { "RESTART LEVEL", "MAIN MENU" };
    WORD left = 76;
    WORD top = 82;
    WORD right = 244;
    WORD bottom = 170;
    WORD i;

    SetAPen(&renderRP, 1);
    RectFill(&renderRP, left - 4, top - 4, right + 4, bottom + 4);
    SetAPen(&renderRP, 13);
    RectFill(&renderRP, left - 2, top - 2, right + 2, bottom + 2);
    SetAPen(&renderRP, 0);
    RectFill(&renderRP, left, top, right, bottom);

    MiniTextCentered(&renderRP, top + 10, "PAUSED", 7, 2);
    MiniTextCentered(&renderRP, top + 28, "Q/ESC CLOSE", 8, 1);

    for (i = 0; i < 2; i++) {
        WORD y = top + 46 + (i * 18);
        WORD textX = (SCREEN_W - MiniTextWidth(items[i], 2)) / 2;

        if (i == pauseMenuSelection) {
            SetAPen(&renderRP, 4);
            RectFill(&renderRP, left + 14, y - 4, right - 14, y + 13);
            MiniTextScaled(&renderRP, left + 20, y, ">", 14, 2);
            MiniTextScaled(&renderRP, right - 28, y, "<", 14, 2);
        }
        MiniTextScaled(&renderRP, textX, y, items[i], (i == pauseMenuSelection) ? 14 : 7, 2);
    }

    MiniTextCentered(&renderRP, bottom - 12, "ARROWS ENTER", 13, 1);
}


static void DrawAiSelectMenu(void)
{
    static const char *items[4] = { "0 AI", "1 AI", "2 AI", "3 AI" };
    WORD left = 76;
    WORD top = 70;
    WORD right = 244;
    WORD bottom = 188;
    WORD i;

    SetAPen(&renderRP, 1);
    RectFill(&renderRP, left - 4, top - 4, right + 4, bottom + 4);
    SetAPen(&renderRP, 13);
    RectFill(&renderRP, left - 2, top - 2, right + 2, bottom + 2);
    SetAPen(&renderRP, 0);
    RectFill(&renderRP, left, top, right, bottom);

    MiniTextCentered(&renderRP, top + 10, "TWO PLAYER AI", 7, 2);
    MiniTextCentered(&renderRP, top + 28, "HOW MANY RIVALS?", 8, 1);

    for (i = 0; i < 4; i++) {
        WORD y = top + 44 + (i * 16);
        WORD textX = (SCREEN_W - MiniTextWidth(items[i], 2)) / 2;

        if (i == aiSelectMenuSelection) {
            SetAPen(&renderRP, 4);
            RectFill(&renderRP, left + 14, y - 4, right - 14, y + 13);
            MiniTextScaled(&renderRP, left + 20, y, ">", 14, 2);
            MiniTextScaled(&renderRP, right - 28, y, "<", 14, 2);
        }
        MiniTextScaled(&renderRP, textX, y, items[i], (i == aiSelectMenuSelection) ? 14 : 7, 2);
    }

    MiniTextCentered(&renderRP, bottom - 12, "0-3 OR ARROWS ENTER", 13, 1);
}


static void DrawAiDifficultyMenu(void)
{
    static const char *items[3] = { "EASY", "NORMAL", "HARD" };
    WORD left = 64;
    WORD top = 76;
    WORD right = 256;
    WORD bottom = 184;
    WORD i;

    SetAPen(&renderRP, 1);
    RectFill(&renderRP, left - 4, top - 4, right + 4, bottom + 4);
    SetAPen(&renderRP, 10);
    RectFill(&renderRP, left - 2, top - 2, right + 2, bottom + 2);
    SetAPen(&renderRP, 0);
    RectFill(&renderRP, left, top, right, bottom);

    MiniTextCentered(&renderRP, top + 10, "AI DIFFICULTY", 7, 2);
    MiniTextCentered(&renderRP, top + 28, "HOW FAR CAN AI FIRE?", 8, 1);

    for (i = 0; i < 3; i++) {
        WORD y = top + 44 + (i * 16);
        WORD textX = (SCREEN_W - MiniTextWidth(items[i], 2)) / 2;

        if (i == aiDifficultyMenuSelection) {
            SetAPen(&renderRP, 4);
            RectFill(&renderRP, left + 14, y - 4, right - 14, y + 13);
            MiniTextScaled(&renderRP, left + 20, y, ">", 14, 2);
            MiniTextScaled(&renderRP, right - 28, y, "<", 14, 2);
        }
        MiniTextScaled(&renderRP, textX, y, items[i], (i == aiDifficultyMenuSelection) ? 14 : 7, 2);
    }

    MiniTextCentered(&renderRP, bottom - 12, "1-3/E-N-H OR ENTER", 13, 1);
}

#if USE_DIRTY_RECTS
static void RestoreDirtyRectFromRoom(struct DirtyRect *rect)
{
    if (!rect || rect->w <= 0 || rect->h <= 0) return;

    if (roomBM) {
        BltBitMap(roomBM, rect->x, rect->y,
                  renderBM, rect->x, rect->y,
                  rect->w, rect->h,
                  0xC0, 0xFF, NULL);
    } else {
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, rect->x, rect->y, rect->x + rect->w - 1, rect->y + rect->h - 1);
    }
}

static BOOL RobotIntersectsRect(WORD id, struct DirtyRect *rect)
{
    WORD x;
    WORD y;
    WORD w;
    WORD h;

    if (!rect || id < 0 || id >= robotCount) return FALSE;
    GetRobotDirtyBounds(robots[id].px, robots[id].py, id, &x, &y, &w, &h);
    return RectIntersects(rect->x, rect->y, rect->w, rect->h, x, y, w, h);
}

static void DrawRobotsIntersectingRect(struct DirtyRect *rect)
{
    WORD i;

    for (i = humanPlayers; i < robotCount; i++) {
        if (RobotIntersectsRect(i, rect)) DrawRobotBob(i);
    }
    for (i = 0; i < humanPlayers; i++) {
        if (RobotIntersectsRect(i, rect)) DrawRobotBob(i);
    }
}

static BOOL BoltIntersectsRect(struct Bolt *bolt, struct DirtyRect *rect)
{
    WORD x;
    WORD y;

    if (!bolt || !bolt->active || !rect) return FALSE;
    x = MAP_X + FP_TO_INT(bolt->px) - 1;
    y = MAP_Y + FP_TO_INT(bolt->py) - 1;
    return RectIntersects(rect->x, rect->y, rect->w, rect->h, x, y, ROBOT_W + 2, ROBOT_H + 2);
}

static void DrawBoltsIntersectingRect(struct DirtyRect *rect)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (BoltIntersectsRect(&playerBolts[i], rect)) DrawPlayerBolt(i);
    }
    for (i = 0; i < MAX_BOSS_BOLTS; i++) {
        if (BoltIntersectsRect(&bossBolts[i], rect)) DrawBoltSprite(&bossBolts[i]);
    }
}

static void DrawHudIfDirty(struct DirtyRect *rect)
{
    if (!rect) return;
    if (!RectIntersects(rect->x, rect->y, rect->w, rect->h, 0, 0, SCREEN_W, HUD_H)) return;
    DrawHud();
}

static BOOL BonusBossIntersectsRect(struct DirtyRect *rect)
{
    WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
    WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;

    if (!rect || gameState != GAME_BONUS_PLAYING || bonusBossHealth <= 0) return FALSE;
    if (RectIntersects(rect->x, rect->y, rect->w, rect->h, bonusBossX, bonusBossY, bossW, bossH)) return TRUE;
    return RectIntersects(rect->x, rect->y, rect->w, rect->h, 80, 36, 160, 14);
}

static BOOL BonusBossExplosionIntersectsRect(struct DirtyRect *rect)
{
    WORD spread;
    WORD centerX;
    WORD centerY;
    WORD left;
    WORD top;
    WORD sizeW;
    WORD sizeH;

    if (!rect || gameState != GAME_BONUS_PLAYING || bonusBossExplosionTicks <= 0) return FALSE;

    spread = (BONUS_BOSS_EXPLOSION_TICKS - bonusBossExplosionTicks) / 2;
    centerX = bonusBossExplosionX + ((ROBOT_W * BONUS_BOSS_SCALE) / 2) - (ROBOT_W / 2);
    centerY = bonusBossExplosionY + ((ROBOT_H * BONUS_BOSS_SCALE) / 2) - (ROBOT_H / 2);
    left = centerX - spread - 2;
    top = centerY - spread - 2;
    sizeW = ROBOT_W + (spread * 2) + 4;
    sizeH = ROBOT_H + (spread * 2) + 4;

    return RectIntersects(rect->x, rect->y, rect->w, rect->h, left, top, sizeW, sizeH);
}

static BOOL RoundGoOverlayIntersectsRect(struct DirtyRect *rect)
{
    if (!rect || roundGoTicks <= 0 || roundCountdownTicks > 0) return FALSE;
    return RectIntersects(rect->x, rect->y, rect->w, rect->h,
                          ROUND_GO_TEXT_LEFT, ROUND_GO_TEXT_TOP,
                          ROUND_GO_TEXT_W, ROUND_GO_TEXT_H);
}

static BOOL EmpRobotVisualIntersectsRect(WORD id, struct DirtyRect *rect)
{
    struct DirtyRect empRect;

    if (!rect) return FALSE;
    if (!GetEmpRobotVisualRect(id, &empRect)) return FALSE;
    return RectIntersects(rect->x, rect->y, rect->w, rect->h,
                          empRect.x, empRect.y, empRect.w, empRect.h);
}

static void DrawEmpRobotVisualsIntersectingRect(struct DirtyRect *rect)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (EmpRobotVisualIntersectsRect(i, rect)) DrawEmpRobotVisual(i);
    }
}

static void DrawGameplayDirtyOverlays(struct DirtyRect *rect)
{
    if (RoundGoOverlayIntersectsRect(rect)) DrawRoundStartOverlay();
    DrawEmpRobotVisualsIntersectingRect(rect);
}

static void DrawGameplayDirtyRects(void)
{
    WORD i;

    if (!DirtyGameplayRectsReady()) return;

    for (i = 0; i < dirtyRectCount; i++) {
        struct DirtyRect rect = dirtyRects[i];
        if (rect.x >= SCREEN_W || rect.y >= SCREEN_H || rect.w <= 0 || rect.h <= 0) continue;
        if (rect.x + rect.w > SCREEN_W) rect.w = SCREEN_W - rect.x;
        if (rect.y + rect.h > SCREEN_H) rect.h = SCREEN_H - rect.y;

        RestoreDirtyRectFromRoom(&rect);
        DrawHudIfDirty(&rect);
        DrawRobotsIntersectingRect(&rect);
        DrawBoltsIntersectingRect(&rect);
        if (BonusBossExplosionIntersectsRect(&rect)) DrawBossExplosion();
        if (BonusBossIntersectsRect(&rect)) DrawBonusBoss();
        DrawGameplayDirtyOverlays(&rect);
    }
}
#endif

static void DrawFrame(void)
{
    WORD i;

    if (gameState == GAME_INTRO) {
        DisableTitleCopperGradient(TRUE);
        DrawIntroTitleImage();
        return;
    }

    if (gameState == GAME_MATCH_END) {
        DrawLeaderboardScreen(FALSE);
        return;
    }

    if (gameState == GAME_BONUS_END) {
        DrawLeaderboardScreen(TRUE);
        return;
    }

    if (gameState == GAME_TITLE) {
        BuildTitleStaticCache();
        if (titleStaticBM && titleFullPresentPending) {
            BltBitMap(titleStaticBM, 0, 0,
                      renderBM, 0, 0,
                      SCREEN_W, SCREEN_H,
                      0xC0, 0xFF, NULL);
        }
        if ((aiSelectMenuOpen || aiDifficultyMenuOpen) && titleStaticBM) {
            BltBitMap(titleStaticBM, 0, TITLE_MENU_DIRTY_TOP,
                      renderBM, 0, TITLE_MENU_DIRTY_TOP,
                      SCREEN_W, SCREEN_H - TITLE_MENU_DIRTY_TOP,
                      0xC0, 0xFF, NULL);
        }
        DrawTitleCarousel();
        if (aiSelectMenuOpen) {
            DrawAiSelectMenu();
        }
        if (aiDifficultyMenuOpen) {
            DrawAiDifficultyMenu();
        }
        return;
    }

    DisableTitleCopperGradient(TRUE);

#if USE_DIRTY_RECTS
    if (gameState == GAME_PLAYING || gameState == GAME_BONUS_PLAYING) {
        if (DirtyGameplayNoChanges()) {
            return;
        }
        if (DirtyGameplayRectsReady()) {
            DrawGameplayDirtyRects();
            return;
        }
    }
#endif

    if (roomBM) {
        BltBitMap(roomBM, 0, 0,
                  renderBM, 0, 0,
                  SCREEN_W, SCREEN_H,
                  0xC0, 0xFF, NULL);
    } else {
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    }

    DrawHud();

    for (i = humanPlayers; i < robotCount; i++) {
        DrawRobotBob(i);
    }
    for (i = 0; i < humanPlayers; i++) {
        DrawRobotBob(i);
    }
    for (i = 0; i < robotCount; i++) {
        DrawPlayerBolt(i);
    }

    DrawBossBolts();
    DrawBossExplosion();
    DrawBonusBoss();
    DrawRoundStartOverlay();
    DrawEmpRobotVisuals();

    if (pauseMenuOpen) {
        DrawPauseMenu();
    }
}

static void PresentFullFrame(void)
{
    BltBitMap(renderBM, 0, 0,
              scr->RastPort.BitMap, 0, 0,
              SCREEN_W, SCREEN_H,
              0xC0, 0xFF, NULL);
}

#if USE_DIRTY_RECTS
static BOOL DirtyGameplayRectsReady(void)
{
    /*
     * Dirty gameplay rect collection is presentation-only.  If a frame has
     * not produced a complete rect set by the time the stable WaitTOF-paced
     * loop reaches presentation, the frame is still presented exactly once by
     * falling back to the same full-frame blit used by stable main.  This keeps
     * StepGame() on the one-tick-per-TOF cadence instead of letting the dirty
     * renderer skip, replay, or add simulation ticks while waiting for rects.
     */
    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) return FALSE;
    if (!dirtyRectsBuiltForFrame) return FALSE;
    if (!dirtyRectsValid) return FALSE;
    if (dirtyRectCount <= 0) return FALSE;
    if (dirtyForceFullFrame) return FALSE;
    if (roundCountdownTicks > 0 || pauseMenuOpen) return FALSE;
    return TRUE;
}


static BOOL DirtyGameplayNoChanges(void)
{
    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) return FALSE;
    if (!dirtyRectsBuiltForFrame) return FALSE;
    if (!dirtyRectsValid) return FALSE;
    if (dirtyForceFullFrame) return FALSE;
    if (dirtyRectCount > 0) return FALSE;
    if (roundCountdownTicks > 0 || pauseMenuOpen) return FALSE;
    return TRUE;
}

static void PresentDirtyGameplayRects(void)
{
    WORD i;

    for (i = 0; i < dirtyRectCount; i++) {
        struct DirtyRect rect = dirtyRects[i];
        if (rect.x >= SCREEN_W || rect.y >= SCREEN_H || rect.w <= 0 || rect.h <= 0) continue;
        if (rect.x + rect.w > SCREEN_W) rect.w = SCREEN_W - rect.x;
        if (rect.y + rect.h > SCREEN_H) rect.h = SCREEN_H - rect.y;
        BltBitMap(renderBM, rect.x, rect.y,
                  scr->RastPort.BitMap, rect.x, rect.y,
                  rect.w, rect.h,
                  0xC0, 0xFF, NULL);
    }
    dirtyFrameCount++;
    dirtyRectTotal += dirtyRectCount;
#if DIRTY_RECT_DEBUG_PRINTF
    if ((dirtyFrameCount & 0x3F) == 0) {
        printf("dirty pre:%d post:%d draw:%d area:%ld avgrect:%ld frames:%ld strips:%ld fallback full:%ld\n",
               dirtyRectPreMergeCount, dirtyRectPostMergeCount, dirtyRectCount,
               (LONG)dirtyRectLastArea,
               dirtyFrameCount ? (LONG)(dirtyRectTotal / dirtyFrameCount) : 0,
               (LONG)dirtyFrameCount, (LONG)dirtyStripFrameCount, (LONG)fallbackFullFrameCount);
    }
#endif
}

static void PresentDirtyGameplayFrame(void)
{
    if (DirtyGameplayNoChanges()) {
        dirtyRectsBuiltForFrame = FALSE;
        return;
    }

    if (!DirtyGameplayRectsReady()) {
        fallbackFullFrameCount++;
#if DIRTY_RECT_DEBUG_PRINTF
        printf("dirty fallback full:%ld rects:%d valid:%d forced:%d built:%d\n",
               (LONG)fallbackFullFrameCount, dirtyRectCount, dirtyRectsValid, dirtyForceFullFrame, dirtyRectsBuiltForFrame);
#endif
        PresentFullFrame();
        dirtyForceFullFrame = FALSE;
        dirtyRectsBuiltForFrame = FALSE;
        return;
    }

    PresentDirtyGameplayRects();
    dirtyRectsBuiltForFrame = FALSE;
}
#endif

static void PresentFrame(void)
{
    WORD dirtyTop;

    if (gameState == GAME_TITLE) {
        WaitBlit();
    }

    if (gameState == GAME_TITLE && !titleFullPresentPending && titleFullPresentFrames <= 0) {
        dirtyTop = (aiSelectMenuOpen || aiDifficultyMenuOpen) ? TITLE_MENU_DIRTY_TOP : TITLE_DIRTY_TOP;
        BltBitMap(renderBM, 0, dirtyTop,
                  scr->RastPort.BitMap, 0, dirtyTop,
                  SCREEN_W, SCREEN_H - dirtyTop,
                  0xC0, 0xFF, NULL);
        return;
    }

#if USE_DIRTY_RECTS
    if (gameState == GAME_PLAYING || gameState == GAME_BONUS_PLAYING) {
        PresentDirtyGameplayFrame();
    } else {
        PresentFullFrame();
    }
#else
    PresentFullFrame();
#endif

    if (gameState == GAME_TITLE) {
        if (titleFullPresentFrames > 0) titleFullPresentFrames--;
        titleFullPresentPending = (titleFullPresentFrames > 0) ? TRUE : FALSE;
        titleFirstFullPresentDone = TRUE;
    }
}

/* -------------------------------------------------------------------------
 * Input
 * ------------------------------------------------------------------------- */


static void ClearMovementKeys(void)
{
    WORD i;
    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        keyLeft[i] = FALSE;
        keyRight[i] = FALSE;
        keyUp[i] = FALSE;
        keyDown[i] = FALSE;
        joyLeft[i] = FALSE;
        joyRight[i] = FALSE;
        joyUp[i] = FALSE;
        joyDown[i] = FALSE;
    }
}

static void OpenPauseMenu(void)
{
    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) return;
    pauseMenuOpen = TRUE;
    pauseMenuSelection = 0;
    ForceGameplayFullPresent();
    ClearMovementKeys();
}

static void ClosePauseMenu(void)
{
    pauseMenuOpen = FALSE;
    pauseMenuSelection = 0;
    ForceGameplayFullPresent();
    ClearMovementKeys();
}

static void OpenAiSelectMenu(WORD initialSelection)
{
    if (gameState != GAME_TITLE) return;
    if (initialSelection < 0) initialSelection = 0;
    if (initialSelection > 3) initialSelection = 3;
    humanPlayers = 2;
    titleTwoPlayerArmed = TRUE;
    titlePlayer2Locked = TRUE;
    titleSelectPlayer = 0;
    aiSelectMenuOpen = TRUE;
    aiSelectMenuSelection = initialSelection;
    ClearMovementKeys();
    MarkTitleAllDirty();
}

static void CloseAiSelectMenu(void)
{
    aiSelectMenuOpen = FALSE;
    aiSelectMenuSelection = 0;
    ClearMovementKeys();
    MarkTitleAllDirty();
}

static void CloseAiDifficultyMenu(void)
{
    aiDifficultyMenuOpen = FALSE;
    aiDifficultyMenuSelection = aiDifficulty;
    aiDifficultyPendingPlayers = 1;
    aiDifficultyPendingRivals = 0;
    ClearMovementKeys();
    MarkTitleAllDirty();
}

static void OpenAiDifficultyMenu(WORD players, WORD rivals)
{
    if (gameState != GAME_TITLE) return;
    if (rivals <= 0) {
        StartMatch(players, rivals);
        return;
    }
    aiSelectMenuOpen = FALSE;
    aiDifficultyPendingPlayers = players;
    aiDifficultyPendingRivals = rivals;
    aiDifficultyMenuSelection = aiDifficulty;
    aiDifficultyMenuOpen = TRUE;
    ClearMovementKeys();
    MarkTitleAllDirty();
}

static void ActivateAiDifficultyMenu(void)
{
    WORD players = aiDifficultyPendingPlayers;
    WORD rivals = aiDifficultyPendingRivals;

    aiDifficulty = aiDifficultyMenuSelection;
    CloseAiDifficultyMenu();
    StartMatch(players, rivals);
}

static void ActivateAiSelectMenu(void)
{
    WORD rivals = aiSelectMenuSelection;
    CloseAiSelectMenu();
    OpenAiDifficultyMenu(2, rivals);
}

static BOOL HandleAiDifficultyMenuRawKey(UWORD code, BOOL keyUpEvent)
{
    if (!aiDifficultyMenuOpen) return FALSE;
    if (keyUpEvent) return TRUE;

    if (code == RAW_Q || code == RAW_ESC) {
        CloseAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_UP) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 2) % 3;
        return TRUE;
    }
    if (code == RAW_DOWN) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 1) % 3;
        return TRUE;
    }
    if (code == RAW_1 || code == RAW_E) {
        aiDifficultyMenuSelection = 0;
        ActivateAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_2 || code == RAW_N) {
        aiDifficultyMenuSelection = 1;
        ActivateAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_3 || code == RAW_H) {
        aiDifficultyMenuSelection = 2;
        ActivateAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_RETURN || code == RAW_SPACE) {
        ActivateAiDifficultyMenu();
        return TRUE;
    }

    return TRUE;
}

static BOOL HandleAiSelectMenuRawKey(UWORD code, BOOL keyUpEvent)
{
    if (!aiSelectMenuOpen) return FALSE;
    if (keyUpEvent) return TRUE;

    if (code == RAW_Q || code == RAW_ESC) {
        CloseAiSelectMenu();
        return TRUE;
    }
    if (code == RAW_UP) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 3) & 3;
        return TRUE;
    }
    if (code == RAW_DOWN) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 1) & 3;
        return TRUE;
    }
    if (code == RAW_0 || code == RAW_1 || code == RAW_2 || code == RAW_3) {
        aiSelectMenuSelection = (code == RAW_0) ? 0 : (WORD)code;
        ActivateAiSelectMenu();
        return TRUE;
    }
    if (code == RAW_RETURN || code == RAW_SPACE) {
        ActivateAiSelectMenu();
        return TRUE;
    }

    return TRUE;
}

static void ActivatePauseMenuSelection(void)
{
    if (pauseMenuSelection == 0) {
        if (gameState == GAME_BONUS_PLAYING) StartBonusRound();
        else ResetLevel();
    } else {
        ClosePauseMenu();
        EnterTitleScreen();
    }
}

static BOOL HandlePauseMenuRawKey(UWORD code, BOOL keyUpEvent)
{
    if (!pauseMenuOpen) return FALSE;
    if (keyUpEvent) return TRUE;

    if (code == RAW_Q || code == RAW_ESC) {
        ClosePauseMenu();
        return TRUE;
    }
    if (code == RAW_UP || code == RAW_DOWN) {
        pauseMenuSelection = 1 - pauseMenuSelection;
        return TRUE;
    }
    if (code == RAW_RETURN || code == RAW_SPACE) {
        ActivatePauseMenuSelection();
        return TRUE;
    }

    return TRUE;
}

static void StartMatch(WORD players, WORD rivals)
{
    WORD i;
    StopMenuMusic();
    humanPlayers = players;
    if (humanPlayers < 1) humanPlayers = 1;
    if (humanPlayers > MAX_HUMAN_PLAYERS) humanPlayers = MAX_HUMAN_PLAYERS;
    aiRivals = rivals;
    if (aiRivals < 0) aiRivals = 0;
    if (aiRivals > MAX_ROBOTS - humanPlayers) aiRivals = MAX_ROBOTS - humanPlayers;
    roundIndex = 0;
    MarkHudStatusTextDirty();
    bonusAvailable = FALSE;
    bonusBossHealth = 0;
    for (i = 0; i < MAX_ROBOTS; i++) { roundWins[i] = 0; totalScores[i] = 0; }
    titleTwoPlayerArmed = FALSE;
    titlePlayer2Locked = FALSE;
    titleSelectPlayer = 0;
    aiDifficultyMenuOpen = FALSE;
    ResetAllJoystickConfirmHolds();
    ResetLevel();
}

/* Attract mode: 4 AI-only robots clean a random room by themselves, Easy
 * difficulty so they don't shoot at each other. Any real key, joystick, or
 * mouse input immediately drops back to the title screen (see HandleRawKey/
 * PollJoysticks/PollWindowMessages); a finished round just starts another
 * one via the demoModeActive branch in CheckEndState rather than showing
 * the normal round/match-end screens. */
static void StartDemoMode(void)
{
    WORD i;

    StopMenuMusic();
    demoModeActive = TRUE;
    /* The first joystick poll after D/idle start establishes a baseline.
     * Only a new edge after that baseline is allowed to cancel the demo. */
    demoJoyPrimed = FALSE;
    humanPlayers = 0;
    aiRivals = 4;
    aiDifficulty = 0;
    roundIndex = 0;
    MarkHudStatusTextDirty();
    bonusAvailable = FALSE;
    bonusBossHealth = 0;
    for (i = 0; i < MAX_ROBOTS; i++) { roundWins[i] = 0; totalScores[i] = 0; }
    titleTwoPlayerArmed = FALSE;
    titlePlayer2Locked = FALSE;
    titleSelectPlayer = 0;
    aiDifficultyMenuOpen = FALSE;
    aiSelectMenuOpen = FALSE;
    ResetAllJoystickConfirmHolds();
    /* The menu-driven match-start paths (ActivateAiDifficultyMenu/
     * ActivateAiSelectMenu) always mark the title screen fully dirty before
     * handing off to gameplay, via their CloseAiDifficultyMenu/
     * CloseAiSelectMenu calls. D is a direct shortcut with no menu to close,
     * so without this the title screen's cached bitmap could still be
     * flagged clean and never get overdrawn, leaving it visible under/after
     * the demo starts. */
    MarkTitleAllDirty();
    ResetLevel();
}

static void StartWithRivals(WORD rivals)
{
    if (gameState == GAME_TITLE && titleTwoPlayerArmed && titlePlayer2Locked) {
        OpenAiSelectMenu(rivals);
        return;
    }
    OpenAiDifficultyMenu(humanPlayers, rivals);
}

static void TitleChooseVariant(WORD playerId, WORD delta)
{
    if (playerId < 0 || playerId >= MAX_HUMAN_PLAYERS) return;
    selectedPlayerVariant[playerId] += delta;
    while (selectedPlayerVariant[playerId] < 0) selectedPlayerVariant[playerId] += ROBOT_VARIANTS;
    while (selectedPlayerVariant[playerId] >= ROBOT_VARIANTS) selectedPlayerVariant[playerId] -= ROBOT_VARIANTS;
    MarkTitlePanelDirty();
}

static void TitleArmTwoPlayer(void)
{
    humanPlayers = 2;
    titleTwoPlayerArmed = TRUE;
    titlePlayer2Locked = FALSE;
    titleSelectPlayer = 1;
    MarkTitlePanelDirty();
}

static void TitleLockPlayer2(void)
{
    if (!titleTwoPlayerArmed) {
        TitleArmTwoPlayer();
        return;
    }
    titlePlayer2Locked = TRUE;
    titleSelectPlayer = 0;
    MarkTitlePanelDirty();
    OpenAiSelectMenu(aiRivals);
}

static void TitlePlayer2Fire(void)
{
    if (!titleTwoPlayerArmed || titlePlayer2Locked) {
        TitleArmTwoPlayer();
    } else {
        TitleLockPlayer2();
    }
}

static void FireRobotBolt(WORD id, WORD dirX, WORD dirY, BOOL useBattery, BOOL playSound)
{
    struct Bolt *bolt;

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) return;
    if (RoundStartLocked()) return;
    if (id < 0 || id >= robotCount) return;
    if (robots[id].stunTicks > 0) return;
    if (dirX == 0 && dirY == 0) dirY = -1;

    bolt = &playerBolts[id];
    if (bolt->active) return;
    if (useBattery && robots[id].battery < 2 && !(robots[id].powerType == POWER_BOLT && robots[id].powerMovesLeft > 0)) return;

    if (useBattery && !(robots[id].powerType == POWER_BOLT && robots[id].powerMovesLeft > 0)) {
        robots[id].battery -= 2;
    }

    bolt->active = TRUE;
    bolt->dirX = dirX;
    bolt->dirY = dirY;
    bolt->px = TO_FP(robots[id].tileX * TILE_SIZE);
    bolt->py = TO_FP(robots[id].tileY * TILE_SIZE);
    bolt->ttl = (gameState == GAME_BONUS_PLAYING) ? 42 : 24;
    if (playSound) PlayBoltFireSample();
}

/* Holding two perpendicular directions at once (e.g. up+right) fires a 45
 * degree bolt instead of picking just one axis, the way the boss's own
 * bolts already travel on all 8 headings (see FireBossBolt/StepPlayerBolt).
 * Grid movement itself stays 4-directional (StartRobotMove/ChoosePlayerMove
 * are unaffected); this only changes which way a stationary shot flies. */
static void FirePlayerBolt(WORD id)
{
    WORD dirX = 0, dirY = 0;

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) return;
    if (RoundStartLocked()) return;
    if (id < 0 || id >= humanPlayers || id >= MAX_HUMAN_PLAYERS) return;

    /* Check currently-held input first, on both axes independently, so
     * holding two perpendicular directions at once (e.g. continuing left
     * while also holding up) fires diagonally even though grid movement
     * itself only ever travels one axis at a time. Only fall back to the
     * in-progress move's axis or last facing when nothing is held right
     * now (e.g. fire pressed the instant direction keys are released). */
    if (keyLeft[id] || joyLeft[id]) dirX = -1;
    else if (keyRight[id] || joyRight[id]) dirX = 1;

    if (keyUp[id] || joyUp[id]) dirY = -1;
    else if (keyDown[id] || joyDown[id]) dirY = 1;

    if (dirX == 0 && dirY == 0) {
        if (robots[id].moving) {
            dirX = robots[id].targetX - robots[id].tileX;
            dirY = robots[id].targetY - robots[id].tileY;
        } else {
            dirX = playerFacingX[id];
            dirY = playerFacingY[id];
        }
    }
    if (dirX == 0 && dirY == 0) dirY = -1;

    playerFacingX[id] = dirX;
    playerFacingY[id] = dirY;
    FireRobotBolt(id, dirX, dirY, TRUE, TRUE);
}

static void StepPlayerBolt(WORD ownerId)
{
    WORD tx, ty, i;
    struct Bolt *bolt;

    if (ownerId < 0 || ownerId >= robotCount) return;
    bolt = &playerBolts[ownerId];
    if (!bolt->active) return;
    if (bolt->ttl-- <= 0) { bolt->active = FALSE; return; }

    bolt->px += bolt->dirX * (5 * FP_ONE);
    bolt->py += bolt->dirY * (5 * FP_ONE);
    tx = FP_TO_INT(bolt->px) / TILE_SIZE;
    ty = FP_TO_INT(bolt->py) / TILE_SIZE;

    if (IsBlocked(tx, ty)) { bolt->active = FALSE; return; }

    if (gameState == GAME_BONUS_PLAYING && bonusBossHealth > 0) {
        WORD bossW = ROBOT_W * BONUS_BOSS_SCALE;
        WORD bossH = ROBOT_H * BONUS_BOSS_SCALE;
        WORD boltX = MAP_X + FP_TO_INT(bolt->px);
        WORD boltY = MAP_Y + FP_TO_INT(bolt->py);
        if (boltX >= bonusBossX && boltX <= bonusBossX + bossW &&
            boltY >= bonusBossY && boltY <= bonusBossY + bossH) {
            bonusBossHealth--;
            MarkBossHpTextDirty();
            MarkHudStatusTextDirty();
            robots[ownerId].score += BONUS_BOSS_HIT_POINTS;
            totalScores[ownerId] += BONUS_BOSS_HIT_POINTS;
            snprintf(lastPowerText, sizeof(lastPowerText), "%s BOSS HIT +%d HP:%d", RobotTag(ownerId), BONUS_BOSS_HIT_POINTS, bonusBossHealth);
            lastPowerTicks = 80;
            bolt->active = FALSE;
            if (bonusBossHealth <= 0) StartBonusBossExplosion();
            return;
        }
    }

    if (gameState == GAME_BONUS_PLAYING) return;

    for (i = 0; i < robotCount; i++) {
        if (i == ownerId) continue;
        if (AbsW(robots[i].tileX - tx) + AbsW(robots[i].tileY - ty) <= 1) {
            if (ApplyBoltStun(i, BOLT_STUN_DAMAGE)) {
                robots[ownerId].score += BONUS_BOSS_HIT_POINTS;
                snprintf(lastPowerText, sizeof(lastPowerText), "%s BOLT +%d", RobotTag(ownerId), BONUS_BOSS_HIT_POINTS);
                lastPowerTicks = 80;
            }
            bolt->active = FALSE;
            return;
        }
    }
}

static void StepPlayerBolts(void)
{
    WORD i;
    for (i = 0; i < robotCount; i++) {
        StepPlayerBolt(i);
    }
}

static void FireAiBoltAtBoss(WORD id)
{
    WORD robotX;
    WORD robotY;
    WORD bossCenterX;
    WORD bossCenterY;
    WORD dx;
    WORD dy;

    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].stunTicks > 0) return;

    robotX = robots[id].tileX * TILE_SIZE + (ROBOT_W / 2);
    robotY = robots[id].tileY * TILE_SIZE + (ROBOT_H / 2);
    bossCenterX = (bonusBossX - MAP_X) + ((ROBOT_W * BONUS_BOSS_SCALE) / 2);
    bossCenterY = (bonusBossY - MAP_Y) + ((ROBOT_H * BONUS_BOSS_SCALE) / 2);
    dx = bossCenterX - robotX;
    dy = bossCenterY - robotY;

    if (AbsW(dx) >= AbsW(dy)) {
        FireRobotBolt(id, (dx < 0) ? -1 : 1, 0, FALSE, TRUE);
    } else {
        FireRobotBolt(id, 0, (dy < 0) ? -1 : 1, FALSE, TRUE);
    }
}

static void StepBonusAiFire(void)
{
    static WORD nextAiShooter = 0;
    WORD tries;

    if (gameState != GAME_BONUS_PLAYING || bonusBossHealth <= 0 || bonusBossExplosionTicks > 0) return;
    if (humanPlayers >= robotCount) return;

    if (bonusAiFireTicks > 0) bonusAiFireTicks--;
    if (bonusAiFireTicks > 0) return;
    bonusAiFireTicks = BONUS_AI_FIRE_INTERVAL_TICKS;

    if (nextAiShooter < humanPlayers || nextAiShooter >= robotCount) nextAiShooter = humanPlayers;
    for (tries = 0; tries < robotCount; tries++) {
        WORD id = nextAiShooter;
        nextAiShooter++;
        if (nextAiShooter >= robotCount) nextAiShooter = humanPlayers;
        if (id >= humanPlayers && id < robotCount && !playerBolts[id].active) {
            FireAiBoltAtBoss(id);
            return;
        }
    }
}

static BOOL ClearBoltLane(WORD sx, WORD sy, WORD tx, WORD ty)
{
    WORD dx = 0;
    WORD dy = 0;
    WORD x;
    WORD y;

    if (sx == tx) dy = (ty > sy) ? 1 : -1;
    else if (sy == ty) dx = (tx > sx) ? 1 : -1;
    else return FALSE;

    x = sx + dx;
    y = sy + dy;
    while (x != tx || y != ty) {
        if (IsBlocked(x, y)) return FALSE;
        x += dx;
        y += dy;
    }

    return TRUE;
}

static void StepMainGameAiFire(void)
{
    static WORD nextAiShooter = 0;
    WORD tries;
    WORD fireRange;

    if (gameState != GAME_PLAYING || RoundStartLocked()) return;
    if (humanPlayers >= robotCount) return;
    if (aiDifficulty <= 0) return;

    fireRange = (aiDifficulty >= 2) ? MAIN_AI_FIRE_RANGE_HARD : MAIN_AI_FIRE_RANGE_NORMAL;

    if (nextAiShooter < humanPlayers || nextAiShooter >= robotCount) nextAiShooter = humanPlayers;
    for (tries = 0; tries < robotCount; tries++) {
        WORD id = nextAiShooter;
        WORD target = -1;
        WORD bestDist = fireRange + 1;
        WORD j;

        nextAiShooter++;
        if (nextAiShooter >= robotCount) nextAiShooter = humanPlayers;

        if (id < humanPlayers || id >= robotCount) continue;
        if (robots[id].stunTicks > 0 || playerBolts[id].active) continue;
        if (robots[id].battery < 2 && !(robots[id].powerType == POWER_BOLT && robots[id].powerMovesLeft > 0)) continue;
        if (RandRange(MAIN_AI_FIRE_CHANCE) != 0) continue;

        for (j = 0; j < robotCount; j++) {
            WORD dist;
            if (j == id) continue;
            if (robots[j].tileX != robots[id].tileX && robots[j].tileY != robots[id].tileY) continue;
            dist = AbsW(robots[j].tileX - robots[id].tileX) + AbsW(robots[j].tileY - robots[id].tileY);
            if (dist <= 0 || dist > fireRange || dist >= bestDist) continue;
            if (!ClearBoltLane(robots[id].tileX, robots[id].tileY, robots[j].tileX, robots[j].tileY)) continue;
            target = j;
            bestDist = dist;
        }

        if (target >= 0) {
            WORD dirX = 0;
            WORD dirY = 0;
            if (robots[target].tileX < robots[id].tileX) dirX = -1;
            else if (robots[target].tileX > robots[id].tileX) dirX = 1;
            else if (robots[target].tileY < robots[id].tileY) dirY = -1;
            else dirY = 1;
            FireRobotBolt(id, dirX, dirY, TRUE, TRUE);
            return;
        }
    }
}

static void StepBossBolts(void)
{
    WORD i;

    if (gameState != GAME_BONUS_PLAYING) return;

    for (i = 0; i < MAX_BOSS_BOLTS; i++) {
        WORD tx;
        WORD ty;
        WORD j;
        struct Bolt *bolt = &bossBolts[i];

        if (!bolt->active) continue;
        if (bolt->ttl-- <= 0) { bolt->active = FALSE; continue; }

        bolt->px += bolt->dirX * (4 * FP_ONE);
        bolt->py += bolt->dirY * (4 * FP_ONE);
        tx = FP_TO_INT(bolt->px) / TILE_SIZE;
        ty = FP_TO_INT(bolt->py) / TILE_SIZE;

        if (IsBlocked(tx, ty)) { bolt->active = FALSE; continue; }

        for (j = 0; j < robotCount; j++) {
            if (AbsW(robots[j].tileX - tx) + AbsW(robots[j].tileY - ty) <= 1) {
                BossStunRobot(j, "BOLT");
                bolt->active = FALSE;
                break;
            }
        }
    }
}


static BOOL ActivateSpaceOrFireAction(void)
{
    if (gameState == GAME_ROUND_END) { roundIndex++; MarkHudStatusTextDirty(); ResetLevel(); return TRUE; }
    if (gameState == GAME_MATCH_END && bonusAvailable) { StartBonusRound(); return TRUE; }
    if (gameState == GAME_BONUS_END || gameState == GAME_MATCH_END) { EnterTitleScreen(); return TRUE; }
    if (gameState == GAME_TITLE && titleTwoPlayerArmed && !titlePlayer2Locked) { TitleLockPlayer2(); return TRUE; }
    if (gameState == GAME_TITLE && titleTwoPlayerArmed && titlePlayer2Locked) { OpenAiSelectMenu(aiRivals); return TRUE; }
    if (gameState == GAME_TITLE) { OpenAiDifficultyMenu(humanPlayers, aiRivals); return TRUE; }
    return FALSE;
}

static void HandleRawKey(UWORD rawCode)
{
    BOOL keyUpEvent = (rawCode & 0x80) ? TRUE : FALSE;
    UWORD code = rawCode & 0x7F;

    /* Attract mode: any keypress hands control straight back to a real
     * player instead of being interpreted as a P1/P2 command. */
    if (demoModeActive) {
        if (!keyUpEvent) {
            demoModeActive = FALSE;
            EnterTitleScreen();
        }
        return;
    }

    if (HandleAiDifficultyMenuRawKey(code, keyUpEvent)) {
        return;
    }

    if (HandleAiSelectMenuRawKey(code, keyUpEvent)) {
        return;
    }

    if (HandlePauseMenuRawKey(code, keyUpEvent)) {
        return;
    }

    if (code == RAW_ESC && !keyUpEvent) {
        running = FALSE;
        return;
    }

    if (!keyUpEvent && code == RAW_Q && (gameState == GAME_PLAYING || gameState == GAME_BONUS_PLAYING)) {
        OpenPauseMenu();
        return;
    }

    if (!keyUpEvent && gameState == GAME_INTRO) {
        EnterTitleScreen();
        return;
    }

    if (!keyUpEvent && gameState == GAME_TITLE) {
        if (code == RAW_LEFT) { if (!titleTwoPlayerArmed || titlePlayer2Locked) { titleSelectPlayer = 0; MarkTitlePanelDirty(); TitleChooseVariant(0, -1); } return; }
        if (code == RAW_RIGHT) { if (!titleTwoPlayerArmed || titlePlayer2Locked) { titleSelectPlayer = 0; MarkTitlePanelDirty(); TitleChooseVariant(0, 1); } return; }
        if (code == RAW_Z) { if (!titleTwoPlayerArmed || titlePlayer2Locked) TitleArmTwoPlayer(); titleSelectPlayer = 1; MarkTitlePanelDirty(); TitleChooseVariant(1, -1); return; }
        if (code == RAW_C) { if (!titleTwoPlayerArmed || titlePlayer2Locked) TitleArmTwoPlayer(); titleSelectPlayer = 1; MarkTitlePanelDirty(); TitleChooseVariant(1, 1); return; }
        if (code == RAW_V) { TitlePlayer2Fire(); return; }
        /* Don't hijack the title screen into a demo while the AI
         * difficulty/rival menus are up mid-selection - starting a match
         * pulls the rug out from under whatever the player was choosing
         * and can leave that menu's cached overlay stuck on screen. */
        if (code == RAW_D && !aiDifficultyMenuOpen && !aiSelectMenuOpen) { StartDemoMode(); return; }
        if (code == RAW_0 && titleTwoPlayerArmed && titlePlayer2Locked) { OpenAiSelectMenu(0); return; }
        if (code == RAW_1) { StartWithRivals(1); return; }
        if (code == RAW_2) { StartWithRivals(2); return; }
        if (code == RAW_3) { StartWithRivals(3); return; }
        if (code == RAW_4) { CycleGameSpeed(); return; }
        if (code == RAW_O) { StartWithRivals(9); return; }
        if (code == RAW_E) { maxBattery = 110; batteryCostPerMove = 1; MarkHudStatusTextDirty(); return; }
        if (code == RAW_S) { maxBattery = 55; batteryCostPerMove = 2; MarkHudStatusTextDirty(); return; }
        if (code == RAW_H) { maxBattery = 36; batteryCostPerMove = 3; MarkHudStatusTextDirty(); return; }
    }

    if (!keyUpEvent && code == RAW_B) { FirePlayerBolt(0); return; }
    if (!keyUpEvent && code == RAW_V) { FirePlayerBolt(1); return; }

    if (!keyUpEvent && code == RAW_R) {
        if (gameState == GAME_PLAYING) { ResetLevel(); return; }
        if (gameState == GAME_BONUS_PLAYING) { StartBonusRound(); return; }
    }

    if (!keyUpEvent && code == RAW_SPACE) {
        if (ActivateSpaceOrFireAction()) return;
    }

    switch (code) {
        case RAW_LEFT:  keyLeft[0]  = !keyUpEvent; break;
        case RAW_RIGHT: keyRight[0] = !keyUpEvent; break;
        case RAW_UP:    keyUp[0]    = !keyUpEvent; break;
        case RAW_DOWN:  keyDown[0]  = !keyUpEvent; break;
        case RAW_Z:     keyLeft[1]  = !keyUpEvent; break;
        case RAW_C:     keyRight[1] = !keyUpEvent; break;
        case RAW_S:     keyUp[1]    = !keyUpEvent; break;
        case RAW_X:     keyDown[1]  = !keyUpEvent; break;
        default: break;
    }
}

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

static BOOL ReadJoystickFire(WORD id)
{
    UBYTE pra = ciaa.ciapra;

    /* On-screen J1/P1 deliberately uses the physical Amiga joystick port
     * (custom.joy1dat plus CIAA PRA bit 7).  On-screen J2/P2 uses the mouse
     * port (custom.joy0dat plus CIAA PRA bit 6), with fire-only protection
     * applied in PollJoysticks outside the title join/confirm flow.
     */
    if (id == 0) return (pra & 0x80) ? FALSE : TRUE;
    return (pra & 0x40) ? FALSE : TRUE;
}

static void ResetJoystickConfirmHold(WORD id)
{
    if (id < 0 || id >= MAX_HUMAN_PLAYERS) return;
    joyFireHoldTicks[id] = 0;
    joyFireHoldTriggered[id] = FALSE;
    joyBluePrev[id] = FALSE;
}

static void ResetAllJoystickConfirmHolds(void)
{
    WORD i;
    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        ResetJoystickConfirmHold(i);
    }
}

static BOOL ReadJoystickBlueButton(WORD id)
{
    /* The second fire button (a CD32 pad's blue button, or a two-button
     * stick's second button) is wired to the port's POTY pin, not the CIA
     * fire line.  PollJoysticks drives the pot data lines high via POTGO each
     * frame; a pressed second button then grounds its POTY line, so the
     * matching POTINP data bit reads back low:
     *   Player 1 (joystick port, id 0): POTY = POTINP bit 14 (DATRY)
     *   Player 2 (mouse port,   id 1): POTY = POTINP bit 10 (DATLY)
     * This reads the CD32 blue button in the pad's default (un-shifted) mode
     * without the full CD32 shift-register protocol the other pad buttons
     * would need.  Hold-to-confirm stays as a fallback for one-button sticks.
     */
    UWORD pin = custom.potinp;
    if (id == 0) return (pin & 0x4000) ? FALSE : TRUE;
    return (pin & 0x0400) ? FALSE : TRUE;
}

static BOOL UpdateJoystickConfirmHold(WORD id, BOOL fire)
{
    if (id < 0 || id >= MAX_HUMAN_PLAYERS) return FALSE;

    if (!fire) {
        joyFireHoldTicks[id] = 0;
        joyFireHoldTriggered[id] = FALSE;
        return FALSE;
    }

    if (joyFireHoldTriggered[id]) return FALSE;
    if (joyFireHoldTicks[id] < JOY_CONFIRM_HOLD_FRAMES) joyFireHoldTicks[id]++;
    if (joyFireHoldTicks[id] >= JOY_CONFIRM_HOLD_FRAMES) {
        joyFireHoldTriggered[id] = TRUE;
        return TRUE;
    }
    return FALSE;
}

static BOOL ActivateTitleJoystickConfirmAction(void)
{
    if (aiDifficultyMenuOpen) { ActivateAiDifficultyMenu(); return TRUE; }
    if (aiSelectMenuOpen) { ActivateAiSelectMenu(); return TRUE; }
    return ActivateSpaceOrFireAction();
}

static void HandleAiDifficultyJoystick(BOOL up, BOOL down, BOOL firePressed)
{
    static BOOL prevUp = FALSE;
    static BOOL prevDown = FALSE;

    if (!aiDifficultyMenuOpen) {
        prevUp = up;
        prevDown = down;
        return;
    }

    if (up && !prevUp) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 2) % 3;
    }
    if (down && !prevDown) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 1) % 3;
    }
    if (firePressed) {
        ActivateAiDifficultyMenu();
    }

    prevUp = up;
    prevDown = down;
}

static void HandleAiSelectJoystick(BOOL up, BOOL down, BOOL firePressed)
{
    static BOOL prevUp = FALSE;
    static BOOL prevDown = FALSE;

    if (!aiSelectMenuOpen) {
        prevUp = up;
        prevDown = down;
        return;
    }

    if (up && !prevUp) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 3) & 3;
    }
    if (down && !prevDown) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 1) & 3;
    }
    if (firePressed) {
        ActivateAiSelectMenu();
    }

    prevUp = up;
    prevDown = down;
}

static void HandleTitleJoystick(WORD id, BOOL left, BOOL right, BOOL up, BOOL down, BOOL firePressed, BOOL confirmPressed)
{
    static BOOL prevLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
    static BOOL prevRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

    if (gameState != GAME_TITLE) {
        prevLeft[id] = left;
        prevRight[id] = right;
        if (id == 0) {
            HandleAiSelectJoystick(FALSE, FALSE, FALSE);
            HandleAiDifficultyJoystick(FALSE, FALSE, FALSE);
        }
        return;
    }

    if (!joyEnabled[id]) {
        prevLeft[id] = left;
        prevRight[id] = right;
        if (id == 0) {
            HandleAiSelectJoystick(FALSE, FALSE, FALSE);
            HandleAiDifficultyJoystick(FALSE, FALSE, FALSE);
        }
        return;
    }

    if (aiDifficultyMenuOpen) {
        if (id == 0) {
            /* A plain fire tap selects the highlighted entry here too, not
             * just blue/hold-to-confirm, so a single-fire-button stick or an
             * undetected CD32 pad can still drive this menu. */
            HandleAiDifficultyJoystick(up, down, confirmPressed || firePressed);
        }
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (aiSelectMenuOpen) {
        if (id == 0) {
            HandleAiSelectJoystick(up, down, confirmPressed || firePressed);
        }
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (confirmPressed) {
        ActivateTitleJoystickConfirmAction();
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (firePressed) {
        if (id == 0) {
            /* J1 fire shares the left mouse button line, so on the title
             * screen it only confirms/focuses Player 1's carousel selection.
             * Starting/advancing is left to Space, keyboard AI shortcuts, or
             * the J1 second button/RMB path handled by Intuition.
             */
            titleSelectPlayer = 0;
            MarkTitlePanelDirty();
        } else if (id == 1) {
            TitleArmTwoPlayer();
        }
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (id == 0) {
        if (!titleTwoPlayerArmed || titlePlayer2Locked) {
            titleSelectPlayer = 0;
            MarkTitlePanelDirty();
            if (left && !prevLeft[id]) TitleChooseVariant(0, -1);
            if (right && !prevRight[id]) TitleChooseVariant(0, 1);
        }
    } else if (titleTwoPlayerArmed && !titlePlayer2Locked) {
        titleSelectPlayer = 1;
        MarkTitlePanelDirty();
        if (left && !prevLeft[id]) TitleChooseVariant(1, -1);
        if (right && !prevRight[id]) TitleChooseVariant(1, 1);
    }

    prevLeft[id] = left;
    prevRight[id] = right;
}

static void PollJoysticks(void)
{
    UWORD dat[MAX_HUMAN_PLAYERS];
    WORD i;

    /* CD32 pads were previously read through lowlevel.library's ReadJoyPort()
     * to expose all seven buttons via its shift-register protocol. That call
     * ran every single frame right before the direct joy1dat/CIA reads below,
     * and on real hardware left P1's port completely unresponsive (movement
     * and fire both dead) -- almost certainly the two fighting over the same
     * POTGO/shift-register lines. A CD32 pad's d-pad and red fire button are
     * already wired to the standard joystick lines for compatibility, and its
     * blue button already reads through the POTY pin trick in
     * ReadJoystickBlueButton below, so dropping the lowlevel path only costs
     * the pad's Play button (Q still pauses from the keyboard) in exchange
     * for the port actually working. */

    /* Drive the four pot data lines high as outputs so the second-button
     * (two-button stick button 2) read in ReadJoystickBlueButton is valid this
     * frame; a pressed button then pulls its POTY line low. */
    custom.potgo = 0xff00;

    dat[0] = custom.joy1dat; /* on-screen J1/P1 = physical joystick port */
    dat[1] = custom.joy0dat; /* on-screen J2/P2 = mouse port */

    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        WORD stateBefore = gameState;
        BOOL left, right, up, down, fire, blue;
        BOOL bluePressed;
        BOOL directionActive;
        BOOL firePressed;
        BOOL holdConfirmed;
        BOOL confirmPressed;

        left  = JOY_LEFT(dat[i]);
        right = JOY_RIGHT(dat[i]);
        up    = JOY_UP(dat[i]);
        down  = JOY_DOWN(dat[i]);
        fire  = ReadJoystickFire(i);
        blue  = ReadJoystickBlueButton(i);

        directionActive = left || right || up || down;

        /* Outside the title screen, keep mouse-port protection for the direct
         * read: a mouse left click is electrically the same as J2/P2 fire.
         * During the title screen, allow fire-only input so player 2 can
         * join/arm before moving the stick and so hold-to-confirm works. The
         * intro screen gets the same exemption so a P2 joystick's fire button
         * can skip it (a stray mouse click there already skips the intro too,
         * via the SELECTDOWN handler in PollWindowMessages).
         */
        if (gameState != GAME_TITLE && gameState != GAME_INTRO && i == 1 && !joyEnabled[i] && !directionActive) {
            fire = FALSE;
        }

        bluePressed = (blue && !joyBluePrev[i]) ? TRUE : FALSE;

        if (gameState == GAME_TITLE && (directionActive || fire || blue)) titleIdleTicks = 0;

        if (demoModeActive) {
            BOOL newDemoInput;

            if (!demoJoyPrimed) {
                demoPrevLeft[i] = left;
                demoPrevRight[i] = right;
                demoPrevUp[i] = up;
                demoPrevDown[i] = down;
                demoPrevFire[i] = fire;
                demoPrevBlue[i] = blue;
                joyFirePrev[i] = fire;
                joyBluePrev[i] = blue;
                if (i == MAX_HUMAN_PLAYERS - 1) demoJoyPrimed = TRUE;
                continue;
            }

            newDemoInput = (left && !demoPrevLeft[i]) ||
                           (right && !demoPrevRight[i]) ||
                           (up && !demoPrevUp[i]) ||
                           (down && !demoPrevDown[i]) ||
                           (fire && !demoPrevFire[i]) ||
                           (blue && !demoPrevBlue[i]);

            demoPrevLeft[i] = left;
            demoPrevRight[i] = right;
            demoPrevUp[i] = up;
            demoPrevDown[i] = down;
            demoPrevFire[i] = fire;
            demoPrevBlue[i] = blue;

            if (newDemoInput) {
                demoModeActive = FALSE;
                joyFirePrev[i] = fire;
                joyBluePrev[i] = blue;
                EnterTitleScreen();
                return;
            }
        }

        firePressed = (fire && !joyFirePrev[i]) ? TRUE : FALSE;
        if (gameState == GAME_TITLE) {
            holdConfirmed = UpdateJoystickConfirmHold(i, fire);
            confirmPressed = bluePressed || holdConfirmed;
        } else {
            ResetJoystickConfirmHold(i);
            holdConfirmed = FALSE;
            confirmPressed = FALSE;
        }

        if (firePressed || (gameState == GAME_TITLE && fire)) {
            if (!joyEnabled[i] && gameState == GAME_TITLE) MarkTitleAllDirty();
            joyEnabled[i] = TRUE;
        }

        if (gameState == GAME_TITLE) {
            HandleTitleJoystick(i, left, right, up, down, firePressed, confirmPressed);
        }

        joyLeft[i] = joyEnabled[i] ? left : FALSE;
        joyRight[i] = joyEnabled[i] ? right : FALSE;
        joyUp[i] = joyEnabled[i] ? up : FALSE;
        joyDown[i] = joyEnabled[i] ? down : FALSE;

        if (stateBefore == GAME_PLAYING || stateBefore == GAME_BONUS_PLAYING) {
            if (joyEnabled[i] && firePressed) FirePlayerBolt(i);
        } else if (stateBefore != GAME_TITLE && joyEnabled[i] && firePressed) {
            ActivateSpaceOrFireAction();
        }
        joyFirePrev[i] = fire;
        joyBluePrev[i] = blue;
    }
}

static void PollWindowMessages(void)
{
    struct IntuiMessage *msg;

    if (!win || !win->UserPort) return;

    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;

        ReplyMsg((struct Message *)msg);

        if (gameState == GAME_TITLE) titleIdleTicks = 0;

        if (cls == IDCMP_RAWKEY) {
            HandleRawKey(code);
        } else if (cls == IDCMP_MOUSEBUTTONS) {
            if (demoModeActive && code == SELECTDOWN) {
                demoModeActive = FALSE;
                EnterTitleScreen();
                continue;
            }
            if (code == SELECTDOWN && gameState == GAME_INTRO) EnterTitleScreen();
            if (code == MENUDOWN && gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) {
                if (aiDifficultyMenuOpen) ActivateAiDifficultyMenu();
                else if (aiSelectMenuOpen) ActivateAiSelectMenu();
                else ActivateSpaceOrFireAction();
            }
            /* J1 fire shares the left mouse button line on Amiga hardware, so
             * title-screen SELECTDOWN must not start/activate menus.  Let
             * PollJoysticks consume the fire press and enable J1 instead.
             * J1 second fire/RMB arrives as MENUDOWN and can start/advance.
             */
        }
    }
}

static void DrainWindowMessages(void)
{
    struct IntuiMessage *msg;

    if (!win || !win->UserPort) return;

    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ReplyMsg((struct Message *)msg);
    }
}

/* -------------------------------------------------------------------------
 * Screen/window setup
 * ------------------------------------------------------------------------- */

static BOOL OpenGameScreen(void)
{
    scr = OpenScreenTags(NULL,
        SA_Width,       SCREEN_W,
        SA_Height,      SCREEN_H,
        SA_Depth,       DEPTH,
        SA_Title,       (ULONG)"RoboVac Rescue",
        SA_ShowTitle,   FALSE,
        TAG_END);

    if (!scr) {
        printf("Could not open custom screen\n");
        return FALSE;
    }

    LoadRGB4(&scr->ViewPort, palette, 32);

    InitTitleEffectQuality();

    audioSilenceWord = (UWORD *)AllocMem(sizeof(UWORD), MEMF_CHIP | MEMF_PUBLIC | MEMF_CLEAR);
    if (!audioSilenceWord) {
        printf("Could not allocate chip RAM for audio silence guard; one-shot samples may repeat\n");
    }

    renderBM = AllocBitMap(SCREEN_W, SCREEN_H, DEPTH,
                           BMF_CLEAR | BMF_DISPLAYABLE,
                           scr->RastPort.BitMap);
    if (!renderBM) {
        printf("Could not allocate render bitmap\n");
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    roomBM = AllocBitMap(SCREEN_W, SCREEN_H, DEPTH,
                         BMF_CLEAR | BMF_DISPLAYABLE,
                         scr->RastPort.BitMap);
    if (!roomBM) {
        printf("Could not allocate room bitmap\n");
        FreeBitMap(renderBM);
        renderBM = NULL;
        renderRP.BitMap = NULL;
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    InitRastPort(&renderRP);
    renderRP.BitMap = renderBM;

    InitRastPort(&roomRP);
    roomRP.BitMap = roomBM;

    if (!InitRoundStartOverlayCache()) {
        printf("Round-start overlay cache allocation failed; simple countdown text fallback enabled\n");
    }

    if (!InitTileCache()) {
        FreeRoundStartOverlayCache();
        FreeBitMap(roomBM);
        roomBM = NULL;
        roomRP.BitMap = NULL;
        FreeBitMap(renderBM);
        renderBM = NULL;
        renderRP.BitMap = NULL;
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    if (!InitRobotBobs()) {
        printf("Robot BOB cache allocation failed; fallback drawing enabled\n");
    }

    if (!AllocTitleStaticCache()) {
        printf("Could not reserve title static cache before menu music load; direct title rendering fallback active\n");
    }

    if (!LoadIntroTitleImage()) {
        printf("Optional PROGDIR:tiles/robovac-title.iff not loaded; starting at menu\n");
    }

    LoadMenuMusicStream();
    LoadRoundStartSamples();
    LoadGameplaySamples();

    win = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)scr,
        WA_Left,         0,
        WA_Top,          0,
        WA_Width,        SCREEN_W,
        WA_Height,       SCREEN_H,
        WA_Backdrop,     TRUE,
        WA_Borderless,   TRUE,
        WA_Activate,     TRUE,
        WA_RMBTrap,      TRUE,
        WA_IDCMP,        IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS,
        TAG_END);

    if (!win) {
        printf("Could not open game window\n");
        CloseGameScreen();
        return FALSE;
    }

    return TRUE;
}

static void CloseGameScreen(void)
{
    FreeGameplaySamples();
    FreeRoundStartSamples();
    FreeMenuMusicSample();
    FreeTitleCopperGradient();

    if (win) {
        DrainWindowMessages();
        CloseWindow(win);
        win = NULL;
    }

    FreeIntroTitleImage();
    FreeTitleStaticCache();
    FreeTitleCarouselCache();
    FreeBonusBossCache();
    FreeBoltCache();
    FreeRobotScaledCache();

    if (robotCacheBM) {
        FreeBitMap(robotCacheBM);
        robotCacheBM = NULL;
    }

    if (robotMaskBM) {
        FreeBitMap(robotMaskBM);
        robotMaskBM = NULL;
    }
    robotRP.BitMap = NULL;

    FreeRoundStartOverlayCache();

    if (tileCacheBM) {
        FreeBitMap(tileCacheBM);
        tileCacheBM = NULL;
    }
    tileRP.BitMap = NULL;

    if (roomBM) {
        FreeBitMap(roomBM);
        roomBM = NULL;
    }
    roomRP.BitMap = NULL;

    if (renderBM) {
        FreeBitMap(renderBM);
        renderBM = NULL;
    }
    renderRP.BitMap = NULL;

    if (scr) {
        CloseScreen(scr);
        scr = NULL;
    }

    if (audioSilenceWord) {
        FreeMem(audioSilenceWord, sizeof(UWORD));
        audioSilenceWord = NULL;
    }
}

int main(void)
{
    printf("RoboVac Rescue smooth AI prototype starting...\n");

    if (!OpenGameScreen()) {
        return 20;
    }

    gameState = introTitleBM ? GAME_INTRO : GAME_TITLE;
    if (gameState == GAME_TITLE) {
        PrepareTitlePresentation();
    }

    while (running) {
        PollWindowMessages();
        PollJoysticks();

        if (gameState == GAME_TITLE && !aiDifficultyMenuOpen && !aiSelectMenuOpen) {
            titleIdleTicks++;
            if (titleIdleTicks >= TITLE_IDLE_DEMO_TICKS) {
                titleIdleTicks = 0;
                StartDemoMode();
            }
        } else {
            titleIdleTicks = 0;
        }

        StepGame();
        DrawFrame();
        WaitTOF();
        PresentFrame();
        ServiceTitleMusicForState();
    }

    CloseGameScreen();

    printf("RoboVac Rescue ended.\n");
    return 0;
}
