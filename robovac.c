/*
 * robovac.c - Top-down robot hoover game prototype for AmigaOS
 *
 * Build:
 *   m68k-amigaos-gcc -s -Os -o robovac robovac.c
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

#define GAME_TITLE      0
#define GAME_PLAYING    1
#define GAME_WON        2
#define GAME_BATTERY    3

static struct Window *win = NULL;
static struct RastPort *rp = NULL;

static UBYTE map[MAP_H][MAP_W];

static WORD robotTileX = START_X;
static WORD robotTileY = START_Y;
static WORD robotPxX = 0;
static WORD robotPxY = 0;
static WORD dirtLeft = 0;
static WORD battery = 100;
static WORD moves = 0;
static WORD gameState = GAME_TITLE;
static BOOL running = TRUE;

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
    robotPxX = MAP_X + robotTileX * TILE_SIZE + 1;
    robotPxY = MAP_Y + robotTileY * TILE_SIZE + 1;
    battery = 100;
    moves = 0;
    gameState = GAME_PLAYING;

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

    SetAPen(rp, 0);
    RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);

    if (tile == TILE_FLOOR || tile == TILE_DIRT || tile == TILE_DOCK) {
        SetAPen(rp, 8);
        Move(rp, sx + 8, sy);
        Draw(rp, sx + 8, sy + TILE_SIZE - 1);
        Move(rp, sx, sy + 8);
        Draw(rp, sx + TILE_SIZE - 1, sy + 8);
    }

    switch (tile) {
        case TILE_WALL:
            SetAPen(rp, 1);
            RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
            SetAPen(rp, 7);
            Move(rp, sx, sy + 1);
            Draw(rp, sx + TILE_SIZE - 1, sy + 1);
            SetAPen(rp, 5);
            Move(rp, sx, sy + TILE_SIZE - 2);
            Draw(rp, sx + TILE_SIZE - 1, sy + TILE_SIZE - 2);
            break;

        case TILE_TABLE:
            SetAPen(rp, 6);
            RectFill(rp, sx + 2, sy + 2, sx + TILE_SIZE - 3, sy + TILE_SIZE - 3);
            SetAPen(rp, 1);
            Move(rp, sx + 2, sy + 2);
            Draw(rp, sx + TILE_SIZE - 3, sy + 2);
            Draw(rp, sx + TILE_SIZE - 3, sy + TILE_SIZE - 3);
            Draw(rp, sx + 2, sy + TILE_SIZE - 3);
            Draw(rp, sx + 2, sy + 2);
            SetAPen(rp, 3);
            WritePixel(rp, sx + 4, sy + 4);
            WritePixel(rp, sx + TILE_SIZE - 5, sy + 4);
            WritePixel(rp, sx + 4, sy + TILE_SIZE - 5);
            WritePixel(rp, sx + TILE_SIZE - 5, sy + TILE_SIZE - 5);
            break;

        case TILE_DIRT:
            SetAPen(rp, 3);
            RectFill(rp, sx + 5, sy + 5, sx + 10, sy + 10);
            SetAPen(rp, 2);
            WritePixel(rp, sx + 4, sy + 7);
            WritePixel(rp, sx + 11, sy + 9);
            WritePixel(rp, sx + 8, sy + 4);
            WritePixel(rp, sx + 7, sy + 11);
            break;

        case TILE_DOCK:
            SetAPen(rp, 4);
            RectFill(rp, sx + 1, sy + 3, sx + TILE_SIZE - 2, sy + TILE_SIZE - 2);
            SetAPen(rp, 7);
            Move(rp, sx + 3, sy + 6);
            Draw(rp, sx + TILE_SIZE - 4, sy + 6);
            SetAPen(rp, 2);
            RectFill(rp, sx + 5, sy + 9, sx + TILE_SIZE - 6, sy + 12);
            break;

        default:
            break;
    }
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
    WORD sx = robotPxX;
    WORD sy = robotPxY;

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

    if (gameState == GAME_WON) {
        snprintf(buf, sizeof(buf), "ROOM CLEAN! Moves:%d  R:restart Esc:quit", moves);
    } else if (gameState == GAME_BATTERY) {
        snprintf(buf, sizeof(buf), "BATTERY FLAT! Dirt:%d  R:restart Esc:quit", dirtLeft);
    } else {
        snprintf(buf, sizeof(buf), "RoboVac Rescue  Dirt:%d  Battery:%d%%  Moves:%d", dirtLeft, battery, moves);
    }

    Text(rp, (STRPTR)buf, strlen(buf));
}

static void DrawTitleScreen(void)
{
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, WIN_W - 1, WIN_H - 1);

    SetAPen(rp, 7);
    Move(rp, 92, 70);
    Text(rp, (STRPTR)"ROBOVAC RESCUE", 14);

    SetAPen(rp, 6);
    Move(rp, 70, 102);
    Text(rp, (STRPTR)"Clean all dirt before battery runs out.", 36);

    SetAPen(rp, 5);
    Move(rp, 64, 134);
    Text(rp, (STRPTR)"Arrow keys: Move", 16);
    Move(rp, 64, 150);
    Text(rp, (STRPTR)"R: Restart room", 15);
    Move(rp, 64, 166);
    Text(rp, (STRPTR)"Esc: Quit", 9);

    SetAPen(rp, 3);
    Move(rp, 58, 206);
    Text(rp, (STRPTR)"Press any arrow key or R to start", 33);
}

static void RedrawAll(void)
{
    if (!rp) return;

    SetAPen(rp, 0);
    RectFill(rp, 0, 0, WIN_W - 1, WIN_H - 1);

    if (gameState == GAME_TITLE) {
        DrawTitleScreen();
        return;
    }

    DrawStatus();
    DrawMap();
    DrawRobot();
}

static void AnimateMove(WORD oldTileX, WORD oldTileY, WORD targetPxX, WORD targetPxY)
{
    WORD dx = targetPxX - robotPxX;
    WORD dy = targetPxY - robotPxY;

    while (dx != 0 || dy != 0) {
        DrawTile(oldTileX, oldTileY);
        DrawTile(robotTileX, robotTileY);

        if (dx > 0) { robotPxX += 2; if (robotPxX > targetPxX) robotPxX = targetPxX; }
        if (dx < 0) { robotPxX -= 2; if (robotPxX < targetPxX) robotPxX = targetPxX; }
        if (dy > 0) { robotPxY += 2; if (robotPxY > targetPxY) robotPxY = targetPxY; }
        if (dy < 0) { robotPxY -= 2; if (robotPxY < targetPxY) robotPxY = targetPxY; }

        DrawRobot();
        Delay(1);

        dx = targetPxX - robotPxX;
        dy = targetPxY - robotPxY;
    }
}

static void TryMoveRobot(WORD dx, WORD dy)
{
    WORD nx, ny;
    WORD oldX = robotTileX;
    WORD oldY = robotTileY;

    if (gameState != GAME_PLAYING) return;

    nx = robotTileX + dx;
    ny = robotTileY + dy;

    if (IsBlocked(nx, ny)) return;

    robotTileX = nx;
    robotTileY = ny;
    moves++;

    if (battery > 0) battery--;

    AnimateMove(oldX, oldY, MAP_X + robotTileX * TILE_SIZE + 1, MAP_Y + robotTileY * TILE_SIZE + 1);

    if (map[robotTileY][robotTileX] == TILE_DIRT) {
        map[robotTileY][robotTileX] = TILE_FLOOR;
        if (dirtLeft > 0) dirtLeft--;
    }

    if (map[robotTileY][robotTileX] == TILE_DOCK) {
        battery = 100;
    }

    if (dirtLeft <= 0) gameState = GAME_WON;
    if (battery <= 0 && gameState != GAME_WON) gameState = GAME_BATTERY;

    DrawStatus();
    DrawTile(oldX, oldY);
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
        case RAW_RIGHT:
        case RAW_UP:
        case RAW_DOWN:
            if (gameState == GAME_TITLE) {
                ResetLevel();
                RedrawAll();
                break;
            }
            if (code == RAW_LEFT) TryMoveRobot(-1, 0);
            if (code == RAW_RIGHT) TryMoveRobot(1, 0);
            if (code == RAW_UP) TryMoveRobot(0, -1);
            if (code == RAW_DOWN) TryMoveRobot(0, 1);
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

    gameState = GAME_TITLE;

    if (!OpenGameWindow()) {
        return 20;
    }

    RedrawAll();
    MainLoop();

    CloseGameWindow();

    printf("RoboVac Rescue prototype ended.\n");
    return 0;
}
