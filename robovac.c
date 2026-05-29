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
 *   Arrow keys - move player robot
 *   R/Space    - start/reset
 *   1/2/3      - start with 1/2/3 AI rivals from title screen
 *   O          - hidden 9-rival mode (battle mode)
 *   Esc/RMB    - quit
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <proto/datatypes.h>

#include <datatypes/pictureclass.h>
#include <datatypes/datatypes.h>

#include <stdio.h>
#include <string.h>

static const char __attribute__((used)) min_stack[] = "$STACK:65536";

#define SCREEN_W    320
#define SCREEN_H    256
#define DEPTH       5

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
#define POWERUP_DURATION_MOVES  20
#define POWERUP_BOLT_MOVES      10
#define POWERUP_EMP_TICKS       250
#define POWERUP_DIRT_DROP       5
#define POWERUP_QUAD_RADIUS     1

#define ROBOT_W     16
#define ROBOT_H     16
#define MAX_ROBOTS  10
#define ROBOT_VARIANTS 7

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

#define GAME_INTRO      0
#define GAME_TITLE      1
#define GAME_PLAYING    2
#define GAME_ROUND_END  3
#define GAME_MATCH_END  4

#define RAW_ESC     0x45
#define RAW_LEFT    0x4F
#define RAW_RIGHT   0x4E
#define RAW_UP      0x4C
#define RAW_DOWN    0x4D
#define RAW_R       0x13
#define RAW_SPACE   0x40
#define RAW_1       0x01
#define RAW_2       0x02
#define RAW_3       0x03
#define RAW_O       0x18
#define RAW_B       0x35
#define RAW_E       0x12
#define RAW_S       0x21
#define RAW_H       0x25

#define TITLE_CAROUSEL_Y 172
#define TITLE_ROBOT_SCALE 2
#define INTRO_TITLE_W 300
#define INTRO_TITLE_H 100
#define INTRO_HOLD_FRAMES 90
#define INTRO_EFFECT_FRAMES 64
#define INTRO_SLICE_H 4
#define INTRO_TOTAL_FRAMES (INTRO_HOLD_FRAMES + INTRO_EFFECT_FRAMES)
#define TITLE_SPIN_STEPS 32
#define TITLE_SPIN_STEPS_FALLBACK 16
#define TITLE_ROT_W (ROBOT_W * TITLE_ROBOT_SCALE)
#define TITLE_ROT_H (ROBOT_H * TITLE_ROBOT_SCALE)

struct Robot {
    WORD tileX;
    WORD tileY;
    WORD targetX;
    WORD targetY;
    LONG px;
    LONG py;
    WORD battery;
    WORD score;
    WORD stunTicks;
    WORD emergencyMovesLeft;
    WORD chargeTicks;
    WORD cleanStreak;
    WORD powerMovesLeft;
    UBYTE powerType;
    BOOL ai;
    BOOL moving;
    UBYTE spriteIndex;
    UBYTE spriteVariant;
};

static struct Screen *scr = NULL;
static struct Window *win = NULL;

static struct RastPort renderRP;
static struct BitMap *renderBM = NULL;

static struct RastPort roomRP;
static struct BitMap *roomBM = NULL;

static struct RastPort tileRP;
static struct BitMap *tileCacheBM = NULL;

static struct RastPort robotRP;
static struct BitMap *robotCacheBM = NULL;
static struct BitMap *robotMaskBM = NULL;
static struct RastPort titleCarouselRP;
static struct BitMap *titleCarouselBM = NULL;
static struct BitMap *titleCarouselMaskBM = NULL;
static WORD titleCarouselFrameCount = 0;
static struct RastPort introTitleRP;
static struct BitMap *introTitleBM = NULL;
static WORD introTitleW = 0;
static WORD introTitleH = 0;
static WORD introTicks = 0;
static BOOL introPaletteActive = FALSE;
static UWORD introPalette[32];

static UBYTE map[MAP_H][MAP_W];

static struct Robot robots[MAX_ROBOTS];
static WORD aiPrevTileX[MAX_ROBOTS];
static WORD aiPrevTileY[MAX_ROBOTS];
static WORD robotCount = 2;
static WORD aiRivals = 1;
static WORD selectedPlayerVariant = 0;
static WORD titleSpinPhase = 0;

static WORD dirtLeft = 0;
static WORD moves = 0;
static WORD maxBattery = 110;
static WORD batteryCostPerMove = 1;
static WORD gameState = GAME_TITLE;

static WORD roundIndex = 0;
static WORD roundWins[MAX_ROBOTS] = {0,0,0,0};
static WORD totalScores[MAX_ROBOTS] = {0,0,0,0};
static WORD roundScores[MAX_ROBOTS] = {0,0,0,0};
static WORD roundWinner = -1;
static WORD finalWinner = -1;
static WORD roomType = 0;
static BOOL running = TRUE;

static BOOL keyLeft = FALSE;
static BOOL keyRight = FALSE;
static BOOL keyUp = FALSE;
static BOOL keyDown = FALSE;
static WORD playerFacingX = 0;
static WORD playerFacingY = -1;
static char lastPowerText[80] = "";
static WORD lastPowerTicks = 0;

struct Bolt {
    BOOL active;
    WORD dirX;
    WORD dirY;
    LONG px;
    LONG py;
    WORD ttl;
};
static struct Bolt playerBolt = {FALSE, 0, -1, 0, 0, 0};

static ULONG rng = 0x1234ABCD;

static const WORD robotStartX[MAX_ROBOTS] = {1, 18, 17, 18, 1, 2, 1, 18, 17, 18};
static const WORD robotStartY[MAX_ROBOTS] = {1, 1, 1, 2, 12, 12, 11, 12, 12, 11};
static const WORD robotDockX[MAX_ROBOTS]  = {1, 18, 18, 18, 1, 1, 1, 18, 18, 18};
static const WORD robotDockY[MAX_ROBOTS]  = {1, 1, 1, 1, 12, 12, 12, 12, 12, 12};

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

static const WORD roundDirtTargets[5] = {14, 20, 26, 32, 38};
static void StepPlayerBolt(void);
static void DrawTitleCarousel(void);
static void TriggerRobotPower(WORD id);
static WORD SpawnDirtTiles(WORD count);
static void EnterTitleScreen(void);

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

static void PutText(struct RastPort *rp, WORD x, WORD y, const char *s, UBYTE pen)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)s, strlen(s));
}

static void CountDirt(void)
{
    WORD x;
    WORD y;

    dirtLeft = 0;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            if (map[y][x] == TILE_DIRT) {
                dirtLeft++;
            }
        }
    }
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

static BOOL InitTileCache(void)
{
    UBYTE i;
    tileCacheBM = AllocBitMap(TILE_SIZE * TILE_COUNT, TILE_SIZE, DEPTH,
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
    WORD srcX = TILE_WALL * TILE_SIZE;
    WORD dstX = MAP_X + tx * TILE_SIZE;
    WORD dstY = MAP_Y + ty * TILE_SIZE;
    UBYTE rot = GetWallRotation(tx, ty);
    WORD x;
    WORD y;

    if (rot == 0) {
        BltBitMapRastPort(tileCacheBM, srcX, 0,
                          rp, dstX, dstY,
                          TILE_SIZE, TILE_SIZE,
                          0xC0);
        return;
    }

    for (y = 0; y < TILE_SIZE; y++) {
        for (x = 0; x < TILE_SIZE; x++) {
            WORD sx = x;
            WORD sy = y;
            WORD px;

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

            px = ReadPixel(&tileRP, srcX + sx, sy);
            SetAPen(rp, (UBYTE)px);
            WritePixel(rp, dstX + x, dstY + y);
        }
    }
}

static void UpdateRoomTile(WORD tx, WORD ty)
{
    if (!roomBM || tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return;
    if (map[ty][tx] == TILE_WALL) {
        BlitWallRotatedTo(&roomRP, tx, ty);
    } else {
        BlitTileTo(&roomRP, map[ty][tx], tx, ty);
    }
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


static void LoadGamePalette(void)
{
    LoadRGB4(&scr->ViewPort, palette, 32);
    introPaletteActive = FALSE;
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

    titleCarouselFrameCount = 0;
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
    return TRUE;
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
    WORD srcX = (variant * SPR_STATE_COUNT + SPR_READY) * ROBOT_W;
    WORD dstBaseX = (variant * titleCarouselFrameCount + frame) * TITLE_ROT_W;
    WORD centre = (ROBOT_W - 1) * TITLE_ROBOT_SCALE / 2;
    WORD x;
    WORD y;

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            LONG pen = ReadPixel(&robotRP, srcX + x, y);
            WORD rx;
            WORD ry;
            WORD tx;
            WORD ty;
            WORD dx;
            WORD dy;

            if (pen <= 0) continue;

            rx = x - (ROBOT_W / 2);
            ry = y - (ROBOT_H / 2);
            tx = ((rx * cosv) - (ry * sinv)) / 64;
            ty = ((rx * sinv) + (ry * cosv)) / 64;
            tx = centre + (tx * TITLE_ROBOT_SCALE);
            ty = centre + (ty * TITLE_ROBOT_SCALE);

            SetAPen(&titleCarouselRP, (UBYTE)pen);
            SetAPen(maskRP, 1);
            for (dy = 0; dy < TITLE_ROBOT_SCALE; dy++) {
                for (dx = 0; dx < TITLE_ROBOT_SCALE; dx++) {
                    WORD px = tx + dx;
                    WORD py = ty + dy;
                    if (px >= 0 && py >= 0 && px < TITLE_ROT_W && py < TITLE_ROT_H) {
                        WritePixel(&titleCarouselRP, dstBaseX + px, py);
                        WritePixel(maskRP, dstBaseX + px, py);
                    }
                }
            }
        }
    }
}

static BOOL BuildTitleCarouselRotationCache(void)
{
    struct RastPort maskRP;
    WORD variant;
    WORD frame;

    if (!robotCacheBM || !robotMaskBM) return FALSE;

    if (!AllocTitleCarouselCache(TITLE_SPIN_STEPS)) {
        if (!AllocTitleCarouselCache(TITLE_SPIN_STEPS_FALLBACK)) {
            printf("Could not allocate title carousel rotation cache\n");
            return FALSE;
        }
        printf("Using %ld title carousel rotation frames per robot\n", (LONG)titleCarouselFrameCount);
    }

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
        return FALSE;
    }

    InitRastPort(&robotRP);
    robotRP.BitMap = robotCacheBM;

    if (!LoadRobotSheetIntoCache()) {
        printf("Could not load PROGDIR:tiles/airobot1.iff through airobot7.iff (need at least 16x16, 16 colours)\n");
        FreeBitMap(robotCacheBM);
        FreeBitMap(robotMaskBM);
        robotCacheBM = NULL;
        robotMaskBM = NULL;
        return FALSE;
    }

    if (!BuildTitleCarouselRotationCache()) {
        FreeBitMap(robotCacheBM);
        FreeBitMap(robotMaskBM);
        robotCacheBM = NULL;
        robotMaskBM = NULL;
        return FALSE;
    }

    LoadGamePalette();

    return TRUE;
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
    RectFill(&renderRP, sx + 4, sy + 13, sx + 12, sy + 14);

    if (robotCacheBM && robotMaskBM && robotMaskBM->Planes[0]) {
        BltMaskBitMapRastPort(robotCacheBM, srcX, 0,
                              &renderRP, sx, sy,
                              ROBOT_W, ROBOT_H,
                              (ABC | ABNC | ANBC),
                              robotMaskBM->Planes[0]);
    } else {
        SetAPen(&renderRP, 16 + 1);
        RectFill(&renderRP, sx + 2, sy + 2, sx + 13, sy + 13);
    }
}

static void DrawPlayerBolt(void)
{
    WORD sx, sy, x, y;
    WORD srcBaseX;

    if (!playerBolt.active || !robotCacheBM) return;

    sx = MAP_X + FP_TO_INT(playerBolt.px);
    sy = MAP_Y + FP_TO_INT(playerBolt.py);
    srcBaseX = SPR_ENERGY_BOLT * ROBOT_W;

    if (playerBolt.dirY != 0) {
        for (y = 0; y < ROBOT_H; y++) {
            for (x = 0; x < ROBOT_W; x++) {
                LONG p = ReadPixel(&robotRP, srcBaseX + x, y);
                if (p <= 0) continue;
                SetAPen(&renderRP, (UBYTE)p);
                WritePixel(&renderRP, sx + y, sy + (ROBOT_W - 1 - x));
            }
        }
    } else {
        for (y = 0; y < ROBOT_H; y++) {
            for (x = 0; x < ROBOT_W; x++) {
                LONG p = ReadPixel(&robotRP, srcBaseX + x, y);
                if (p <= 0) continue;
                SetAPen(&renderRP, (UBYTE)p);
                WritePixel(&renderRP, sx + x, sy + y);
            }
        }
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
    robots[id].moving = FALSE;
    robots[id].spriteIndex = SPR_READY;
    robots[id].spriteVariant = 0;
}

static void InitRobots(void)
{
    WORD i;

    robotCount = 1 + aiRivals;
    if (robotCount < 1) robotCount = 1;
    if (robotCount > MAX_ROBOTS) robotCount = MAX_ROBOTS;

    for (i = 0; i < robotCount; i++) {
        SetRobotTile(i, robotStartX[i], robotStartY[i]);
        aiPrevTileX[i] = robotStartX[i];
        aiPrevTileY[i] = robotStartY[i];
        robots[i].battery = maxBattery;
        robots[i].score = 0;
        robots[i].stunTicks = 0;
        robots[i].emergencyMovesLeft = EMERGENCY_DOCK_MOVES;
        robots[i].chargeTicks = 0;
        robots[i].cleanStreak = 0;
        robots[i].powerMovesLeft = 0;
        robots[i].powerType = POWER_NONE;
        robots[i].ai = (i != 0) ? TRUE : FALSE;
        robots[i].spriteIndex = SPR_READY;
        robots[i].spriteVariant = (i == 0) ? (UBYTE)selectedPlayerVariant : (UBYTE)((selectedPlayerVariant + i) % ROBOT_VARIANTS);
    }

    moves = 0;
}



static WORD CleanTileForRobot(WORD id, WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 0;
    if (map[ty][tx] != TILE_DIRT) return 0;

    map[ty][tx] = TILE_FLOOR;
    if (dirtLeft > 0) dirtLeft--;
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

    if (robots[id].powerType == POWER_BOLT) {
        robots[id].powerMovesLeft = POWERUP_BOLT_MOVES;
    } else if (robots[id].powerType == POWER_EMP) {
        for (i = 0; i < robotCount; i++) {
            if (i != id) {
                robots[i].stunTicks = POWERUP_EMP_TICKS;
                if (robots[i].moving) {
                    robots[i].tileX = robots[i].targetX;
                    robots[i].tileY = robots[i].targetY;
                    robots[i].px = TO_FP(robots[i].tileX * TILE_SIZE);
                    robots[i].py = TO_FP(robots[i].tileY * TILE_SIZE);
                    robots[i].moving = FALSE;
                }
            }
        }
        robots[id].powerMovesLeft = 0;
        robots[id].powerType = POWER_NONE;
    } else if (robots[id].powerType == POWER_DIRT_DROP) {
        WORD dropped = SpawnDirtTiles(POWERUP_DIRT_DROP);
        snprintf(lastPowerText, sizeof(lastPowerText), "%s DIRT BOMB +%d", RobotTag(id), dropped);
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
    return (tx == robotDockX[id] && ty == robotDockY[id]) ? TRUE : FALSE;
}

static BOOL ValidDirtTile(WORD tx, WORD ty)
{
    WORD i;
    if (map[ty][tx] != TILE_FLOOR) return FALSE;
    for (i = 0; i < MAX_ROBOTS; i++) {
        if (IsRobotDock(i, tx, ty)) return FALSE;
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
            UpdateRoomTile(x, y);
            placed++;
        }
        tries++;
    }
    if (placed > 0) CountDirt();
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
            placed++;
        }
        tries++;
    }
}

static void ResetLevel(void)
{
    WORD x, y;
    const char **layout;

    roomType = RandRange(5);
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

    SpawnRoundDirt(roundDirtTargets[roundIndex]);
    keyLeft = keyRight = keyUp = keyDown = FALSE;
    playerBolt.active = FALSE;
    lastPowerText[0] = '\0';
    lastPowerTicks = 0;
    gameState = GAME_PLAYING;

    InitRobots();
    CountDirt();
    BuildRoomBuffer();
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
    if (RobotAtTile(nx, ny, id)) return FALSE;

    if (robots[id].powerType == POWER_WALL_SMASH && robots[id].powerMovesLeft > 0 && map[ny][nx] == TILE_WALL) {
        map[ny][nx] = TILE_FLOOR;
        UpdateRoomTile(nx, ny);
    }

    if (robots[id].battery < batteryCostPerMove) {
        dockDistNow = AbsW(robots[id].tileX - robotDockX[id]) + AbsW(robots[id].tileY - robotDockY[id]);
        dockDistNext = AbsW(nx - robotDockX[id]) + AbsW(ny - robotDockY[id]);
        if (dockDistNext >= dockDistNow) return FALSE;
    }

    robots[id].targetX = nx;
    robots[id].targetY = ny;
    robots[id].moving = TRUE;
    if (dx < 0) robots[id].spriteIndex = SPR_LEFT;
    else if (dx > 0) robots[id].spriteIndex = SPR_RIGHT;
    else if (dy < 0) robots[id].spriteIndex = SPR_UP;
    else if (dy > 0) robots[id].spriteIndex = SPR_DOWN;
    if (id == 0) {
        playerFacingX = dx;
        playerFacingY = dy;
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

    if (id == 0) {
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
        aiPrevTileX[id] = robots[id].tileX;
        aiPrevTileY[id] = robots[id].tileY;
    }

    robots[id].tileX = tx;
    robots[id].tileY = ty;
    robots[id].px = TO_FP(tx * TILE_SIZE);
    robots[id].py = TO_FP(ty * TILE_SIZE);
    robots[id].moving = FALSE;

    if (CleanTileForRobot(id, tx, ty)) {
        robots[id].cleanStreak++;
        if (robots[id].cleanStreak >= POWERUP_CLEAN_TARGET) {
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
        if (id == 0) robots[id].spriteIndex = SPR_CHARGING;
    }
}

static void StepRobotMovement(WORD id)
{
    LONG targetPx;
    LONG targetPy;
    LONG dx;
    LONG dy;

    if (!robots[id].moving) return;

    targetPx = TO_FP(robots[id].targetX * TILE_SIZE);
    targetPy = TO_FP(robots[id].targetY * TILE_SIZE);

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

static void ChooseAiMove(WORD id)
{
    WORD x;
    WORD y;
    WORD bestX = -1;
    WORD bestY = -1;
    WORD bestDist = 9999;
    WORD curX;
    WORD curY;
    WORD bestMoveDx = 0;
    WORD bestMoveDy = 0;
    WORD bestMoveScore = 32767;
    static const WORD dirX[4] = {1, 0, -1, 0};   /* right, down, left, up */
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD dir;

    if (id <= 0 || id >= robotCount) return;
    if (robots[id].moving) return;
    if (robots[id].stunTicks > 0) return;

    if (robots[id].battery <= 25 || (robots[id].battery <= 0 && (AbsW(robots[id].tileX - robotDockX[id]) + AbsW(robots[id].tileY - robotDockY[id]) <= 4))) {
        bestX = robotDockX[id];
        bestY = robotDockY[id];
    } else if (robots[id].battery < batteryCostPerMove) return;

    curX = robots[id].tileX;
    curY = robots[id].tileY;

    if (bestX < 0) {
        for (y = 0; y < MAP_H; y++) {
            for (x = 0; x < MAP_W; x++) {
                if (map[y][x] == TILE_DIRT) {
                    WORD d = AbsW(x - curX) + AbsW(y - curY);
                    if (d < bestDist) {
                        bestDist = d;
                        bestX = x;
                        bestY = y;
                    }
                }
            }
        }
    }

    if (bestX >= 0) {
        for (dir = 0; dir < 4; dir++) {
            WORD nx = curX + dirX[dir];
            WORD ny = curY + dirY[dir];
            WORD score;
            BOOL backtrack;

            if (!RobotCanPassTile(id, nx, ny)) continue;
            if (RobotAtTile(nx, ny, id)) continue;
            score = AbsW(bestX - nx) + AbsW(bestY - ny);
            backtrack = (nx == aiPrevTileX[id] && ny == aiPrevTileY[id]) ? TRUE : FALSE;
            if (backtrack) score += 6; /* prefer forward progress over ping-ponging */

            if (score < bestMoveScore) {
                bestMoveScore = score;
                bestMoveDx = dirX[dir];
                bestMoveDy = dirY[dir];
            }
        }

        if (bestMoveScore < 32767) {
            if (StartRobotMove(id, bestMoveDx, bestMoveDy)) return;
        }
    }

    /* Final fallback random movement if no directional move is available */
    for (dir = 0; dir < 4; dir++) {
        UWORD r = RandRange(4);
        if (r == 0 && StartRobotMove(id, -1, 0)) return;
        if (r == 1 && StartRobotMove(id, 1, 0)) return;
        if (r == 2 && StartRobotMove(id, 0, -1)) return;
        if (r == 3 && StartRobotMove(id, 0, 1)) return;
    }
}

static void ChoosePlayerMove(void)
{
    if (robots[0].moving) return;
    if (robots[0].stunTicks > 0) return;

    if (keyLeft) {
        StartRobotMove(0, -1, 0);
    } else if (keyRight) {
        StartRobotMove(0, 1, 0);
    } else if (keyUp) {
        StartRobotMove(0, 0, -1);
    } else if (keyDown) {
        StartRobotMove(0, 0, 1);
    }
}

static BOOL AnyRobotCanMove(void)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (robots[i].battery > 0) return TRUE;
        if (robots[i].moving) return TRUE;
    }

    return FALSE;
}

static void CheckEndState(void)
{
    WORD i;
    WORD best = -1;

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

    if (roundIndex >= 4) {
        finalWinner = 0;
        for (i = 1; i < robotCount; i++) {
            if (roundWins[i] > roundWins[finalWinner] ||
                (roundWins[i] == roundWins[finalWinner] && totalScores[i] > totalScores[finalWinner])) {
                finalWinner = i;
            }
        }
        gameState = GAME_MATCH_END;
    } else {
        gameState = GAME_ROUND_END;
    }
}


static void EnterTitleScreen(void)
{
    gameState = GAME_TITLE;
    introTicks = 0;
    LoadGamePalette();
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

    if (gameState == GAME_INTRO) {
        StepIntro();
        return;
    }

    if (gameState != GAME_PLAYING) return;

    for (i = 0; i < robotCount; i++) {
        StepRobotMovement(i);
        if (robots[i].stunTicks > 0) robots[i].stunTicks--;
        if (robots[i].powerType == POWER_BOLT && robots[i].powerMovesLeft > 0 && (RandRange(24) == 0)) {
            WORD j;
            for (j = 0; j < robotCount; j++) {
                if (j != i && AbsW(robots[j].tileX - robots[i].tileX) + AbsW(robots[j].tileY - robots[i].tileY) <= 3) {
                    robots[j].stunTicks = 80;
                    robots[j].battery -= 3;
                    if (robots[j].battery < 0) robots[j].battery = 0;
                    break;
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
    }
    StepPlayerBolt();
    if (lastPowerTicks > 0) lastPowerTicks--;

    ChoosePlayerMove();

    for (i = 1; i < robotCount; i++) {
        ChooseAiMove(i);
    }

    if (!robots[0].moving && robots[0].battery <= 25) {
        robots[0].spriteIndex = SPR_LOW_BATTERY;
    } else if (!robots[0].moving && map[robots[0].tileY][robots[0].tileX] == TILE_DOCK) {
        robots[0].spriteIndex = SPR_CHARGING;
    }

    CheckEndState();
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

static void DrawCachedTitleRobotSpin(WORD variant, WORD phase, WORD dstX, WORD dstY)
{
    WORD frame;
    WORD srcX;

    if (!titleCarouselBM || !titleCarouselMaskBM || !titleCarouselMaskBM->Planes[0]) return;
    if (variant < 0 || variant >= ROBOT_VARIANTS || titleCarouselFrameCount <= 0) return;

    phase &= (TITLE_SPIN_STEPS - 1);
    frame = (phase * titleCarouselFrameCount) / TITLE_SPIN_STEPS;
    if (frame >= titleCarouselFrameCount) frame = titleCarouselFrameCount - 1;
    srcX = (variant * titleCarouselFrameCount + frame) * TITLE_ROT_W;

    BltMaskBitMapRastPort(titleCarouselBM, srcX, 0,
                          &renderRP, dstX, dstY,
                          TITLE_ROT_W, TITLE_ROT_H,
                          (ABC | ABNC | ANBC),
                          titleCarouselMaskBM->Planes[0]);
}

static void DrawTitleSelectorGradient(WORD x, WORD y)
{
    static const UBYTE gradientPens[] = {4, 4, 6, 6, 8, 8, 1, 1, 0};
    WORD left = x - 2;
    WORD top = y - 2;
    WORD right = x + (ROBOT_W * TITLE_ROBOT_SCALE) + 1;
    WORD bottom = y + (ROBOT_H * TITLE_ROBOT_SCALE) + 1;
    WORD h = bottom - top + 1;
    WORD bandCount = sizeof(gradientPens) / sizeof(gradientPens[0]);
    WORD band;

    for (band = 0; band < bandCount; band++) {
        WORD y0 = top + (band * h) / bandCount;
        WORD y1 = top + ((band + 1) * h) / bandCount - 1;
        SetAPen(&renderRP, gradientPens[band]);
        RectFill(&renderRP, left, y0, right, y1);
    }
}

static void DrawTitleCarousel(void)
{
    char b[80];
    WORD slot;
    static const WORD slotX[ROBOT_VARIANTS] = {24, 64, 104, 144, 184, 224, 264};

    titleSpinPhase = (titleSpinPhase + 1) & (TITLE_SPIN_STEPS - 1);

    SetAPen(&renderRP, 1);
    RectFill(&renderRP, 0, TITLE_CAROUSEL_Y - 8, SCREEN_W - 1, SCREEN_H - 1);
    SetAPen(&renderRP, 8);
    RectFill(&renderRP, 0, TITLE_CAROUSEL_Y - 8, SCREEN_W - 1, TITLE_CAROUSEL_Y - 6);

    MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y - 24, "SELECT YOUR ROBOVAC", 7, 2);

    for (slot = 0; slot < ROBOT_VARIANTS; slot++) {
        WORD variant = selectedPlayerVariant + slot - (ROBOT_VARIANTS / 2);
        WORD x = slotX[slot];
        WORD y = TITLE_CAROUSEL_Y;
        while (variant < 0) variant += ROBOT_VARIANTS;
        while (variant >= ROBOT_VARIANTS) variant -= ROBOT_VARIANTS;

        if (slot == (ROBOT_VARIANTS / 2)) {
            SetAPen(&renderRP, 13);
            RectFill(&renderRP, x - 4, y - 4, x + (ROBOT_W * TITLE_ROBOT_SCALE) + 3, y + (ROBOT_H * TITLE_ROBOT_SCALE) + 3);
            DrawTitleSelectorGradient(x, y);
        } else {
            SetAPen(&renderRP, 8);
            RectFill(&renderRP, x - 2, y - 2, x + (ROBOT_W * TITLE_ROBOT_SCALE) + 1, y + (ROBOT_H * TITLE_ROBOT_SCALE) + 1);
            SetAPen(&renderRP, 0);
            RectFill(&renderRP, x, y, x + (ROBOT_W * TITLE_ROBOT_SCALE) - 1, y + (ROBOT_H * TITLE_ROBOT_SCALE) - 1);
        }

        DrawCachedTitleRobotSpin(variant, (titleSpinPhase + slot * 2) & (TITLE_SPIN_STEPS - 1), x, y);
        if (slot != (ROBOT_VARIANTS / 2)) {
            MiniText(&renderRP, x + 10, y + 35, robotVariantTags[variant], 7);
        }
    }

    snprintf(b, sizeof(b), "PLAYER %s", robotVariantNames[selectedPlayerVariant]);
    MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y + 50, b, 7, 2);
    MiniTextCentered(&renderRP, TITLE_CAROUSEL_Y + 70, "<- ARROWS CHOOSE ->", 13, 2);
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

        SetAPen(&renderRP, (i == 0) ? 4 : 8);
        RectFill(&renderRP, x, 12, x + slotW - 2, 29);
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, x + 1, 13, x + slotW - 3, 28);

        MiniText(&renderRP, x + 2, 14, RobotTag(i), (i == 0) ? 14 : 7);
        snprintf(scoreText, sizeof(scoreText), "%d", robots[i].score);
        MiniText(&renderRP, x + 21, 14, scoreText, 13);
        if (robots[i].powerMovesLeft > 0) {
            MiniText(&renderRP, x + 2, 26, "P", 14);
        } else if (robots[i].cleanStreak > 0) {
            char streakText[2];
            snprintf(streakText, sizeof(streakText), "%d", robots[i].cleanStreak);
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

static void DrawHud(void)
{
    char b[160];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    if (gameState == GAME_TITLE) {
        MiniTextCentered(&renderRP, 4, "ROBOVAC RESCUE", 7, 2);
        MiniTextCentered(&renderRP, 20, "LEFT/RIGHT: SELECT VAC", 13, 2);
        MiniTextCentered(&renderRP, 32, "1/2/3: AI RIVALS SPACE/R: START", 13, 2);
        MiniTextCentered(&renderRP, 44, "E/S/H DIFFICULTY B FIRES BOLT", 14, 2);
        return;
    }

    if (gameState == GAME_ROUND_END) {
        snprintf(b, sizeof(b), "ROUND WINNER: %s", RobotName(roundWinner));
        PutText(&renderRP, 14, 10, b, 13);
        DrawRobotHealthStrip();
        PutText(&renderRP, 76, 30, "Press R/Space for next round", 7);
        return;
    }

    if (gameState == GAME_MATCH_END) {
        snprintf(b, sizeof(b), "MATCH WINNER: %s", RobotName(finalWinner));
        PutText(&renderRP, 42, 10, b, 12);
        DrawRobotHealthStrip();
        PutText(&renderRP, 82, 30, "Press R/Space", 7);
        return;
    }

    if (lastPowerTicks > 0 && lastPowerText[0]) {
        PutText(&renderRP, 4, 8, lastPowerText, 14);
    } else {
        snprintf(b, sizeof(b), "R%d %s DIRT:%d MOVE:%d", roundIndex + 1, roomNames[roomType], dirtLeft, batteryCostPerMove);
        PutText(&renderRP, 4, 8, b, 7);
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

    if (effectTick > 0) {
        DrawIntroTitleBobEffect(dstX, dstY, effectTick);
        return;
    }

    BltBitMapRastPort(introTitleBM, 0, 0,
                      &renderRP, dstX, dstY,
                      introTitleW, introTitleH,
                      0xC0);
}

static void DrawFrame(void)
{
    WORD i;

    if (gameState == GAME_INTRO) {
        DrawIntroTitleImage();
        return;
    }

    if (gameState == GAME_TITLE) {
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
        DrawHud();
        MiniTextCentered(&renderRP, 86, "A TINY AMIGA ROBOT CLEANER", 7, 2);
        MiniTextCentered(&renderRP, 108, "CLEAN MORE DIRT THAN", 9, 2);
        MiniTextCentered(&renderRP, 122, "THE AI ROBOTS", 9, 2);
        MiniTextCentered(&renderRP, 142, "EVERY 5 DIRT EARNS POWERS", 14, 2);
        DrawTitleCarousel();
        return;
    }

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

    for (i = 1; i < robotCount; i++) {
        DrawRobotBob(i);
    }
    DrawRobotBob(0);
    DrawPlayerBolt();
}

static void PresentFrame(void)
{
    BltBitMap(renderBM, 0, 0,
              scr->RastPort.BitMap, 0, 0,
              SCREEN_W, SCREEN_H,
              0xC0, 0xFF, NULL);
}

/* -------------------------------------------------------------------------
 * Input
 * ------------------------------------------------------------------------- */

static void StartWithRivals(WORD rivals)
{
    WORD i;
    aiRivals = rivals;
    if (aiRivals < 1) aiRivals = 1;
    if (aiRivals > 9) aiRivals = 9;
    roundIndex = 0;
    for (i = 0; i < MAX_ROBOTS; i++) { roundWins[i] = 0; totalScores[i] = 0; }
    ResetLevel();
}


static void FirePlayerBolt(void)
{
    WORD dirX = 0, dirY = 0;

    if (gameState != GAME_PLAYING) return;
    if (robots[0].battery < 2 && !(robots[0].powerType == POWER_BOLT && robots[0].powerMovesLeft > 0)) return;
    if (playerBolt.active) return;

    if (robots[0].moving) {
        dirX = robots[0].targetX - robots[0].tileX;
        dirY = robots[0].targetY - robots[0].tileY;
    } else if (keyLeft) dirX = -1;
    else if (keyRight) dirX = 1;
    else if (keyUp) dirY = -1;
    else if (keyDown) dirY = 1;
    else { dirX = playerFacingX; dirY = playerFacingY; }
    if (dirX == 0 && dirY == 0) dirY = -1;

    if (!(robots[0].powerType == POWER_BOLT && robots[0].powerMovesLeft > 0)) {
        robots[0].battery -= 2;
    }
    playerFacingX = dirX;
    playerFacingY = dirY;
    playerBolt.active = TRUE;
    playerBolt.dirX = dirX;
    playerBolt.dirY = dirY;
    playerBolt.px = TO_FP(robots[0].tileX * TILE_SIZE);
    playerBolt.py = TO_FP(robots[0].tileY * TILE_SIZE);
    playerBolt.ttl = 24;
}

static void StepPlayerBolt(void)
{
    WORD tx, ty, i;

    if (!playerBolt.active) return;
    if (playerBolt.ttl-- <= 0) { playerBolt.active = FALSE; return; }

    playerBolt.px += playerBolt.dirX * (5 * FP_ONE);
    playerBolt.py += playerBolt.dirY * (5 * FP_ONE);
    tx = FP_TO_INT(playerBolt.px) / TILE_SIZE;
    ty = FP_TO_INT(playerBolt.py) / TILE_SIZE;

    if (IsBlocked(tx, ty)) { playerBolt.active = FALSE; return; }

    for (i = 1; i < robotCount; i++) {
        if (AbsW(robots[i].tileX - tx) + AbsW(robots[i].tileY - ty) <= 1) {
            robots[i].stunTicks = 250;
            robots[i].battery -= 5;
            if (robots[i].battery < 0) robots[i].battery = 0;
            playerBolt.active = FALSE;
            return;
        }
    }
}

static void HandleRawKey(UWORD rawCode)
{
    BOOL keyUpEvent = (rawCode & 0x80) ? TRUE : FALSE;
    UWORD code = rawCode & 0x7F;

    if (code == RAW_ESC && !keyUpEvent) {
        running = FALSE;
        return;
    }

    if (!keyUpEvent && gameState == GAME_INTRO) {
        EnterTitleScreen();
        return;
    }

    if (!keyUpEvent && gameState == GAME_TITLE) {
        if (code == RAW_LEFT) { selectedPlayerVariant--; if (selectedPlayerVariant < 0) selectedPlayerVariant = ROBOT_VARIANTS - 1; return; }
        if (code == RAW_RIGHT) { selectedPlayerVariant++; if (selectedPlayerVariant >= ROBOT_VARIANTS) selectedPlayerVariant = 0; return; }
        if (code == RAW_1) { StartWithRivals(1); return; }
        if (code == RAW_2) { StartWithRivals(2); return; }
        if (code == RAW_3) { StartWithRivals(3); return; }
        if (code == RAW_O) { StartWithRivals(9); return; }
        if (code == RAW_E) { maxBattery = 110; batteryCostPerMove = 1; return; }
        if (code == RAW_S) { maxBattery = 55; batteryCostPerMove = 2; return; }
        if (code == RAW_H) { maxBattery = 36; batteryCostPerMove = 3; return; }
    }

    if (!keyUpEvent && code == RAW_B) { FirePlayerBolt(); return; }

    if (!keyUpEvent && (code == RAW_R || code == RAW_SPACE)) {
        if (gameState == GAME_PLAYING) { ResetLevel(); }
        else if (gameState == GAME_ROUND_END) { roundIndex++; ResetLevel(); }
        else { WORD i; roundIndex = 0; for (i=0;i<MAX_ROBOTS;i++){roundWins[i]=0; totalScores[i]=0;} ResetLevel(); }
        return;
    }

    switch (code) {
        case RAW_LEFT:  keyLeft  = !keyUpEvent; break;
        case RAW_RIGHT: keyRight = !keyUpEvent; break;
        case RAW_UP:    keyUp    = !keyUpEvent; break;
        case RAW_DOWN:  keyDown  = !keyUpEvent; break;
        default: break;
    }
}

static void PollWindowMessages(void)
{
    struct IntuiMessage *msg;

    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;

        ReplyMsg((struct Message *)msg);

        if (cls == IDCMP_RAWKEY) {
            HandleRawKey(code);
        } else if (cls == IDCMP_MOUSEBUTTONS) {
            if (code == MENUDOWN) running = FALSE;
            if (code == SELECTDOWN && gameState == GAME_INTRO) EnterTitleScreen();
            if (code == SELECTDOWN && gameState == GAME_TITLE) StartWithRivals(aiRivals);
        }
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
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    InitRastPort(&renderRP);
    renderRP.BitMap = renderBM;

    InitRastPort(&roomRP);
    roomRP.BitMap = roomBM;

    if (!InitTileCache()) {
        FreeBitMap(roomBM);
        roomBM = NULL;
        FreeBitMap(renderBM);
        renderBM = NULL;
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    if (!InitRobotBobs()) {
        printf("Robot BOB cache allocation failed; fallback drawing enabled\n");
    }

    if (!LoadIntroTitleImage()) {
        printf("Optional PROGDIR:tiles/robovac-title.iff not loaded; starting at menu\n");
    }

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
        FreeIntroTitleImage();
        FreeTitleCarouselCache();
        if (robotCacheBM) FreeBitMap(robotCacheBM);
        if (robotMaskBM) FreeBitMap(robotMaskBM);
        if (tileCacheBM) FreeBitMap(tileCacheBM);
        if (roomBM) FreeBitMap(roomBM);
        if (renderBM) FreeBitMap(renderBM);
        robotCacheBM = NULL;
        robotMaskBM = NULL;
        tileCacheBM = NULL;
        roomBM = NULL;
        renderBM = NULL;
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    return TRUE;
}

static void CloseGameScreen(void)
{
    if (win) {
        CloseWindow(win);
        win = NULL;
    }

    FreeIntroTitleImage();
    FreeTitleCarouselCache();

    if (robotCacheBM) {
        FreeBitMap(robotCacheBM);
        robotCacheBM = NULL;
    }

    if (robotMaskBM) {
        FreeBitMap(robotMaskBM);
        robotMaskBM = NULL;
    }

    if (tileCacheBM) {
        FreeBitMap(tileCacheBM);
        tileCacheBM = NULL;
    }

    if (roomBM) {
        FreeBitMap(roomBM);
        roomBM = NULL;
    }

    if (renderBM) {
        FreeBitMap(renderBM);
        renderBM = NULL;
    }

    if (scr) {
        CloseScreen(scr);
        scr = NULL;
    }
}

int main(void)
{
    printf("RoboVac Rescue smooth AI prototype starting...\n");

    if (!OpenGameScreen()) {
        return 20;
    }

    gameState = introTitleBM ? GAME_INTRO : GAME_TITLE;

    while (running) {
        PollWindowMessages();
        StepGame();
        DrawFrame();
        WaitTOF();
        PresentFrame();
    }

    CloseGameScreen();

    printf("RoboVac Rescue ended.\n");
    return 0;
}
