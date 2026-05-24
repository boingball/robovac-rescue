/*
 * robovac.c - Competitive robot hoover game prototype for AmigaOS
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

#define WIN_W       768
#define WIN_H       600

#define TILE_SIZE   8
#define MAP_W       40
#define MAP_H       30

#define MAP_X       16
#define MAP_Y       34

#define ROBOT_W     6
#define ROBOT_H     6

#define MAX_ROBOTS  4
#define MAX_ROUNDS  5

#define TILE_FLOOR  0
#define TILE_WALL   1
#define TILE_DIRT   2

#define RAW_ESC     0x45
#define RAW_LEFT    0x4F
#define RAW_RIGHT   0x4E
#define RAW_UP      0x4C
#define RAW_DOWN    0x4D
#define RAW_R       0x13
#define RAW_1       0x01
#define RAW_2       0x02
#define RAW_3       0x03
#define RAW_4       0x04

#define GAME_TITLE      0
#define GAME_SELECT     1
#define GAME_PLAYING    2
#define GAME_ROUND_END  3
#define GAME_MATCH_END  4

static struct Window *win = NULL;
static struct RastPort *rp = NULL;
static UBYTE map[MAP_H][MAP_W];

static const char *roomNames[] = {"Living Room", "Kitchen", "Bedroom", "Dining Room", "Bathroom"};
static const UBYTE roomStyles[] = {0, 1, 2, 3, 4};

struct Robot {
    WORD x;
    WORD y;
    WORD battery;
    WORD score;
    BOOL ai;
    UBYTE color;
};

static struct Robot robots[MAX_ROBOTS];
static WORD robotCount = 2;
static WORD roundIndex = 0;
static WORD dirtLeft = 0;
static WORD targetDirt = 24;
static WORD winner = -1;
static WORD gameState = GAME_TITLE;
static UBYTE roomType = 0;
static BOOL running = TRUE;

static ULONG rng = 0x1234ABCD;
static UWORD RandRange(UWORD n)
{
    rng = rng * 1103515245UL + 12345UL;
    if (!n) return 0;
    return (UWORD)((rng >> 16) % n);
}

static BOOL IsBlocked(WORD tx, WORD ty)
{
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return TRUE;
    return map[ty][tx] == TILE_WALL;
}

static void ClearMap(void)
{
    WORD x, y;
    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            map[y][x] = TILE_FLOOR;
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) map[y][x] = TILE_WALL;
        }
    }
}

static void BuildRoomObstacles(void)
{
    WORD x, y;
    ClearMap();

    if (roomType == 0) {
        for (x = 8; x < 32; x++) map[12][x] = TILE_WALL;
        for (y = 14; y < 24; y++) map[y][20] = TILE_WALL;
    } else if (roomType == 1) {
        for (y = 4; y < 25; y += 4) for (x = 6; x < 35; x++) if ((x % 7) < 5) map[y][x] = TILE_WALL;
    } else if (roomType == 2) {
        for (y = 6; y < 24; y++) { map[y][10] = TILE_WALL; map[y][30] = TILE_WALL; }
    } else if (roomType == 3) {
        for (x = 6; x < 34; x++) { map[8][x] = TILE_WALL; map[21][x] = TILE_WALL; }
        for (y = 8; y < 22; y++) { map[y][6] = TILE_WALL; map[y][33] = TILE_WALL; }
    } else {
        for (y = 5; y < 25; y++) {
            map[y][13] = TILE_WALL;
            map[y][26] = TILE_WALL;
        }
        for (x = 13; x <= 26; x++) map[15][x] = TILE_WALL;
    }
}

static void SpawnDirt(void)
{
    WORD tries = 0;
    dirtLeft = 0;
    targetDirt = 24 + roundIndex * 8 + RandRange(8);
    while (dirtLeft < targetDirt && tries < 4000) {
        WORD x = 1 + RandRange(MAP_W - 2);
        WORD y = 1 + RandRange(MAP_H - 2);
        if (map[y][x] == TILE_FLOOR) {
            map[y][x] = TILE_DIRT;
            dirtLeft++;
        }
        tries++;
    }
}

static void ResetRobotsForRound(void)
{
    WORD i;
    static const WORD sx[MAX_ROBOTS] = {1, MAP_W - 2, 1, MAP_W - 2};
    static const WORD sy[MAX_ROBOTS] = {1, 1, MAP_H - 2, MAP_H - 2};
    for (i = 0; i < robotCount; i++) {
        robots[i].x = sx[i];
        robots[i].y = sy[i];
        robots[i].battery = 120;
    }
}

static void StartRound(void)
{
    roomType = roomStyles[RandRange(5)];
    BuildRoomObstacles();
    SpawnDirt();
    ResetRobotsForRound();
    gameState = GAME_PLAYING;
}

static void StartMatch(WORD rivals)
{
    WORD i;
    robotCount = 1 + rivals;
    roundIndex = 0;
    winner = -1;
    for (i = 0; i < robotCount; i++) {
        robots[i].score = 0;
        robots[i].ai = (i != 0);
        robots[i].color = 2 + i;
    }
    StartRound();
}

static void DrawTile(WORD tx, WORD ty)
{
    WORD sx = MAP_X + tx * TILE_SIZE;
    WORD sy = MAP_Y + ty * TILE_SIZE;
    UBYTE t = map[ty][tx];
    SetAPen(rp, 0);
    RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
    if (t == TILE_WALL) { SetAPen(rp, 1); RectFill(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1); }
    else { SetAPen(rp, 8); WritePixel(rp, sx + 3, sy + 3); }
    if (t == TILE_DIRT) { SetAPen(rp, 3); RectFill(rp, sx + 2, sy + 2, sx + 5, sy + 5); }
}

static void DrawMap(void)
{
    WORD x, y;
    for (y = 0; y < MAP_H; y++) for (x = 0; x < MAP_W; x++) DrawTile(x, y);
}

static void DrawRobots(void)
{
    WORD i;
    for (i = 0; i < robotCount; i++) {
        WORD sx = MAP_X + robots[i].x * TILE_SIZE + 1;
        WORD sy = MAP_Y + robots[i].y * TILE_SIZE + 1;
        SetAPen(rp, robots[i].color);
        RectFill(rp, sx, sy, sx + ROBOT_W, sy + ROBOT_H);
    }
}

static BOOL AnyBatteryLeft(void)
{
    WORD i;
    for (i = 0; i < robotCount; i++) if (robots[i].battery > 0) return TRUE;
    return FALSE;
}

static void TryMove(WORD id, WORD dx, WORD dy)
{
    WORD nx = robots[id].x + dx;
    WORD ny = robots[id].y + dy;
    if (robots[id].battery <= 0) return;
    if (IsBlocked(nx, ny)) return;
    robots[id].x = nx;
    robots[id].y = ny;
    robots[id].battery--;
    if (map[ny][nx] == TILE_DIRT) {
        map[ny][nx] = TILE_FLOOR;
        robots[id].score++;
        dirtLeft--;
    }
}

static void AiStep(WORD id)
{
    UWORD d = RandRange(4);
    if (d == 0) TryMove(id, -1, 0);
    if (d == 1) TryMove(id, 1, 0);
    if (d == 2) TryMove(id, 0, -1);
    if (d == 3) TryMove(id, 0, 1);
}

static void FinishRoundOrMatch(void)
{
    WORD i;
    if (dirtLeft > 0 && AnyBatteryLeft()) return;
    roundIndex++;
    if (roundIndex >= MAX_ROUNDS) {
        WORD best = -1, bestScore = -1;
        for (i = 0; i < robotCount; i++) if (robots[i].score > bestScore) { bestScore = robots[i].score; best = i; }
        winner = best;
        gameState = GAME_MATCH_END;
    } else {
        gameState = GAME_ROUND_END;
    }
}

static void DrawStatus(void)
{
    char b[256];
    SetAPen(rp, 0); RectFill(rp, 0, 0, WIN_W - 1, 30);
    SetAPen(rp, 7); Move(rp, 4, 10);
    if (gameState == GAME_TITLE) {
        Text(rp, (STRPTR)"ROBOVAC RESCUE", 14); return;
    }
    if (gameState == GAME_SELECT) {
        Text(rp, (STRPTR)"Choose AI rivals: 1,2,3 (Esc quits)", 36); return;
    }
    if (gameState == GAME_MATCH_END) {
        snprintf(b, sizeof(b), "Match over! Winner: R%d score:%d  R=restart Esc=quit", winner + 1, robots[winner].score);
        Text(rp, (STRPTR)b, strlen(b)); return;
    }
    if (gameState == GAME_ROUND_END) {
        snprintf(b, sizeof(b), "Round %d done in %s. Press R for next round", roundIndex, roomNames[roomType]);
        Text(rp, (STRPTR)b, strlen(b)); return;
    }
    snprintf(b, sizeof(b), "Round:%d/5 Room:%s Dirt:%d", roundIndex + 1, roomNames[roomType], dirtLeft);
    Text(rp, (STRPTR)b, strlen(b));
    Move(rp, 4, 22);
    snprintf(b, sizeof(b), "You:%d B:%d  A1:%d B:%d  A2:%d B:%d  A3:%d B:%d", robots[0].score, robots[0].battery,
        (robotCount>1)?robots[1].score:0, (robotCount>1)?robots[1].battery:0,
        (robotCount>2)?robots[2].score:0, (robotCount>2)?robots[2].battery:0,
        (robotCount>3)?robots[3].score:0, (robotCount>3)?robots[3].battery:0);
    Text(rp, (STRPTR)b, strlen(b));
}

static void RedrawAll(void)
{
    SetAPen(rp, 0); RectFill(rp, 0, 0, WIN_W - 1, WIN_H - 1);
    DrawStatus();
    if (gameState == GAME_TITLE) {
        SetAPen(rp, 6); Move(rp, 36, 100); Text(rp, (STRPTR)"Press R to start competitive cleanup", 35); return;
    }
    if (gameState == GAME_SELECT) return;
    DrawMap(); DrawRobots();
}

static void TickAI(void)
{
    WORD i;
    if (gameState != GAME_PLAYING) return;
    for (i = 1; i < robotCount; i++) AiStep(i);
    FinishRoundOrMatch();
}

static BOOL OpenGameWindow(void)
{
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"RoboVac Rescue", WA_Width, WIN_W, WA_Height, WIN_H,
        WA_DepthGadget, TRUE, WA_DragBar, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_REFRESHWINDOW, TAG_END);
    if (!win) return FALSE;
    rp = win->RPort;
    return TRUE;
}

static void HandleRawKey(UWORD code)
{
    if (code & 0x80) return;
    if (code == RAW_ESC) { running = FALSE; return; }

    if (gameState == GAME_TITLE && code == RAW_R) { gameState = GAME_SELECT; RedrawAll(); return; }
    if (gameState == GAME_SELECT) {
        if (code == RAW_1) StartMatch(1);
        if (code == RAW_2) StartMatch(2);
        if (code == RAW_3) StartMatch(3);
        RedrawAll();
        return;
    }
    if (gameState == GAME_ROUND_END && code == RAW_R) { StartRound(); RedrawAll(); return; }
    if (gameState == GAME_MATCH_END && code == RAW_R) { gameState = GAME_SELECT; RedrawAll(); return; }

    if (gameState == GAME_PLAYING) {
        if (code == RAW_LEFT) TryMove(0, -1, 0);
        if (code == RAW_RIGHT) TryMove(0, 1, 0);
        if (code == RAW_UP) TryMove(0, 0, -1);
        if (code == RAW_DOWN) TryMove(0, 0, 1);
        FinishRoundOrMatch();
        RedrawAll();
    }
}

static void MainLoop(void)
{
    ULONG winSig = 1L << win->UserPort->mp_SigBit;
    while (running) {
        ULONG sigs = Wait(winSig | SIGBREAKF_CTRL_C | (1L<<13));
        if (sigs & SIGBREAKF_CTRL_C) running = FALSE;
        if (sigs & (1L<<13)) { TickAI(); RedrawAll(); }
        if (sigs & winSig) {
            struct IntuiMessage *msg;
            while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
                ULONG cls = msg->Class; UWORD code = msg->Code;
                ReplyMsg((struct Message *)msg);
                if (cls == IDCMP_CLOSEWINDOW) running = FALSE;
                else if (cls == IDCMP_RAWKEY) HandleRawKey(code);
                else if (cls == IDCMP_REFRESHWINDOW) { BeginRefresh(win); RedrawAll(); EndRefresh(win, TRUE); }
            }
        }
        Delay(1);
        TickAI();
    }
}

int main(void)
{
    if (!OpenGameWindow()) return 20;
    gameState = GAME_TITLE;
    RedrawAll();
    MainLoop();
    CloseWindow(win);
    return 0;
}
