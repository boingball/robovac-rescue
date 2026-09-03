#include "robovac.h"

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
    WORD divisor = titleSpinAdvanceDivisor;

    /* Once J1 is active the carousel is being used as a selector. Keep its
     * phase moving at the original one-step-per-frame rate so input never
     * makes the selected hoover appear to crawl. */
    if (joyEnabled[0]) divisor = 1;

    titleSpinAdvanceCounter++;
    if (titleSpinAdvanceCounter >= divisor) {
        titleSpinAdvanceCounter = 0;
        titleSpinPhase = (titleSpinPhase + 1) & (TITLE_SPIN_STEPS - 1);
    }
}


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


static void MarkBossHpTextDirty(void)
{
    dirtyBossHpText = TRUE;
}


static void MarkHudStatusTextDirty(void)
{
    dirtyHudStatusText = TRUE;
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

    quadActive = (bigHeadMode || (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0)) ? 1 : 0;
    if (robots[id].spriteIndex != dirtyPrevRobotSpriteIndex[id]) return TRUE;
    if (robots[id].prevSpriteIndex != dirtyPrevRobotPrevSpriteIndex[id]) return TRUE;
    if (robots[id].powerType != dirtyPrevRobotPowerType[id]) return TRUE;
    if (robots[id].powerMovesLeft != dirtyPrevRobotPowerMovesLeft[id]) return TRUE;
    if (robots[id].stunTicks != dirtyPrevRobotStunTicks[id]) return TRUE;
    if (robots[id].battery != dirtyPrevRobotBattery[id]) return TRUE;
    if (robots[id].turnTicks != dirtyPrevRobotTurnTicks[id]) return TRUE;
    if (robots[id].turnDirection != dirtyPrevRobotTurnDirection[id]) return TRUE;
    if (speedFlashTicks[id] != dirtyPrevRobotSpeedFlashTicks[id]) return TRUE;
    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_FLOODHOUSE &&
        floodCarried[id] != dirtyPrevRobotFloodCarried[id]) return TRUE;
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
    dirtyPrevRobotQuadActive[id] = (bigHeadMode || (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0)) ? 1 : 0;
    dirtyPrevRobotTileUnder[id] = DirtyRobotTileUnder(id);
    dirtyPrevRobotSpeedFlashTicks[id] = speedFlashTicks[id];
    dirtyPrevRobotFloodCarried[id] = floodCarried[id];
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

    if (bigHeadMode) return TRUE;
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
    /* Include the cached speed trail around both the previous and current
     * robot positions. Keeping this slightly generous makes the dirty-rect
     * path erase an old trail without needing a full-frame redraw. */
    const WORD margin = 8;

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

static void AddDirtyPuckAt(LONG px, LONG py)
{
    AddDirtyRect(MAP_X + FP_TO_INT(px) - 2, MAP_Y + FP_TO_INT(py) - 2,
                 PUCK_W + 4, PUCK_H + 4);
}

static void AddDirtyAirHockeyPuckAt(LONG px, LONG py)
{
    AddDirtyRect(MAP_X + FP_TO_INT(px) - 2, MAP_Y + FP_TO_INT(py) - 2,
                 AIRHOCKEY_W + 4, AIRHOCKEY_H + 4);
}

static void AddDirtyDirtStormAt(LONG px, WORD tileY)
{
    AddDirtyRect(MAP_X + FP_TO_INT(px) - 2, MAP_Y + (tileY * TILE_SIZE) - 2,
                 ROBOT_W + 4, ROBOT_H + 4);
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

    if (gameState == GAME_MINIGAME_PLAYING) {
        if (miniGameType == MINIGAME_RACE &&
            dirtyPrevRaceSecond != (raceTicksRemaining / 50)) changed = TRUE;
        if (miniGameType == MINIGAME_PUCK &&
            (dirtyPrevPuckSecond != (puckTicksRemaining / 50) ||
             dirtyPrevPuckScore[0] != puckTeamScore[0] ||
             dirtyPrevPuckScore[1] != puckTeamScore[1] ||
             dirtyPrevPuckScoringTeam != puckScoringTeam)) changed = TRUE;
        if (miniGameType == MINIGAME_BUMPER &&
            (dirtyPrevBumperSecond != (bumperTicksRemaining / 50) ||
             dirtyPrevBumperAlive != bumperAliveCount ||
             dirtyPrevBumperFlashTicks != bumperEliminatedFlashTicks)) changed = TRUE;
        if (miniGameType == MINIGAME_AIRHOCKEY &&
            (dirtyPrevAirhockeySecond != (airhockeyTicksRemaining / 50) ||
             dirtyPrevAirhockeyScore[0] != airhockeyTeamScore[0] ||
             dirtyPrevAirhockeyScore[1] != airhockeyTeamScore[1] ||
             dirtyPrevAirhockeyScoringTeam != airhockeyScoringTeam)) changed = TRUE;
        if (miniGameType == MINIGAME_BOWLING &&
            (dirtyPrevBowlingSecond != (bowlingTicksRemaining / 50) ||
             dirtyPrevBowlingPinsRemaining != bowlingPinsRemaining ||
             dirtyPrevBowlingScore[0] != bowlingTeamScore[0] ||
             dirtyPrevBowlingScore[1] != bowlingTeamScore[1] ||
             dirtyPrevBowlingFlashTicks != bowlingFlashTicks)) changed = TRUE;
        if (miniGameType == MINIGAME_FLOODHOUSE &&
            (dirtyPrevFloodSecond != (floodTicksRemaining / 50) ||
             dirtyPrevFloodLooseRemaining != floodLooseRemaining ||
             dirtyPrevFloodFlashTicks != floodFlashTicks)) changed = TRUE;
    }

    for (i = 0; i < robotCount; i++) {
        if (dirtyPrevBattery[i] != robots[i].battery || dirtyPrevScore[i] != robots[i].score ||
            dirtyPrevStunTicks[i] != robots[i].stunTicks || dirtyPrevPowerMoves[i] != robots[i].powerMovesLeft ||
            dirtyPrevPowerType[i] != robots[i].powerType) {
            changed = TRUE;
            break;
        }
        if (gameState == GAME_MINIGAME_PLAYING &&
            (dirtyPrevRaceLap[i] != raceLap[i] || dirtyPrevRacePlace[i] != racePlace[i])) {
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
    dirtyPrevRaceSecond = raceTicksRemaining / 50;
    dirtyPrevPuckSecond = puckTicksRemaining / 50;
    dirtyPrevPuckScore[0] = puckTeamScore[0];
    dirtyPrevPuckScore[1] = puckTeamScore[1];
    dirtyPrevPuckScoringTeam = puckScoringTeam;
    dirtyPrevBumperSecond = bumperTicksRemaining / 50;
    dirtyPrevBumperAlive = bumperAliveCount;
    dirtyPrevBumperFlashTicks = bumperEliminatedFlashTicks;
    dirtyPrevAirhockeySecond = airhockeyTicksRemaining / 50;
    dirtyPrevAirhockeyScore[0] = airhockeyTeamScore[0];
    dirtyPrevAirhockeyScore[1] = airhockeyTeamScore[1];
    dirtyPrevAirhockeyScoringTeam = airhockeyScoringTeam;
    dirtyPrevBowlingSecond = bowlingTicksRemaining / 50;
    dirtyPrevBowlingPinsRemaining = bowlingPinsRemaining;
    dirtyPrevBowlingScore[0] = bowlingTeamScore[0];
    dirtyPrevBowlingScore[1] = bowlingTeamScore[1];
    dirtyPrevBowlingFlashTicks = bowlingFlashTicks;
    dirtyPrevFloodSecond = floodTicksRemaining / 50;
    dirtyPrevFloodLooseRemaining = floodLooseRemaining;
    dirtyPrevFloodFlashTicks = floodFlashTicks;
    for (i = 0; i < robotCount; i++) {
        dirtyPrevBattery[i] = robots[i].battery;
        dirtyPrevScore[i] = robots[i].score;
        dirtyPrevStunTicks[i] = robots[i].stunTicks;
        dirtyPrevPowerMoves[i] = robots[i].powerMovesLeft;
        dirtyPrevPowerType[i] = robots[i].powerType;
        dirtyPrevRaceLap[i] = raceLap[i];
        dirtyPrevRacePlace[i] = racePlace[i];
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

    if (!IsArenaPlaying()) {
        dirtyRectsBuiltForFrame = FALSE;
        return;
    }

    ClearDirtyRects();

    if (roundCountdownTicks > 0 || pauseMenuOpen) {
        ForceGameplayFullPresent();
    }

    MarkDirtyRoundGoOverlay();
    MarkDirtyEmpOverlay();

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_PUCK &&
        dirtyPrevPuckValid) {
        AddDirtyPuckAt(dirtyPrevPuckPx, dirtyPrevPuckPy);
    }

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_AIRHOCKEY &&
        dirtyPrevAirhockeyPuckValid) {
        AddDirtyAirHockeyPuckAt(dirtyPrevAirhockeyPuckPx, dirtyPrevAirhockeyPuckPy);
    }

    if (dirtyPrevDirtStormValid) {
        AddDirtyDirtStormAt(dirtyPrevDirtStormPx, dirtyPrevDirtStormTileY);
    }

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

    if (!IsArenaPlaying()) {
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

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_PUCK) {
        AddDirtyPuckAt(puckPx, puckPy);
        dirtyPrevPuckPx = puckPx;
        dirtyPrevPuckPy = puckPy;
        dirtyPrevPuckValid = TRUE;
    } else {
        dirtyPrevPuckValid = FALSE;
    }

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_AIRHOCKEY) {
        AddDirtyAirHockeyPuckAt(airhockeyPuckPx, airhockeyPuckPy);
        dirtyPrevAirhockeyPuckPx = airhockeyPuckPx;
        dirtyPrevAirhockeyPuckPy = airhockeyPuckPy;
        dirtyPrevAirhockeyPuckValid = TRUE;
    } else {
        dirtyPrevAirhockeyPuckValid = FALSE;
    }

    if (dirtStormActive) {
        AddDirtyDirtStormAt(dirtStormPx, dirtStormTileY);
        dirtyPrevDirtStormPx = dirtStormPx;
        dirtyPrevDirtStormTileY = dirtStormTileY;
        dirtyPrevDirtStormValid = TRUE;
    } else {
        dirtyPrevDirtStormValid = FALSE;
    }

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


/* The floor art is a single repeating texture, which looks visibly tiled
 * across a whole room. Pre-rotate it into the same four orientations used
 * for walls above, then BlitTileTo picks a rotation per tile from its map
 * position so the floor reads as a varied pattern instead of one texture
 * stamped identically everywhere. */
static void BuildFloorRotationsInCache(void)
{
    WORD srcBaseX = TILE_FLOOR * TILE_SIZE;
    UBYTE rot;

    for (rot = 0; rot < FLOOR_ROTATION_COUNT; rot++) {
        WORD dstBaseX = (TILE_COUNT + WALL_ROTATION_COUNT + rot) * TILE_SIZE;
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
    BuildFloorRotationsInCache();

    return TRUE;
}


static void BlitTileTo(struct RastPort *rp, UBYTE tileType, WORD tx, WORD ty)
{
    WORD srcX;
    WORD dstX;
    WORD dstY;

    if (!tileCacheBM) return;
    if (tileType >= TILE_COUNT) tileType = TILE_FLOOR;

    if (tileType == TILE_FLOOR) {
        /* Deterministic per-tile pick (not per-frame random) so a cleaned
         * tile's texture doesn't flicker between orientations on redraw -
         * a simple checkerboard-ish mix of the coordinates spreads the four
         * rotations out so no obvious repeating grid remains. */
        UBYTE rot = (UBYTE)(((tx * 3) + (ty * 7)) & (FLOOR_ROTATION_COUNT - 1));
        srcX = (TILE_COUNT + WALL_ROTATION_COUNT + rot) * TILE_SIZE;
    } else {
        srcX = tileType * TILE_SIZE;
    }
    dstX = MAP_X + tx * TILE_SIZE;
    dstY = MAP_Y + ty * TILE_SIZE;

    BltBitMapRastPort(tileCacheBM, srcX, 0,
                      rp, dstX, dstY,
                      TILE_SIZE, TILE_SIZE,
                      0xC0);
}


/* Flood House block state is its own height grid, not a map[][] tile type
 * (a floor tile needs to hold 0-3, which a tile constant cannot), so it is
 * baked into the room buffer here as an overlay on top of the plain floor
 * tile UpdateRoomTile already drew, rather than through BlitTileTo. A cheap
 * "fake stack" - each level a shallow bar stepped upward from the tile's
 * bottom edge, with a lighter top-face strip - plus the current height as
 * a digit, always anchored to the tile's top edge so it never depends on
 * how tall the stack under it happens to be. */
static void DrawFloodBlockTile(struct RastPort *rp, WORD tx, WORD ty)
{
    WORD dstX = MAP_X + tx * TILE_SIZE;
    WORD dstY = MAP_Y + ty * TILE_SIZE;
    UBYTE height = floodBlockHeight[ty][tx];
    WORD blockH = 4;
    WORD level;
    char num[2];

    if (height <= 0) return;
    if (height > FLOODHOUSE_STACK_MAX) height = FLOODHOUSE_STACK_MAX;

    for (level = 0; level < height; level++) {
        WORD topY = dstY + TILE_SIZE - blockH - (level * blockH);
        SetAPen(rp, 5);
        RectFill(rp, dstX + 2, topY, dstX + TILE_SIZE - 3, topY + blockH - 1);
        SetAPen(rp, 13);
        RectFill(rp, dstX + 2, topY, dstX + TILE_SIZE - 3, topY);
    }

    num[0] = (char)('0' + height);
    num[1] = '\0';
    MiniText(rp, dstX + 6, dstY + 1, num, 14);
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
        /* Bumper Bots reuses TILE_WALL for the abyss ring purely for its
         * blocking behaviour; visually it should read as empty space, not
         * a room wall, so paint it black instead of the wall art. */
        if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER) {
            WORD dstX = MAP_X + tx * TILE_SIZE;
            WORD dstY = MAP_Y + ty * TILE_SIZE;
            SetAPen(&roomRP, 0);
            RectFill(&roomRP, dstX, dstY, dstX + TILE_SIZE - 1, dstY + TILE_SIZE - 1);
        } else {
            BlitWallRotatedTo(&roomRP, tx, ty);
        }
    } else {
        BlitTileTo(&roomRP, map[ty][tx], tx, ty);
    }
    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_FLOODHOUSE) {
        DrawFloodBlockTile(&roomRP, tx, ty);
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

    for (i = 0; i < 32; i++) {
        UWORD c = palette[i];
        UWORD r = (c >> 8) & 0xF;
        UWORD g = (c >> 4) & 0xF;
        UWORD b = c & 0xF;

        /* EMP kills the room lighting as well as the robots' controls. Keep
         * the robot palette intact so the players remain readable, but push
         * the room colours down to a convincing emergency-light level. */
        if (i < 16) {
            r /= EMP_ROOM_DIM_DIVISOR;
            g /= EMP_ROOM_DIM_DIVISOR;
            b /= EMP_ROOM_DIM_DIVISOR;
        }
        cycled[i] = (UWORD)((r << 8) | (g << 4) | b);
    }

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


/* Rotates the source bolt glyph by k*45 degrees (nearest-neighbour, same
 * sampling style as DrawRobotBobRotated below) so all eight compass
 * headings - including the four diagonals - get a properly rotated frame
 * instead of reusing an axis-aligned sprite for a diagonal shot. */
static void BuildBoltCacheFrame(struct RastPort *maskRP, WORD frame, WORD k)
{
    static const WORD cosTable[8] = {256, 181, 0, -181, -256, -181, 0, 181};
    static const WORD sinTable[8] = {0, 181, 256, 181, 0, -181, -256, -181};
    WORD x;
    WORD y;
    WORD srcBaseX = SPR_ENERGY_BOLT * ROBOT_W;
    WORD dstBaseX = frame * ROBOT_W;
    WORD cosv = cosTable[k & 7];
    WORD sinv = sinTable[k & 7];
    WORD half = ROBOT_W / 2;

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            WORD dx = x - half;
            WORD dy = y - half;
            WORD sampleX = (WORD)((((LONG)dx * cosv + (LONG)dy * sinv) >> 8)) + half;
            WORD sampleY = (WORD)((((LONG)dy * cosv - (LONG)dx * sinv) >> 8)) + half;

            if (sampleX < 0 || sampleY < 0 || sampleX >= ROBOT_W || sampleY >= ROBOT_H) continue;
            CacheBoltPixel(maskRP, srcBaseX, sampleX, sampleY, dstBaseX, x, y);
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

    {
        WORD frame;
        for (frame = 0; frame < BOLT_FRAME_COUNT; frame++) {
            BuildBoltCacheFrame(&maskRP, frame, frame);
        }
    }

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

    if (!BuildSpeedTrailCache()) {
        printf("Could not allocate speed motion-trail cache; speed power will have no trail\n");
    }

    if (!BuildPuckCache()) {
        printf("Could not allocate RoboPuck BOB cache; simple puck fallback enabled\n");
    }

    if (!BuildTitleCarouselRotationCache()) {
        FreeBoltCache();
        FreeRobotScaledCache();
        FreeSpeedTrailCache();
        FreePuckCache();
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


/* Shared cosmetic sprite-rotation sampler: nearest-neighbour inverse
 * rotation by angleStep * (360/BOBROT_STEPS) degrees, Q8 fixed point
 * (256 = 1.0) - same sampling formula as the bolt cache's BuildBoltCacheFrame
 * above, just driven by a live angle instead of one baked at cache-build
 * time. Used for any cosmetic spin finer than a fixed 45-degree blend:
 * RoboRace's cornering blend and Bumper Bots' elimination tumble. angleStep
 * wraps at BOBROT_STEPS and may be negative (mirrors the rotation). */
#define BOBROT_STEPS 32
static const WORD bobRotCosQ8[BOBROT_STEPS] = {
    256,251,237,213,181,142,98,50,
    0,-50,-98,-142,-181,-213,-237,-251,
    -256,-251,-237,-213,-181,-142,-98,-50,
    0,50,98,142,181,213,237,251
};
#define BobRotCos(a) (bobRotCosQ8[(a) & (BOBROT_STEPS - 1)])
#define BobRotSin(a) (bobRotCosQ8[((a) - (BOBROT_STEPS / 4)) & (BOBROT_STEPS - 1)])

static void DrawRobotBobRotated(WORD srcX, WORD sx, WORD sy, WORD angleStep)
{
    WORD cosv = BobRotCos(angleStep);
    WORD sinv = BobRotSin(angleStep);
    WORD x;
    WORD y;

    for (y = 0; y < ROBOT_H; y++) {
        for (x = 0; x < ROBOT_W; x++) {
            WORD dx = x - (ROBOT_W / 2);
            WORD dy = y - (ROBOT_H / 2);
            WORD sampleX = (WORD)((((LONG)dx * cosv) + ((LONG)dy * sinv)) >> 8) + (ROBOT_W / 2);
            WORD sampleY = (WORD)((((LONG)dy * cosv) - ((LONG)dx * sinv)) >> 8) + (ROBOT_H / 2);
            LONG p;

            if (sampleX < 0 || sampleY < 0 || sampleX >= ROBOT_W || sampleY >= ROBOT_H) continue;
            p = ReadPixel(&robotRP, srcX + sampleX, sampleY);
            if (p <= 0) continue;
            PlotRobotPixel(sx + x, sy + y, (UBYTE)p);
        }
    }
}


static void BuildSpeedTrailBar(struct RastPort *maskRP, WORD x, WORD y,
                               WORD w, WORD h, UBYTE pen)
{
    SetAPen(&speedTrailRP, pen);
    RectFill(&speedTrailRP, x, y, x + w - 1, y + h - 1);
    SetAPen(maskRP, 1);
    RectFill(maskRP, x, y, x + w - 1, y + h - 1);
}


static BOOL BuildSpeedTrailCache(void)
{
    struct RastPort maskRP;
    WORD frame;

    speedTrailBM = AllocBitMap(SPEED_TRAIL_W * SPEED_TRAIL_FRAME_COUNT,
                               SPEED_TRAIL_H, DEPTH,
                               BMF_CLEAR | BMF_DISPLAYABLE,
                               scr->RastPort.BitMap);
    speedTrailMaskBM = AllocBitMap(SPEED_TRAIL_W * SPEED_TRAIL_FRAME_COUNT,
                                   SPEED_TRAIL_H, 1,
                                   BMF_CLEAR | BMF_DISPLAYABLE,
                                   scr->RastPort.BitMap);
    if (!speedTrailBM || !speedTrailMaskBM) {
        FreeSpeedTrailCache();
        return FALSE;
    }

    InitRastPort(&speedTrailRP);
    speedTrailRP.BitMap = speedTrailBM;
    InitRastPort(&maskRP);
    maskRP.BitMap = speedTrailMaskBM;

    /* Each frame is a short, stepped streak. It is deliberately built once
     * in chip RAM and stamped with BltMaskBitMapRastPort during gameplay.
     * The robot is drawn over the near end of the streak. */
    for (frame = 0; frame < SPEED_TRAIL_FRAME_COUNT; frame++) {
        WORD x = frame * SPEED_TRAIL_W;

        switch (frame) {
            case 0: /* left-facing: trail extends to the right */
                BuildSpeedTrailBar(&maskRP, x + 16, 7, 8, 3, 13);
                BuildSpeedTrailBar(&maskRP, x + 12, 6, 4, 5, 13);
                BuildSpeedTrailBar(&maskRP, x + 8, 5, 4, 7, 14);
                break;
            case 1: /* right-facing: trail extends to the left */
                BuildSpeedTrailBar(&maskRP, x + 0, 7, 8, 3, 13);
                BuildSpeedTrailBar(&maskRP, x + 8, 6, 4, 5, 13);
                BuildSpeedTrailBar(&maskRP, x + 12, 5, 4, 7, 14);
                break;
            case 2: /* up-facing: trail extends below */
                BuildSpeedTrailBar(&maskRP, x + 7, 16, 3, 8, 13);
                BuildSpeedTrailBar(&maskRP, x + 6, 12, 5, 4, 13);
                BuildSpeedTrailBar(&maskRP, x + 5, 8, 7, 4, 14);
                break;
            default: /* down-facing: trail extends above */
                BuildSpeedTrailBar(&maskRP, x + 7, 0, 3, 8, 13);
                BuildSpeedTrailBar(&maskRP, x + 6, 8, 5, 4, 13);
                BuildSpeedTrailBar(&maskRP, x + 5, 12, 7, 4, 14);
                break;
        }
    }

    return TRUE;
}


static void FreeSpeedTrailCache(void)
{
    if (speedTrailBM) {
        FreeBitMap(speedTrailBM);
        speedTrailBM = NULL;
    }
    if (speedTrailMaskBM) {
        FreeBitMap(speedTrailMaskBM);
        speedTrailMaskBM = NULL;
    }
    speedTrailRP.BitMap = NULL;
}


static BOOL BuildPuckCache(void)
{
    struct RastPort maskRP;
    static const UBYTE rowLeft[PUCK_H] = {4,2,1,0,0,0,0,0,0,1,2,4};
    static const UBYTE rowRight[PUCK_H] = {7,9,10,11,11,11,11,11,11,10,9,7};
    WORD y;

    puckBM = AllocBitMap(PUCK_W, PUCK_H, DEPTH,
                         BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    puckMaskBM = AllocBitMap(PUCK_W, PUCK_H, 1,
                             BMF_CLEAR | BMF_DISPLAYABLE, scr->RastPort.BitMap);
    if (!puckBM || !puckMaskBM) {
        FreePuckCache();
        return FALSE;
    }

    InitRastPort(&puckRP);
    puckRP.BitMap = puckBM;
    InitRastPort(&maskRP);
    maskRP.BitMap = puckMaskBM;

    for (y = 0; y < PUCK_H; y++) {
        SetAPen(&puckRP, (y < 3) ? 14 : 13);
        RectFill(&puckRP, rowLeft[y], y, rowRight[y], y);
        SetAPen(&maskRP, 1);
        RectFill(&maskRP, rowLeft[y], y, rowRight[y], y);
    }
    SetAPen(&puckRP, 10);
    RectFill(&puckRP, 3, 2, 5, 3);
    SetAPen(&puckRP, 12);
    RectFill(&puckRP, 4, 8, 8, 9);
    return TRUE;
}


static void FreePuckCache(void)
{
    if (puckBM) {
        FreeBitMap(puckBM);
        puckBM = NULL;
    }
    if (puckMaskBM) {
        FreeBitMap(puckMaskBM);
        puckMaskBM = NULL;
    }
    puckRP.BitMap = NULL;
}


static void DrawSpeedMotionBlur(WORD id, WORD sx, WORD sy)
{
    WORD frame;

    if (id < 0 || id >= robotCount || speedFlashTicks[id] <= 0) return;
    if (!speedTrailBM || !speedTrailMaskBM || !speedTrailMaskBM->Planes[0]) return;

    switch (robots[id].spriteIndex) {
        case SPR_LEFT:  frame = 0; break;
        case SPR_RIGHT: frame = 1; break;
        case SPR_UP:    frame = 2; break;
        case SPR_DOWN:  frame = 3; break;
        default: return;
    }

    /* The masked blit is the per-frame effect. No CPU pixel/rectangle work
     * and no forced full-screen presentation are needed. */
    BltMaskBitMapRastPort(speedTrailBM, frame * SPEED_TRAIL_W, 0,
                          &renderRP, sx - 4, sy - 4,
                          SPEED_TRAIL_W, SPEED_TRAIL_H,
                          (ABC | ABNC | ANBC),
                          speedTrailMaskBM->Planes[0]);
}


/* A short cartoon tumble for a robot just knocked off the rug: it spins
 * through the four facing frames, sinks lower each tick, and blinks, then
 * vanishes for good. Reuses the existing cached sprite frames only - no
 * new art, no scaling. */
static void DrawBumperFallingRobot(WORD id)
{
    WORD sx;
    WORD sy;
    WORD srcX;
    WORD sink;
    WORD elapsed;

    if (!robotCacheBM || !robotMaskBM || !robotMaskBM->Planes[0]) return;
    if ((bumperFallTicks[id] & 2) == 0) return;

    elapsed = BUMPER_FALL_TICKS - bumperFallTicks[id];
    sink = elapsed * 2;
    sx = MAP_X + FP_TO_INT(robots[id].px);
    sy = MAP_Y + FP_TO_INT(robots[id].py) + sink;
    srcX = (robots[id].spriteVariant * SPR_STATE_COUNT + SPR_UP) * ROBOT_W;

    /* A continuous spin instead of jump-cutting between the four cardinal
     * sprites reads as an actual tumble as the robot sinks off the rug. */
    DrawRobotBobRotated(srcX, sx, sy, elapsed * 2);
}


static void DrawRobotBob(WORD id)
{
    WORD sx;
    WORD sy;
    WORD srcX;
    WORD raceTurning;

    if (id < 0 || id >= robotCount) return;
    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER && bumperEliminated[id]) {
        if (bumperFallTicks[id] > 0) DrawBumperFallingRobot(id);
        return;
    }

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER && bumperSliding[id]) {
        sx = MAP_X + FP_TO_INT(bumperVisualPx[id]);
        sy = MAP_Y + FP_TO_INT(bumperVisualPy[id]);
    } else {
        sx = MAP_X + FP_TO_INT(robots[id].px);
        sy = MAP_Y + FP_TO_INT(robots[id].py);
    }
    srcX = (robots[id].spriteVariant * SPR_STATE_COUNT + robots[id].spriteIndex) * ROBOT_W;
    raceTurning = (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_RACE &&
                   robots[id].turnTicks > 0 &&
                   robots[id].prevSpriteIndex != robots[id].spriteIndex) ? TRUE : FALSE;

    if (raceTurning) {
        WORD drift = (robots[id].turnTicks * RACE_DRIFT_PIXELS + RACE_TURN_TICKS - 1) / RACE_TURN_TICKS;
        switch (robots[id].prevSpriteIndex) {
            case SPR_LEFT:  sx -= drift; break;
            case SPR_RIGHT: sx += drift; break;
            case SPR_UP:    sy -= drift; break;
            case SPR_DOWN:  sy += drift; break;
        }
    }

    DrawSpeedMotionBlur(id, sx, sy);

    /* Big Head and the Quad power-up both render the BOB at a clean 2x
     * scale via DrawRobotBobScaled2. The normal ground shadow, sized for
     * the unscaled sprite, looked like an accidental line glued to the
     * bottom of the enlarged sprite in either mode, so omit it whenever
     * the robot is drawn scaled up. */
    if (!bigHeadMode && !(robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0)) {
        if (gameState == GAME_MINIGAME_PLAYING &&
            (miniGameType == MINIGAME_PUCK || miniGameType == MINIGAME_AIRHOCKEY || miniGameType == MINIGAME_BOWLING)) {
            /* The seven hoover variants share one palette with no pen free
             * to safely recolour the cached sprite art itself (see the Dirt
             * Storm note below), so a team "skin" is a solid colour tile
             * behind the BOB instead - same pens DrawPuckHud already uses
             * for TEAM 1/TEAM 2, showing through the sprite's transparent
             * mask as a coloured aura around each hoover. */
            WORD team = (miniGameType == MINIGAME_PUCK) ? PuckTeamForRobot(id) :
                        (miniGameType == MINIGAME_AIRHOCKEY) ? AirHockeyTeamForRobot(id) :
                        BowlingTeamForRobot(id);
            SetAPen(&renderRP, (team == 0) ? 13 : 14);
            RectFill(&renderRP, sx, sy, sx + ROBOT_W - 1, sy + ROBOT_H - 1);
        } else {
            SetAPen(&renderRP, 6);
            RectFill(&renderRP, sx + 4, sy + 13, sx + 12, sy + 14);
        }
    }

    if (robotCacheBM && robotMaskBM && robotMaskBM->Planes[0]) {
        if (bigHeadMode || (robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0)) {
            DrawRobotBobScaled2(srcX, sx, sy);
        } else if (raceTurning && robots[id].turnTicks > 3) {
            WORD turnSrcX = (robots[id].spriteVariant * SPR_STATE_COUNT + robots[id].prevSpriteIndex) * ROBOT_W;
            BltMaskBitMapRastPort(robotCacheBM, turnSrcX, 0,
                                  &renderRP, sx, sy,
                                  ROBOT_W, ROBOT_H,
                                  (ABC | ABNC | ANBC),
                                  robotMaskBM->Planes[0]);
        } else if (robots[id].turnTicks > 1 && robots[id].prevSpriteIndex != robots[id].spriteIndex) {
            WORD turnSrcX = (robots[id].spriteVariant * SPR_STATE_COUNT + robots[id].prevSpriteIndex) * ROBOT_W;
            /* Sweep continuously through the corner instead of holding one
             * fixed 45-degree blend for both ticks of the diagonal phase. */
            WORD angleStep = ((RACE_TURN_TICKS - robots[id].turnTicks) * (BOBROT_STEPS / 4)) / RACE_TURN_TICKS;
            if (robots[id].turnDirection < 0) angleStep = -angleStep;
            DrawRobotBobRotated(turnSrcX, sx, sy, angleStep);
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

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_FLOODHOUSE && floodCarried[id] > 0) {
        /* Kept strictly inside the sprite's own ROBOT_W x ROBOT_H footprint
         * (unlike the stun marks above, which sit comfortably inside it
         * too) so the existing per-robot dirty rect already covers erasing
         * it - drawing even one pixel outside that box would leave a stray
         * mark behind whenever the robot moves on. */
        char carryNum[2];
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, sx + 10, sy, sx + 15, sy + 5);
        carryNum[0] = (char)('0' + floodCarried[id]);
        carryNum[1] = '\0';
        MiniText(&renderRP, sx + 11, sy, carryNum, 14);
    }
}


static WORD BoltFrameForDirection(struct Bolt *bolt)
{
    /* [sy+1][sx+1], sy/sx each -1/0/1. A stationary (0,0) direction falls
     * back to RIGHT, matching the old axis-only behaviour. */
    static const UBYTE table[3][3] = {
        { BOLT_FRAME_UP_LEFT,   BOLT_FRAME_UP,   BOLT_FRAME_UP_RIGHT   },
        { BOLT_FRAME_LEFT,      BOLT_FRAME_RIGHT,BOLT_FRAME_RIGHT      },
        { BOLT_FRAME_DOWN_LEFT, BOLT_FRAME_DOWN, BOLT_FRAME_DOWN_RIGHT }
    };
    WORD sx = (bolt->dirX > 0) - (bolt->dirX < 0);
    WORD sy = (bolt->dirY > 0) - (bolt->dirY < 0);

    return table[sy + 1][sx + 1];
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


static void DrawDirtStorm(void)
{
    WORD sx;
    WORD sy;
    WORD srcX;
    WORD spinStep;

    if (!dirtStormActive) return;
    if (!robotCacheBM || !robotMaskBM || !robotMaskBM->Planes[0]) return;

    sx = MAP_X + FP_TO_INT(dirtStormPx);
    sy = MAP_Y + (dirtStormTileY * TILE_SIZE);
    spinStep = (dirtStormSpinPhase >> 1) & 7;
    srcX = (dirtStormVariant * SPR_STATE_COUNT + SPR_UP) * ROBOT_W;

    /* A full continuous spin of one source frame reads as a proper tumble;
     * the old version cut between four different hand-drawn cardinal
     * sprites with only a single 45-degree blend between each. */
    DrawRobotBobRotated(srcX, sx, sy, spinStep * (BOBROT_STEPS / 8));

    /* All seven hoover variants share the same 16-colour sprite sheet
     * palette, just with different pens painted in their art, so there's
     * no free pen to permanently recolour just this one sprite without
     * risking corrupting art we can't preview on real hardware. Flicker
     * the same warning pens the stun indicator already uses instead, so
     * the storm reads as visibly damaged/sparking on top of whichever
     * variant got picked this pass. */
    {
        UBYTE sparkPen = EmpWarningPen();
        SetAPen(&renderRP, sparkPen);
        RectFill(&renderRP, sx + 2, sy + 2, sx + 3, sy + 3);
        RectFill(&renderRP, sx + 12, sy + 6, sx + 13, sy + 7);
        RectFill(&renderRP, sx + 6, sy + 11, sx + 7, sy + 12);
    }
}


static void DrawPuck(void)
{
    WORD sx;
    WORD sy;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PUCK) return;
    sx = MAP_X + FP_TO_INT(puckPx);
    sy = MAP_Y + FP_TO_INT(puckPy);

    if (puckBM && puckMaskBM && puckMaskBM->Planes[0]) {
        BltMaskBitMapRastPort(puckBM, 0, 0,
                              &renderRP, sx, sy,
                              PUCK_W, PUCK_H,
                              (ABC | ABNC | ANBC),
                              puckMaskBM->Planes[0]);
        return;
    }

    SetAPen(&renderRP, 13);
    RectFill(&renderRP, sx + 2, sy, sx + 9, sy + 11);
    RectFill(&renderRP, sx, sy + 3, sx + 11, sy + 8);
    SetAPen(&renderRP, 14);
    RectFill(&renderRP, sx + 3, sy + 2, sx + 5, sy + 3);
}


static void DrawAirHockeyPuck(void)
{
    WORD sx;
    WORD sy;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return;
    sx = MAP_X + FP_TO_INT(airhockeyPuckPx);
    sy = MAP_Y + FP_TO_INT(airhockeyPuckPy);

    if (puckBM && puckMaskBM && puckMaskBM->Planes[0]) {
        BltMaskBitMapRastPort(puckBM, 0, 0,
                              &renderRP, sx, sy,
                              AIRHOCKEY_W, AIRHOCKEY_H,
                              (ABC | ABNC | ANBC),
                              puckMaskBM->Planes[0]);
    } else {
        SetAPen(&renderRP, 13);
        RectFill(&renderRP, sx + 2, sy, sx + 9, sy + 11);
        RectFill(&renderRP, sx, sy + 3, sx + 11, sy + 8);
        SetAPen(&renderRP, 14);
        RectFill(&renderRP, sx + 3, sy + 2, sx + 5, sy + 3);
    }

    /* EMP power shot: flash a bright ring around the puck for a moment so a
     * boosted shot reads as something more than an ordinary touch. */
    if (airhockeyBoostFlashTicks > 0 && (airhockeyBoostFlashTicks & 2)) {
        SetAPen(&renderRP, 7);
        RectFill(&renderRP, sx - 2, sy - 2, sx + AIRHOCKEY_W + 1, sy - 1);
        RectFill(&renderRP, sx - 2, sy + AIRHOCKEY_H, sx + AIRHOCKEY_W + 1, sy + AIRHOCKEY_H + 1);
        RectFill(&renderRP, sx - 2, sy - 2, sx - 1, sy + AIRHOCKEY_H + 1);
        RectFill(&renderRP, sx + AIRHOCKEY_W, sy - 2, sx + AIRHOCKEY_W + 1, sy + AIRHOCKEY_H + 1);
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
    WORD centreSlot = ROBOT_VARIANTS / 2;
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

        if (joyEnabled[0]) {
            /* Once the physical stick is enabled, keep the neighbouring
             * hoovers visible but stop animating them and spin only the
             * selected one. The static panel cache only holds the empty
             * slot frames, not the robot sprites, so the neighbours must
             * still be drawn here (as a static pose) or they simply vanish.
             * This keeps J1 selection responsive on an 68020/030 while
             * retaining the full phase rate for the selected hoover. */
            if (slot == centreSlot) {
                DrawCachedTitleRobotSpinFrame(variant, baseFrame, x, y);
            } else {
                DrawTitleRobotStatic(variant, SPR_READY, x, y);
            }
        } else if (effectQuality == EFFECT_LOW) {
            if (slot < centreSlot - 1 || slot > centreSlot + 1) continue;
            if (slot == centreSlot) {
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
            MiniText(&renderRP, x + 2, 26, streakText, 14);
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


static void DrawLeaderboardScreen(BOOL finalBoard)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    WORD winnerScale;
    WORD winnerTop;
    char b[80];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    BuildRankOrder(order);
    finalWinner = order[0];
    titleSpinPhase = (titleSpinPhase + 1) & (TITLE_SPIN_STEPS - 1);
    winnerScale = 3 + ((titleSpinPhase >> 3) & 1);
    winnerTop = 80 - ((ROBOT_H * winnerScale) / 2);

    MiniTextCentered(&renderRP, 8, finalBoard ? "FINAL SCORE BOARD" : "CONGRATS WINNER", finalBoard ? 7 : 14, 2);
    snprintf(b, sizeof(b), "1ST %s %s", RobotControlLabel(order[0]), RobotTag(order[0]));
    MiniTextCentered(&renderRP, 30, b, 13, 2);
    DrawRobotLarge(order[0], (SCREEN_W - ROBOT_W * winnerScale) / 2,
                   winnerTop, winnerScale, titleSpinPhase);
    snprintf(b, sizeof(b), "SCORE:%d WINS:%d TABLES:%d",
             totalScores[order[0]], roundWins[order[0]], totalTablePushes[order[0]]);
    MiniTextCentered(&renderRP, 108, b, 10, 1);

    if (robotCount > 1) {
        DrawRobotIcon(order[1], 12, 122, SPR_DOWN);
        snprintf(b, sizeof(b), "2ND %s %s  %d", RobotControlLabel(order[1]), RobotTag(order[1]), totalScores[order[1]]);
        MiniText(&renderRP, 30, 130, b, 7);
    }
    if (robotCount > 2) {
        DrawRobotIcon(order[2], 156, 122, SPR_DOWN);
        snprintf(b, sizeof(b), "3RD %s %s  %d", RobotControlLabel(order[2]), RobotTag(order[2]), totalScores[order[2]]);
        MiniText(&renderRP, 174, 130, b, 7);
    }

    MiniTextCentered(&renderRP, 145, "BOARD", 8, 1);
    for (i = 0; i < robotCount && i < 10; i++) {
        WORD id = order[i];
        WORD col = i / 5;
        WORD row = i % 5;
        WORD x = 8 + (col * 154);
        WORD y = 158 + row * 17;
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


static void DrawMiniGameIntroScreen(void)
{
    WORD i;
    WORD shown = robotCount < 4 ? robotCount : 4;
    WORD spacing = SCREEN_W / (shown + 1);

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    MiniTextCentered(&renderRP, 24, "ROBO PARTY", 14, 4);
    if (miniGameType == MINIGAME_PUCK) {
        MiniTextCentered(&renderRP, 66, "ROBOPUCK", 10, 3);
        MiniTextCentered(&renderRP, 96, "3 MINUTE MATCH", 7, 2);
        MiniTextCentered(&renderRP, 112, "BUMP THE PUCK INTO THEIR GOAL", 13, 1);
        MiniTextCentered(&renderRP, 126, "FIRST TO 3  OR LEAD AT TIME", 14, 1);
    } else if (miniGameType == MINIGAME_BUMPER) {
        MiniTextCentered(&renderRP, 66, "BUMPER BOTS", 10, 3);
        MiniTextCentered(&renderRP, 96, "TINY ARENA  1 MINUTE", 7, 2);
        MiniTextCentered(&renderRP, 112, "BUMP OR BOLT RIVALS OFF THE RUG", 13, 1);
        MiniTextCentered(&renderRP, 126, "LAST BOT STANDING WINS", 14, 1);
    } else if (miniGameType == MINIGAME_AIRHOCKEY) {
        MiniTextCentered(&renderRP, 66, "ROBOHOCKEY", 10, 3);
        MiniTextCentered(&renderRP, 96, "2 MINUTE MATCH", 7, 2);
        MiniTextCentered(&renderRP, 112, "STAY ON YOUR SIDE  FIRE = EMP BOOST", 13, 1);
        MiniTextCentered(&renderRP, 126, "FIRST TO 5  OR LEAD AT TIME", 14, 1);
    } else if (miniGameType == MINIGAME_BOWLING) {
        MiniTextCentered(&renderRP, 66, "ROBO BOWLING", 10, 3);
        MiniTextCentered(&renderRP, 96, "TEN PINS  90 SECONDS", 7, 2);
        MiniTextCentered(&renderRP, 112, "DRIVE INTO A PIN TO KNOCK IT DOWN", 13, 1);
        MiniTextCentered(&renderRP, 126, "MOST PINS DOWN WINS", 14, 1);
    } else if (miniGameType == MINIGAME_FLOODHOUSE) {
        MiniTextCentered(&renderRP, 66, "FLOOD HOUSE", 10, 3);
        MiniTextCentered(&renderRP, 96, "30 SECONDS TO BUILD", 7, 2);
        MiniTextCentered(&renderRP, 112, "WALL ALL 4 SIDES OF YOUR HOME", 13, 1);
        MiniTextCentered(&renderRP, 126, "STEAL BLOCKS  STAY DRIEST", 14, 1);
    } else {
        MiniTextCentered(&renderRP, 66, "ROBORACE", 10, 3);
        MiniTextCentered(&renderRP, 96, "2 LAPS  HIT BOOST PADS", 7, 2);
        MiniTextCentered(&renderRP, 112, "BUMP RIVALS  PASS EVERY GATE", 13, 1);
        MiniTextCentered(&renderRP, 126, "BONUS POINTS  3  2  1", 14, 1);
    }

    for (i = 0; i < shown; i++) {
        DrawRobotLarge(i, spacing * (i + 1) - ROBOT_W, 150, 2,
                       (miniGameIntroTicks + i * 4) & (TITLE_SPIN_STEPS - 1));
    }
    MiniTextCentered(&renderRP, 224, "GET READY", 7, 2);
}


static void DrawMiniGameEndScreen(void)
{
    WORD points;
    WORD row = 0;
    char b[64];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    MiniTextCentered(&renderRP, 12,
                     miniGameType == MINIGAME_PUCK ? "ROBOPUCK RESULT" :
                     miniGameType == MINIGAME_BUMPER ? "BUMPER BOTS RESULT" :
                     miniGameType == MINIGAME_AIRHOCKEY ? "ROBOHOCKEY RESULT" :
                     miniGameType == MINIGAME_BOWLING ? "ROBO BOWLING RESULT" :
                     miniGameType == MINIGAME_FLOODHOUSE ? "FLOOD HOUSE RESULT" : "ROBORACE RESULT",
                     14, 3);

    if (miniGameWinner >= 0) {
        DrawRobotLarge(miniGameWinner, (SCREEN_W - ROBOT_W * 3) / 2, 58, 3, titleSpinPhase);
        if (miniGameType == MINIGAME_PUCK) {
            snprintf(b, sizeof(b), "TEAM %d WINS  %d-%d",
                     PuckTeamForRobot(miniGameWinner) + 1,
                     puckTeamScore[PuckTeamForRobot(miniGameWinner)],
                     puckTeamScore[1 - PuckTeamForRobot(miniGameWinner)]);
        } else if (miniGameType == MINIGAME_AIRHOCKEY) {
            snprintf(b, sizeof(b), "TEAM %d WINS  %d-%d",
                     AirHockeyTeamForRobot(miniGameWinner) + 1,
                     airhockeyTeamScore[AirHockeyTeamForRobot(miniGameWinner)],
                     airhockeyTeamScore[1 - AirHockeyTeamForRobot(miniGameWinner)]);
        } else if (miniGameType == MINIGAME_BOWLING) {
            snprintf(b, sizeof(b), "TEAM %d WINS  %d-%d",
                     BowlingTeamForRobot(miniGameWinner) + 1,
                     bowlingTeamScore[BowlingTeamForRobot(miniGameWinner)],
                     bowlingTeamScore[1 - BowlingTeamForRobot(miniGameWinner)]);
        } else {
            snprintf(b, sizeof(b), "%s %s WINS", RobotControlLabel(miniGameWinner), RobotTag(miniGameWinner));
        }
        MiniTextCentered(&renderRP, 112, b, 10, 2);
    }

    if (miniGameType == MINIGAME_PUCK || miniGameType == MINIGAME_AIRHOCKEY || miniGameType == MINIGAME_BOWLING) {
        MiniTextCentered(&renderRP, 148, "WINNING TEAM +3", 14, 2);
        MiniTextCentered(&renderRP, 174, "OTHER TEAM +1", 7, 2);
    } else {
        for (points = 3; points >= 1; points--) {
            WORD i;
            for (i = 0; i < robotCount; i++) {
                if (miniGamePoints[i] != points) continue;
                snprintf(b, sizeof(b), "%s %s  +%d", RobotControlLabel(i), RobotTag(i), points);
                MiniTextCentered(&renderRP, 142 + row * 18, b, row == 0 ? 14 : 7, 1);
                row++;
                break;
            }
        }
    }

    MiniTextCentered(&renderRP, 226, "SPACE FIRE NEXT ROUND", 13, 1);

    /* A thin rising water line along the very bottom edge, well clear of
     * the text above - the "copper effect" pitched for the flood moment,
     * kept to a plain animated RectFill rather than an actual palette or
     * copper-list effect, since this whole screen is already redrawn fresh
     * every frame (see the full-screen clear at the top of this function)
     * and a palette swap would need its own restore bookkeeping elsewhere
     * to guarantee it never leaves the screen tinted. */
    if (miniGameType == MINIGAME_FLOODHOUSE) {
        WORD waterH;
        if (floodPaletteTicks > 0) floodPaletteTicks--;
        waterH = ((FLOODHOUSE_PALETTE_TICKS - floodPaletteTicks) * 10) / FLOODHOUSE_PALETTE_TICKS;
        if (waterH > 10) waterH = 10;
        if (waterH > 0) {
            SetAPen(&renderRP, 11);
            RectFill(&renderRP, 0, SCREEN_H - waterH, SCREEN_W - 1, SCREEN_H - 1);
        }
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


static void DrawRaceHud(void)
{
    WORD i;
    WORD slotW = SCREEN_W / MAX_ROBOTS;
    WORD seconds = (raceTicksRemaining + 49) / 50;
    char b[32];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    snprintf(b, sizeof(b), "ROBORACE 0:%02d  %d LAPS", seconds, RACE_LAPS);
    MiniText(&renderRP, 4, 3, b, 14);
    DrawJoystickIcons();

    for (i = 0; i < robotCount; i++) {
        WORD x = i * slotW;
        WORD shownLap = raceLap[i] + 1;
        UBYTE pen = (i < humanPlayers) ? 14 : 7;

        if (shownLap > RACE_LAPS) shownLap = RACE_LAPS;
        SetAPen(&renderRP, (i < humanPlayers) ? 4 : 8);
        RectFill(&renderRP, x, 12, x + slotW - 2, 29);
        SetAPen(&renderRP, 0);
        RectFill(&renderRP, x + 1, 13, x + slotW - 3, 28);
        MiniText(&renderRP, x + 2, 15, RobotTag(i), pen);
        if (racePlace[i] >= 0) snprintf(b, sizeof(b), "F%d", racePlace[i] + 1);
        else snprintf(b, sizeof(b), "L%d", shownLap);
        MiniText(&renderRP, x + 19, 23, b, 13);
    }
}


static void DrawPuckHud(void)
{
    WORD totalSeconds = (puckTicksRemaining + 49) / 50;
    WORD minutes = totalSeconds / 60;
    WORD seconds = totalSeconds % 60;
    char b[64];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);
    if (puckTicksRemaining <= 0) {
        snprintf(b, sizeof(b), "ROBOPUCK  TEAM1 %d-%d TEAM2  OT",
                 puckTeamScore[0], puckTeamScore[1]);
    } else {
        snprintf(b, sizeof(b), "ROBOPUCK  TEAM1 %d-%d TEAM2  %d:%02d",
                 puckTeamScore[0], puckTeamScore[1], minutes, seconds);
    }
    MiniText(&renderRP, 4, 3, b, 14);
    if (puckScoringTeam >= 0) {
        if (puckScoringRobot >= 0 && puckScoringRobot < robotCount) {
            snprintf(b, sizeof(b), "%s GOAL! TEAM %d", RobotTag(puckScoringRobot), puckScoringTeam + 1);
        } else {
            snprintf(b, sizeof(b), "TEAM %d GOAL", puckScoringTeam + 1);
        }
        MiniTextCentered(&renderRP, 18, b, puckScoringTeam == 0 ? 13 : 14, 2);
    } else {
        MiniTextCentered(&renderRP, 18, "FIRST TO 3", 7, 1);
    }
    DrawJoystickIcons();
}


static void DrawAirHockeyHud(void)
{
    WORD totalSeconds = (airhockeyTicksRemaining + 49) / 50;
    WORD minutes = totalSeconds / 60;
    WORD seconds = totalSeconds % 60;
    char b[64];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);
    if (airhockeyTicksRemaining <= 0) {
        snprintf(b, sizeof(b), "ROBOHOCKEY  TEAM1 %d-%d TEAM2  OT",
                 airhockeyTeamScore[0], airhockeyTeamScore[1]);
    } else {
        snprintf(b, sizeof(b), "ROBOHOCKEY  TEAM1 %d-%d TEAM2  %d:%02d",
                 airhockeyTeamScore[0], airhockeyTeamScore[1], minutes, seconds);
    }
    MiniText(&renderRP, 4, 3, b, 14);
    if (airhockeyScoringTeam >= 0) {
        if (airhockeyScoringRobot >= 0 && airhockeyScoringRobot < robotCount) {
            snprintf(b, sizeof(b), "%s GOAL! TEAM %d", RobotTag(airhockeyScoringRobot), airhockeyScoringTeam + 1);
        } else {
            snprintf(b, sizeof(b), "TEAM %d GOAL", airhockeyScoringTeam + 1);
        }
        MiniTextCentered(&renderRP, 18, b, airhockeyScoringTeam == 0 ? 13 : 14, 2);
    } else {
        MiniTextCentered(&renderRP, 18, "FIRST TO 5  FIRE = EMP BOOST", 7, 1);
    }
    DrawJoystickIcons();
}


static void DrawBumperHud(void)
{
    WORD totalSeconds = (bumperTicksRemaining + 49) / 50;
    WORD minutes = totalSeconds / 60;
    WORD seconds = totalSeconds % 60;
    char b[64];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    snprintf(b, sizeof(b), "BUMPER BOTS  %d LEFT  %d:%02d", bumperAliveCount, minutes, seconds);
    MiniText(&renderRP, 4, 3, b, 14);

    if (bumperEliminatedFlashTicks > 0 && bumperEliminatedRobot >= 0) {
        if (bumperEliminatedBy >= 0) {
            snprintf(b, sizeof(b), "%s KO'D %s", RobotTag(bumperEliminatedBy), RobotTag(bumperEliminatedRobot));
        } else {
            snprintf(b, sizeof(b), "%s FELL OFF", RobotTag(bumperEliminatedRobot));
        }
        MiniTextCentered(&renderRP, 18, b, 13, 2);
    } else {
        MiniTextCentered(&renderRP, 18, "LAST BOT STANDING", 7, 1);
    }
    DrawJoystickIcons();
}


static void DrawBowlingHud(void)
{
    WORD totalSeconds = (bowlingTicksRemaining + 49) / 50;
    WORD minutes = totalSeconds / 60;
    WORD seconds = totalSeconds % 60;
    char b[64];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    snprintf(b, sizeof(b), "ROBO BOWLING  TEAM1 %d-%d TEAM2  %d PINS  %d:%02d",
             bowlingTeamScore[0], bowlingTeamScore[1], bowlingPinsRemaining, minutes, seconds);
    MiniText(&renderRP, 4, 3, b, 14);

    if (bowlingFlashTicks > 0 && bowlingLastKnockedRobot >= 0) {
        snprintf(b, sizeof(b), "%s PIN DOWN! TEAM %d", RobotTag(bowlingLastKnockedRobot), bowlingLastKnockedTeam + 1);
        MiniTextCentered(&renderRP, 18, b, bowlingLastKnockedTeam == 0 ? 13 : 14, 2);
    } else {
        MiniTextCentered(&renderRP, 18, "MOST PINS DOWN WINS", 7, 1);
    }
    DrawJoystickIcons();
}


static void DrawFloodHouseHud(void)
{
    WORD totalSeconds = (floodTicksRemaining + 49) / 50;
    WORD minutes = totalSeconds / 60;
    WORD seconds = totalSeconds % 60;
    char b[64];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    if (floodPauseTicks > 0) {
        MiniText(&renderRP, 4, 3, "FLOOD HOUSE  THE WATER IS RISING", 14);
        MiniTextCentered(&renderRP, 18, "SURROUNDED HOMES STAY DRY", 11, 1);
        DrawJoystickIcons();
        return;
    }

    snprintf(b, sizeof(b), "FLOOD HOUSE  BUILD YOUR WALLS  %d:%02d",
             minutes, seconds);
    MiniText(&renderRP, 4, 3, b, 14);

    if (floodFlashTicks > 0 && floodLastEventRobot >= 0 && floodLastEventKind != FLOODHOUSE_EVENT_NONE) {
        if (floodLastEventKind == FLOODHOUSE_EVENT_BUILT) {
            snprintf(b, sizeof(b), "%s BUILT A WALL", RobotTag(floodLastEventRobot));
        } else if (floodLastEventKind == FLOODHOUSE_EVENT_RAIDED) {
            snprintf(b, sizeof(b), "%s RAIDED A RIVAL", RobotTag(floodLastEventRobot));
        } else {
            snprintf(b, sizeof(b), "%s GRABBED A BLOCK", RobotTag(floodLastEventRobot));
        }
        MiniTextCentered(&renderRP, 18, b, 13, 2);
    } else {
        MiniTextCentered(&renderRP, 18, "WALL YOUR HOME  DRIEST WINS", 7, 1);
    }
    DrawJoystickIcons();
}


static void DrawHud(void)
{
    char b[160];

    SetAPen(&renderRP, 0);
    RectFill(&renderRP, 0, 0, SCREEN_W - 1, HUD_H - 1);

    DrawJoystickIcons();

    if (gameState == GAME_MINIGAME_PLAYING) {
        if (miniGameType == MINIGAME_PUCK) DrawPuckHud();
        else if (miniGameType == MINIGAME_BUMPER) DrawBumperHud();
        else if (miniGameType == MINIGAME_AIRHOCKEY) DrawAirHockeyHud();
        else if (miniGameType == MINIGAME_BOWLING) DrawBowlingHud();
        else if (miniGameType == MINIGAME_FLOODHOUSE) DrawFloodHouseHud();
        else DrawRaceHud();
        return;
    }

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
        /* The health strip's boxes run down to y=29; a baseline of just 30
         * put this text's ascent right through the bottom row of scores.
         * Push it down a line so it sits clear underneath. */
        PutText(&renderRP, 62, 42, "Press Space/Fire for next round", 7);
        return;
    }

    if (gameState == GAME_MATCH_END || gameState == GAME_BONUS_END) {
        snprintf(b, sizeof(b), "MATCH WINNER: %s", RobotName(finalWinner));
        PutText(&renderRP, 42, 10, b, 12);
        DrawRobotHealthStrip();
        PutText(&renderRP, 72, 42, bonusAvailable ? "Space/Fire bonus" : "Space/Fire", 7);
        return;
    }

    if (lastPowerTicks > 0 && lastPowerText[0]) {
        PutText(&renderRP, 4, 8, lastPowerText, 14);
    } else {
        if (dirtyHudStatusText) {
            if (gameState == GAME_BONUS_PLAYING) {
                snprintf(cachedHudStatusText, sizeof(cachedHudStatusText),
                         "R6 BONUS BOSS:%d MOVE:%d", bonusBossHealth, batteryCostPerMove);
            } else if (hooverModeActive) {
                snprintf(cachedHudStatusText, sizeof(cachedHudStatusText),
                         "HOOVER MODE %s DIRT:%d", roomNames[roomType], dirtLeft);
            } else if (demoModeActive) {
                snprintf(cachedHudStatusText, sizeof(cachedHudStatusText),
                         "ATTRACT %s DIRT:%d", roomNames[roomType], dirtLeft);
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

    if (!IsArenaPlaying()) return;
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

    MiniTextCentered(&renderRP, top + 10, (humanPlayers >= 2) ? "TWO PLAYER AI" : "AI RIVALS", 7, 2);
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

static void DrawPuckIntersectingRect(struct DirtyRect *rect)
{
    WORD x;
    WORD y;

    if (!rect || gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PUCK) return;
    x = MAP_X + FP_TO_INT(puckPx);
    y = MAP_Y + FP_TO_INT(puckPy);
    if (RectIntersects(rect->x, rect->y, rect->w, rect->h, x, y, PUCK_W, PUCK_H)) DrawPuck();
}

static void DrawAirHockeyPuckIntersectingRect(struct DirtyRect *rect)
{
    WORD x;
    WORD y;

    if (!rect || gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return;
    x = MAP_X + FP_TO_INT(airhockeyPuckPx) - 2;
    y = MAP_Y + FP_TO_INT(airhockeyPuckPy) - 2;
    if (RectIntersects(rect->x, rect->y, rect->w, rect->h, x, y, AIRHOCKEY_W + 4, AIRHOCKEY_H + 4)) DrawAirHockeyPuck();
}

static void DrawDirtStormIntersectingRect(struct DirtyRect *rect)
{
    WORD x;
    WORD y;

    if (!rect || !dirtStormActive) return;
    x = MAP_X + FP_TO_INT(dirtStormPx);
    y = MAP_Y + (dirtStormTileY * TILE_SIZE);
    if (RectIntersects(rect->x, rect->y, rect->w, rect->h, x, y, ROBOT_W, ROBOT_H)) DrawDirtStorm();
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
        DrawPuckIntersectingRect(&rect);
        DrawAirHockeyPuckIntersectingRect(&rect);
        DrawDirtStormIntersectingRect(&rect);
        DrawRobotsIntersectingRect(&rect);
        DrawBoltsIntersectingRect(&rect);
        if (BonusBossExplosionIntersectsRect(&rect)) DrawBossExplosion();
        if (BonusBossIntersectsRect(&rect)) DrawBonusBoss();
        DrawGameplayDirtyOverlays(&rect);
    }
}
#endif


/* The rising water during the live flood-moment pause (see floodPauseTicks
 * in StepFloodHouse) - a plain animated RectFill like the result screen's
 * water line, not an actual palette/copper effect, for the same reason:
 * this whole frame is already forced to a full present every pause tick
 * (see ForceGameplayFullPresent in StepFloodHouse), so there is no dirty
 * rect bookkeeping to get wrong by keeping it simple. */
static void DrawFloodWaterOverlay(void)
{
    WORD mapBottom = MAP_Y + (MAP_H * TILE_SIZE);
    WORD waterH = ((FLOODHOUSE_FLOOD_PAUSE_TICKS - floodPauseTicks) * 24) / FLOODHOUSE_FLOOD_PAUSE_TICKS;

    if (waterH > 24) waterH = 24;
    if (waterH <= 0) return;

    SetAPen(&renderRP, 11);
    RectFill(&renderRP, MAP_X, mapBottom - waterH, MAP_X + (MAP_W * TILE_SIZE) - 1, mapBottom - 1);
}


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

    if (gameState == GAME_MINIGAME_INTRO) {
        DrawMiniGameIntroScreen();
        return;
    }

    if (gameState == GAME_MINIGAME_END) {
        DrawMiniGameEndScreen();
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
    if (IsArenaPlaying()) {
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

    DrawPuck();
    DrawAirHockeyPuck();

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
    DrawDirtStorm();
    DrawRoundStartOverlay();
    DrawEmpRobotVisuals();

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_FLOODHOUSE && floodPauseTicks > 0) {
        DrawFloodWaterOverlay();
    }

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
    if (!IsArenaPlaying()) return FALSE;
    if (!dirtyRectsBuiltForFrame) return FALSE;
    if (!dirtyRectsValid) return FALSE;
    if (dirtyRectCount <= 0) return FALSE;
    if (dirtyForceFullFrame) return FALSE;
    if (roundCountdownTicks > 0 || pauseMenuOpen) return FALSE;
    return TRUE;
}


static BOOL DirtyGameplayNoChanges(void)
{
    if (!IsArenaPlaying()) return FALSE;
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
    if (IsArenaPlaying()) {
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