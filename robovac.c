/*
 * robovac_opt_ai_smooth.c - RoboVac Rescue optimized custom-screen engine
 *
 * Adds:
 * - Smooth pixel movement between tiles using fixed-point positions
 * - Up to 4 robots
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

#define ROBOT_W     16
#define ROBOT_H     16
#define MAX_ROBOTS  4

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

#define GAME_TITLE      0
#define GAME_PLAYING    1
#define GAME_ROUND_END  2
#define GAME_MATCH_END  3

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

struct Robot {
    WORD tileX;
    WORD tileY;
    WORD targetX;
    WORD targetY;
    LONG px;
    LONG py;
    WORD battery;
    WORD score;
    BOOL ai;
    BOOL moving;
    UBYTE spriteIndex;
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

static UBYTE map[MAP_H][MAP_W];

static struct Robot robots[MAX_ROBOTS];
static WORD robotCount = 2;
static WORD aiRivals = 1;

static WORD dirtLeft = 0;
static WORD moves = 0;
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

static ULONG rng = 0x1234ABCD;

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
    SPR_FULLY_CHARGED = 7,
    SPR_STATE_COUNT = 8
};

static const char *roomNames[5] = {
    "Living Room", "Dining Room", "Kitchen", "Bathroom", "Bedroom"
};

static const WORD roundDirtTargets[5] = {14, 20, 26, 32, 38};

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
        LoadRGB4(&scr->ViewPort, palette, maxCols);
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

static BOOL LoadRobotSheetIntoCache(void)
{
    Object *dto = NULL;
    struct BitMapHeader *bmhd = NULL;
    struct BitMap *srcBM = NULL;
    UBYTE *cRegs = NULL;
    LONG numCols = 0;
    LONG i;

    dto = NewDTObject("PROGDIR:tiles/robovac-tiles.iff",
                      DTA_GroupID, GID_PICTURE,
                      PDTA_Remap, FALSE,
                      TAG_DONE);
    if (!dto) return FALSE;

    if (DoDTMethod(dto, NULL, NULL, DTM_PROCLAYOUT, 0L, TRUE) != 0) {
        GetDTAttrs(dto,
                   PDTA_BitMapHeader, (ULONG)&bmhd,
                   PDTA_DestBitMap, (ULONG)&srcBM,
                   PDTA_CRegs, (ULONG)&cRegs,
                   PDTA_NumColors, (ULONG)&numCols,
                   TAG_DONE);
    }

    if (!bmhd || !srcBM || bmhd->bmh_Width < 128 || bmhd->bmh_Height != 16) {
        DisposeDTObject(dto);
        return FALSE;
    }

    for (i = 0; i < SPR_STATE_COUNT; i++) {
        BltBitMap(srcBM, i * ROBOT_W, 0, robotCacheBM, i * ROBOT_W, 0, ROBOT_W, ROBOT_H, 0xC0, 0xFF, NULL);
    }

    if (cRegs && numCols > 0) {
        LONG maxCols = (numCols > 16) ? 16 : numCols;
        for (i = 0; i < maxCols; i++) {
            palette[16 + i] = (UWORD)(((cRegs[i * 3 + 0] >> 28) << 8) |
                                      ((cRegs[i * 3 + 1] >> 28) << 4) |
                                       (cRegs[i * 3 + 2] >> 28));
        }
        LoadRGB4(&scr->ViewPort, palette, 32);
    }

    DisposeDTObject(dto);
    return TRUE;
}

static BOOL InitRobotBobs(void)
{
    struct RastPort maskRP;

    robotCacheBM = AllocBitMap(ROBOT_W * SPR_STATE_COUNT, ROBOT_H, DEPTH,
                               BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    robotMaskBM = AllocBitMap(ROBOT_W, ROBOT_H, 1,
                              BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);

    if (!robotCacheBM || !robotMaskBM) {
        printf("Could not allocate robot BOB cache/mask\n");
        return FALSE;
    }

    InitRastPort(&robotRP);
    robotRP.BitMap = robotCacheBM;

    if (!LoadRobotSheetIntoCache()) {
        printf("Could not load PROGDIR:tiles/robovac-tiles.iff (need 128x16, 16 colours)\n");
        return FALSE;
    }

    InitRastPort(&maskRP);
    maskRP.BitMap = robotMaskBM;

    SetAPen(&maskRP, 1);
    RectFill(&maskRP, 0, 0, ROBOT_W - 1, ROBOT_H - 1);

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
    srcX = robots[id].spriteIndex * ROBOT_W;

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
}

static void InitRobots(void)
{
    WORD i;
    static const WORD sx[MAX_ROBOTS] = {1, MAP_W - 2, 1, MAP_W - 2};
    static const WORD sy[MAX_ROBOTS] = {1, 1, MAP_H - 2, MAP_H - 2};

    robotCount = 1 + aiRivals;
    if (robotCount < 1) robotCount = 1;
    if (robotCount > MAX_ROBOTS) robotCount = MAX_ROBOTS;

    for (i = 0; i < robotCount; i++) {
        SetRobotTile(i, sx[i], sy[i]);
        robots[i].battery = 110;
        robots[i].score = 0;
        robots[i].ai = (i != 0) ? TRUE : FALSE;
        robots[i].spriteIndex = SPR_READY;
    }

    moves = 0;
}


static BOOL IsRobotDock(WORD id, WORD tx, WORD ty)
{
    static const WORD sx[MAX_ROBOTS] = {1, MAP_W - 2, 1, MAP_W - 2};
    static const WORD sy[MAX_ROBOTS] = {1, 1, MAP_H - 2, MAP_H - 2};
    if (id < 0 || id >= MAX_ROBOTS) return FALSE;
    return (tx == sx[id] && ty == sy[id]) ? TRUE : FALSE;
}

static BOOL ValidDirtTile(WORD tx, WORD ty)
{
    if (map[ty][tx] != TILE_FLOOR) return FALSE;
    if (IsRobotDock(0, tx, ty) || IsRobotDock(1, tx, ty) || IsRobotDock(2, tx, ty) || IsRobotDock(3, tx, ty)) return FALSE;
    return TRUE;
}

static void SpawnRoundDirt(WORD count)
{
    WORD placed = 0, tries = 0;
    while (placed < count && tries < 2000) {
        WORD x = 1 + RandRange(MAP_W - 2);
        WORD y = 1 + RandRange(MAP_H - 2);
        if (ValidDirtTile(x, y)) { map[y][x] = TILE_DIRT; placed++; }
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
    gameState = GAME_PLAYING;

    InitRobots();
    CountDirt();
    BuildRoomBuffer();
}

static BOOL StartRobotMove(WORD id, WORD dx, WORD dy)
{
    WORD nx;
    WORD ny;

    if (id < 0 || id >= robotCount) return FALSE;
    if (robots[id].moving) return FALSE;
    if (robots[id].battery <= 0) return FALSE;

    nx = robots[id].tileX + dx;
    ny = robots[id].tileY + dy;

    if (IsBlocked(nx, ny)) return FALSE;
    if (RobotAtTile(nx, ny, id)) return FALSE;

    robots[id].targetX = nx;
    robots[id].targetY = ny;
    robots[id].moving = TRUE;
    if (id == 0) {
        if (dx < 0) robots[id].spriteIndex = SPR_LEFT;
        else if (dx > 0) robots[id].spriteIndex = SPR_RIGHT;
        else if (dy < 0) robots[id].spriteIndex = SPR_UP;
        else if (dy > 0) robots[id].spriteIndex = SPR_DOWN;
    }
    robots[id].battery--;

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

    robots[id].tileX = tx;
    robots[id].tileY = ty;
    robots[id].px = TO_FP(tx * TILE_SIZE);
    robots[id].py = TO_FP(ty * TILE_SIZE);
    robots[id].moving = FALSE;

    if (map[ty][tx] == TILE_DIRT) {
        map[ty][tx] = TILE_FLOOR;
        if (dirtLeft > 0) dirtLeft--;

        robots[id].score++;
        UpdateRoomTile(tx, ty);
    }

    if (map[ty][tx] == TILE_DOCK && IsRobotDock(id, tx, ty)) {
        robots[id].battery = 110;
        if (id == 0) robots[id].spriteIndex = SPR_FULLY_CHARGED;
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

    if (dx > 0) {
        robots[id].px += (dx < MOVE_SPEED) ? dx : MOVE_SPEED;
    } else if (dx < 0) {
        robots[id].px -= ((-dx) < MOVE_SPEED) ? (-dx) : MOVE_SPEED;
    }

    if (dy > 0) {
        robots[id].py += (dy < MOVE_SPEED) ? dy : MOVE_SPEED;
    } else if (dy < 0) {
        robots[id].py -= ((-dy) < MOVE_SPEED) ? (-dy) : MOVE_SPEED;
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
    WORD dx = 0;
    WORD dy = 0;
    WORD tryDir;

    if (id <= 0 || id >= robotCount) return;
    if (robots[id].moving) return;
    if (robots[id].battery <= 0) return;

    curX = robots[id].tileX;
    curY = robots[id].tileY;

    if (robots[id].battery <= 25) {
        bestX = (id == 1 || id == 3) ? (MAP_W - 2) : 1;
        bestY = (id >= 2) ? (MAP_H - 2) : 1;
    } else {
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
        if (AbsW(bestX - curX) > AbsW(bestY - curY)) {
            dx = (bestX > curX) ? 1 : -1;
        } else if (bestY != curY) {
            dy = (bestY > curY) ? 1 : -1;
        } else if (bestX != curX) {
            dx = (bestX > curX) ? 1 : -1;
        }

        if (StartRobotMove(id, dx, dy)) return;

        /* Try alternate axis if blocked */
        if (dx != 0 && bestY != curY) {
            dx = 0;
            dy = (bestY > curY) ? 1 : -1;
            if (StartRobotMove(id, dx, dy)) return;
        } else if (dy != 0 && bestX != curX) {
            dy = 0;
            dx = (bestX > curX) ? 1 : -1;
            if (StartRobotMove(id, dx, dy)) return;
        }
    }

    /* Fallback random movement */
    for (tryDir = 0; tryDir < 4; tryDir++) {
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

static void StepGame(void)
{
    WORD i;

    if (gameState != GAME_PLAYING) return;

    for (i = 0; i < robotCount; i++) {
        StepRobotMovement(i);
    }

    ChoosePlayerMove();

    for (i = 1; i < robotCount; i++) {
        ChooseAiMove(i);
    }

    if (robots[0].battery <= 25) {
        robots[0].spriteIndex = SPR_LOW_BATTERY;
    } else if (map[robots[0].tileY][robots[0].tileX] == TILE_DOCK && !robots[0].moving) {
        robots[0].spriteIndex = (robots[0].battery >= 110) ? SPR_FULLY_CHARGED : SPR_CHARGING;
    } else if (!robots[0].moving) {
        robots[0].spriteIndex = SPR_READY;
    }

    CheckEndState();
}

/* -------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------- */

static void DrawHud(void)
{
    char b[160];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    if (gameState == GAME_TITLE) {
        PutText(&renderRP, 72, 10, "ROBOVAC RESCUE", 7);
        PutText(&renderRP, 20, 22, "1/2/3: AI rivals  SPACE/R: start", 13);
        return;
    }

    if (gameState == GAME_ROUND_END) {
        snprintf(b, sizeof(b), "ROUND OVER You:%d  A1:%d A2:%d A3:%d", robots[0].score,
                 (robotCount > 1) ? robots[1].score : 0,
                 (robotCount > 2) ? robots[2].score : 0,
                 (robotCount > 3) ? robots[3].score : 0);
        PutText(&renderRP, 14, 12, b, 13);
        PutText(&renderRP, 76, 24, "Press R/Space for next round", 7);
        return;
    }

    if (gameState == GAME_MATCH_END) {
        snprintf(b, sizeof(b), "MATCH WINNER: Robot %d  Press R/Space", finalWinner);
        PutText(&renderRP, 42, 12, b, 12);
        snprintf(b, sizeof(b), "You:%d A1:%d A2:%d A3:%d", robots[0].score,
                 (robotCount > 1) ? robots[1].score : 0,
                 (robotCount > 2) ? robots[2].score : 0,
                 (robotCount > 3) ? robots[3].score : 0);
        PutText(&renderRP, 72, 24, b, 7);
        return;
    }

    snprintf(b, sizeof(b), "R%d %s Dirt:%d P:%d(%d) A1:%d(%d)",
             roundIndex + 1, roomNames[roomType], dirtLeft, robots[0].score, robots[0].battery,
             (robotCount > 1) ? robots[1].score : 0,
             (robotCount > 1) ? robots[1].battery : 0);
    PutText(&renderRP, 4, 12, b, 7);

    SetAPen(&renderRP, 1);
    RectFill(&renderRP, 222, 20, 312, 26);

    SetAPen(&renderRP, robots[0].battery > 25 ? 13 : 12);
    RectFill(&renderRP, 224, 22, 224 + (robots[0].battery * 86 / 110), 24);
}

static void DrawFrame(void)
{
    WORD i;

    if (gameState == GAME_TITLE) {
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
        DrawHud();
        PutText(&renderRP, 42, 112, "A tiny Amiga robot cleaner", 7);
        PutText(&renderRP, 54, 132, "Clean more dirt than", 9);
        PutText(&renderRP, 82, 144, "the AI robots", 9);
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
    if (aiRivals > 3) aiRivals = 3;
    roundIndex = 0;
    for (i = 0; i < MAX_ROBOTS; i++) { roundWins[i] = 0; totalScores[i] = 0; }
    ResetLevel();
}

static void HandleRawKey(UWORD rawCode)
{
    BOOL keyUpEvent = (rawCode & 0x80) ? TRUE : FALSE;
    UWORD code = rawCode & 0x7F;

    if (code == RAW_ESC && !keyUpEvent) {
        running = FALSE;
        return;
    }

    if (!keyUpEvent && gameState == GAME_TITLE) {
        if (code == RAW_1) { StartWithRivals(1); return; }
        if (code == RAW_2) { StartWithRivals(2); return; }
        if (code == RAW_3) { StartWithRivals(3); return; }
    }

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
            if (code == SELECTDOWN && gameState == GAME_TITLE) ResetLevel();
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

    gameState = GAME_TITLE;

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
