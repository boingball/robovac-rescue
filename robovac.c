/*
 * robovac_base.c - Top-down robot hoover game prototype for AmigaOS
 *
 * Build:
 *   m68k-amigaos-gcc -s -Os -o robovac robovac_base.c
 *
 * Controls:
 *   Arrow keys - move robot
 *   R          - reset level
 *   Esc/Close  - quit
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

static const char __attribute__((used)) min_stack[] = "$STACK:65536";

#define WIN_W       384
#define WIN_H       300

#define TILE_SIZE   16
#define MAP_W       20
#define MAP_H       15

#define MAP_X       16
#define MAP_Y       32

#define ROBOT_W     14
#define ROBOT_H     14

#define START_X     1
#define START_Y     1

#define TILE_FLOOR  0
#define TILE_WALL   1
#define TILE_DIRT   2
#define TILE_DOCK   3
#define TILE_TABLE  4

#define RAW_ESC     0x45
#define RAW_LEFT    0x4F
#define RAW_RIGHT   0x4E
#define RAW_UP      0x4C
#define RAW_DOWN    0x4D
#define RAW_R       0x13

static struct Window *win = NULL;
static struct RastPort *rp = NULL;

static UBYTE map[MAP_H][MAP_W];

static WORD robotTileX = START_X;
static WORD robotTileY = START_Y;
static WORD dirtLeft = 0;
static WORD battery = 100;
static WORD moves = 0;
static BOOL running = TRUE;
static BOOL levelWon = FALSE;
static BOOL batteryDead = FALSE;

static const char *levelData[MAP_H] = {
    "####################",
    "#D....*......*.....#",
    "#..TTT.......TTT...#",
    "#..T.....*.....T...#",
    "#..T...........T...#",
    "#......######......#",
    "#.*....#....#....*.#",
    "#......#....#......#",
    "#......######......#",
    "#..T...........T...#",
    "#..T.....*.....T...#",
    "#..TTT.......TTT...#",
    "#.....*......*.....#",
    "#..................#",
    "####################"
};

static void CountDirt(void)
{
    WORD x, y;
    dirtLeft = 0;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            if (map[y][x] == TILE_DIRT) dirtLeft++;
        }
    }
}

static void ResetLevel(void)
{
    WORD x, y;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            char c = levelData[y][x];

            switch (c) {
                case '#': map[y][x] = TILE_WALL; break;
                case '*': map[y][x] = TILE_DIRT; break;
                case 'D': map[y][x] = TILE_DOCK; break;
                case 'T': map[y][x] = TILE_TABLE; break;
                default:  map[y][x] = TILE_FLOOR; break;
            }
        }
    }

    robotTileX = START_X;
    robotTileY = START_Y;
    battery = 100;
    moves = 0;
    levelWon = FALSE;
    batteryDead = FALSE;

    CountDirt();
}

static BOOL IsBlocked(WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return TRUE;
    return (map[ty][tx] == TILE_WALL || map[ty][tx] == TILE_TABLE);
}

static void DrawTile(WORD tx, WORD ty)
{
    WORD sx = MAP_X + tx * TILE_SIZE;
    WORD sy = MAP_Y + ty * TILE_SIZE;
    UBYTE tile = map[ty][tx];

    switch (tile) {
        case TILE_WALL:
            SetAPen(rp, 1);
            RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            break;

        case TILE_TABLE:
            SetAPen(rp, 5);
            RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            SetAPen(rp, 1);
            Move(rp, sx + 2, sy + 2);
            Draw(rp, sx + TILE_SIZE - 3, sy + TILE_SIZE - 3);
            break;

        case TILE_DIRT:
            SetAPen(rp, 0);
            RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            SetAPen(rp, 3);
            RectFill(rp, sx + 5, sy + 5, sx + 10, sy + 10);
            SetAPen(rp, 2);
            WritePixel(rp, sx + 4, sy + 7);
            WritePixel(rp, sx + 11, sy + 9);
            break;

        case TILE_DOCK:
            SetAPen(rp, 4);
            RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            SetAPen(rp, 1);
            Move(rp, sx + 3, sy + 8);
            Draw(rp, sx + 12, sy + 8);
            break;

        case TILE_FLOOR:
        default:
            SetAPen(rp, 0);
            RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            break;
    }

    SetAPen(rp, 8);
    Move(rp, sx, sy);
    Draw(rp, sx + TILE_SIZE - 1, sy);
    Draw(rp, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
    Draw(rp, sx, sy + TILE_SIZE - 1);
    Draw(rp, sx, sy);
}

static void DrawMap(void)
{
    WORD x, y;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            DrawTile(x, y);
        }
    }
}

static void DrawRobot(void)
{
    WORD sx = MAP_X + robotTileX * TILE_SIZE + 1;
    WORD sy = MAP_Y + robotTileY * TILE_SIZE + 1;

    SetAPen(rp, 7);
    RectFill(rp, sx, sy, sx + ROBOT_W, sy + ROBOT_H);

    SetAPen(rp, 1);
    Move(rp, sx + 3, sy + ROBOT_H - 2);
    Draw(rp, sx + ROBOT_W - 3, sy + ROBOT_H - 2);

    SetAPen(rp, 2);
    WritePixel(rp, sx + 4, sy + 4);
    WritePixel(rp, sx + 9, sy + 4);

    SetAPen(rp, 4);
    Move(rp, sx + 3, sy + 10);
    Draw(rp, sx + 11, sy + 10);
}

static void DrawStatus(void)
{
    char buf[128];

    SetAPen(rp, 0);
    RectFill(rp, 0, 0, WIN_W - 1, 28);

    SetAPen(rp, 7);
    Move(rp, 8, 12);

    if (levelWon) {
        snprintf(buf, sizeof(buf), "ROOM CLEAN! Moves:%d  Press R to reset", moves);
    } else if (batteryDead) {
        snprintf(buf, sizeof(buf), "BATTERY FLAT! Dirt:%d  Press R to reset", dirtLeft);
    } else {
        snprintf(buf, sizeof(buf), "RoboVac Rescue  Dirt:%d  Battery:%d%%  Moves:%d", dirtLeft, battery, moves);
    }

    Text(rp, (STRPTR)buf, strlen(buf));
}

static void RedrawAll(void)
{
    if (!rp) return;

    SetAPen(rp, 0);
    RectFill(rp, 0, 0, WIN_W - 1, WIN_H - 1);

    DrawStatus();
    DrawMap();
    DrawRobot();
}

static void TryMoveRobot(WORD dx, WORD dy)
{
    WORD nx, ny;

    if (levelWon || batteryDead) return;

    nx = robotTileX + dx;
    ny = robotTileY + dy;

    if (IsBlocked(nx, ny)) return;

    DrawTile(robotTileX, robotTileY);

    robotTileX = nx;
    robotTileY = ny;
    moves++;

    if (battery > 0) battery--;

    if (map[robotTileY][robotTileX] == TILE_DIRT) {
        map[robotTileY][robotTileX] = TILE_FLOOR;
        if (dirtLeft > 0) dirtLeft--;
    }

    if (map[robotTileY][robotTileX] == TILE_DOCK) {
        battery = 100;
    }

    if (dirtLeft <= 0) {
        levelWon = TRUE;
    }

    if (battery <= 0 && !levelWon) {
        batteryDead = TRUE;
    }

    DrawStatus();
    DrawTile(robotTileX, robotTileY);
    DrawRobot();
}

static BOOL OpenGameWindow(void)
{
    win = OpenWindowTags(NULL,
        WA_Title,       (ULONG)"RoboVac Rescue - Amiga prototype",
        WA_Width,       WIN_W,
        WA_Height,      WIN_H,
        WA_DepthGadget, TRUE,
        WA_DragBar,     TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_REFRESHWINDOW,
        TAG_END);

    if (!win) {
        printf("Could not open window\n");
        return FALSE;
    }

    rp = win->RPort;
    return TRUE;
}

static void CloseGameWindow(void)
{
    if (win) {
        CloseWindow(win);
        win = NULL;
        rp = NULL;
    }
}

static void HandleRawKey(UWORD code)
{
    if (code & 0x80) return;

    switch (code) {
        case RAW_ESC:
            running = FALSE;
            break;

        case RAW_LEFT:
            TryMoveRobot(-1, 0);
            break;

        case RAW_RIGHT:
            TryMoveRobot(1, 0);
            break;

        case RAW_UP:
            TryMoveRobot(0, -1);
            break;

        case RAW_DOWN:
            TryMoveRobot(0, 1);
            break;

        case RAW_R:
            ResetLevel();
            RedrawAll();
            break;

        default:
            break;
    }
}

static void MainLoop(void)
{
    ULONG winSig = 1L << win->UserPort->mp_SigBit;

    while (running) {
        ULONG sigs = Wait(winSig | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) {
            running = FALSE;
        }

        if (sigs & winSig) {
            struct IntuiMessage *msg;

            while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                ULONG cls = msg->Class;
                UWORD code = msg->Code;

                ReplyMsg((struct Message *)msg);

                if (cls == IDCMP_CLOSEWINDOW) {
                    running = FALSE;
                } else if (cls == IDCMP_RAWKEY) {
                    HandleRawKey(code);
                } else if (cls == IDCMP_REFRESHWINDOW) {
                    BeginRefresh(win);
                    RedrawAll();
                    EndRefresh(win, TRUE);
                }
            }
        }
    }
}

int main(void)
{
    printf("RoboVac Rescue prototype starting...\n");

    ResetLevel();

    if (!OpenGameWindow()) {
        return 20;
    }

    RedrawAll();
    MainLoop();

    CloseGameWindow();

    printf("RoboVac Rescue prototype ended.\n");
    return 0;
}
