#include "robovac.h"


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


static void ChooseRaceAiMove(WORD id)
{
    const struct RaceCheckpoint *gate;
    WORD bestDx = 0;
    WORD bestDy = 0;
    WORD bestDist = 32767;
    WORD x;
    WORD y;

    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].moving || robots[id].stunTicks > 0 || racePlace[id] >= 0) return;

    gate = &raceCheckpoints[raceNextCheckpoint[id]];
    for (y = gate->minY; y <= gate->maxY; y++) {
        for (x = gate->minX; x <= gate->maxX; x++) {
            WORD dx = 0;
            WORD dy = 0;
            WORD dist = 32767;
            if (AiFindPathStep(id, x, y, &dx, &dy, &dist) && dist < bestDist) {
                bestDx = dx;
                bestDy = dy;
                bestDist = dist;
            }
        }
    }

    if ((bestDx != 0 || bestDy != 0) && StartRobotMove(id, bestDx, bestDy)) return;

    /* A gate can be briefly blocked by the pack.  Keep trying legal moves
     * instead of freezing until the exact preferred lane clears. */
    {
        WORD dirs[4];
        static const WORD dirX[4] = {1, 0, -1, 0};
        static const WORD dirY[4] = {0, 1, 0, -1};
        WORD i;
        ShuffleAiDirs(dirs);
        for (i = 0; i < 4; i++) {
            WORD dir = dirs[i];
            if (StartRobotMove(id, dirX[dir], dirY[dir])) return;
        }
    }
}


static void ChoosePuckAiMove(WORD id)
{
    WORD targetX;
    WORD targetY;
    WORD dx = 0;
    WORD dy = 0;
    WORD team;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PUCK) return;
    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].moving || robots[id].stunTicks > 0 || puckGoalPauseTicks > 0) return;

    team = PuckTeamForRobot(id);
    targetX = FP_TO_INT(puckPx + TO_FP(PUCK_W / 2)) / TILE_SIZE;
    targetY = FP_TO_INT(puckPy + TO_FP(PUCK_H / 2)) / TILE_SIZE;

    /* Approach from behind so the next tile step knocks the puck toward the
     * opponent's goal instead of merely orbiting it. */
    targetX += (team == 0) ? -1 : 1;
    if (targetX < 1) targetX = 1;
    if (targetX > MAP_W - 2) targetX = MAP_W - 2;
    if (targetY < 1) targetY = 1;
    if (targetY > MAP_H - 2) targetY = MAP_H - 2;

    if (AiFindPathStep(id, targetX, targetY, &dx, &dy, NULL) &&
        (dx != 0 || dy != 0) && StartRobotMove(id, dx, dy)) return;

    /* If already behind the puck, drive straight through it. */
    dx = (team == 0) ? 1 : -1;
    if (StartRobotMove(id, dx, 0)) return;

    if (robots[id].tileY < targetY) StartRobotMove(id, 0, 1);
    else if (robots[id].tileY > targetY) StartRobotMove(id, 0, -1);
}


static void ChooseAirHockeyAiMove(WORD id)
{
    WORD targetX;
    WORD targetY;
    WORD dx = 0;
    WORD dy = 0;
    WORD team;
    WORD dtx;
    WORD dty;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return;
    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].moving || robots[id].stunTicks > 0 || airhockeyGoalPauseTicks > 0) return;

    team = AirHockeyTeamForRobot(id);
    targetX = FP_TO_INT(airhockeyPuckPx + TO_FP(AIRHOCKEY_W / 2)) / TILE_SIZE;
    targetY = FP_TO_INT(airhockeyPuckPy + TO_FP(AIRHOCKEY_H / 2)) / TILE_SIZE;

    dtx = AbsW(robots[id].tileX - targetX);
    dty = AbsW(robots[id].tileY - targetY);
    if (dtx <= AIRHOCKEY_BOOST_RANGE && dty <= AIRHOCKEY_BOOST_RANGE &&
        airhockeyBoostCooldown[id] <= 0 && TryAirHockeyBoost(id)) return;

    /* Approach from behind so the next tile step knocks the puck toward the
     * opponent's goal instead of merely orbiting it. */
    targetY += (team == 0) ? -1 : 1;
    if (targetX < 1) targetX = 1;
    if (targetX > MAP_W - 2) targetX = MAP_W - 2;

    /* Never target a tile across the half-line - RobotCanPassTile already
     * refuses to move there, so clamp the goal itself rather than let
     * AiFindPathStep fail to find any path at all. */
    if (team == 0) {
        if (targetY < 1) targetY = 1;
        if (targetY >= AIRHOCKEY_HALF_Y) targetY = AIRHOCKEY_HALF_Y - 1;
    } else {
        if (targetY < AIRHOCKEY_HALF_Y) targetY = AIRHOCKEY_HALF_Y;
        if (targetY > MAP_H - 2) targetY = MAP_H - 2;
    }

    if (AiFindPathStep(id, targetX, targetY, &dx, &dy, NULL) &&
        (dx != 0 || dy != 0) && StartRobotMove(id, dx, dy)) return;

    /* If already behind the puck, drive straight through it. */
    dy = (team == 0) ? 1 : -1;
    if (StartRobotMove(id, 0, dy)) return;

    if (robots[id].tileX < targetX) StartRobotMove(id, 1, 0);
    else if (robots[id].tileX > targetX) StartRobotMove(id, -1, 0);
}


static void ChooseBumperAiMove(WORD id)
{
    static const WORD dirX[4] = {1, 0, -1, 0};
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD dirs[4];
    WORD dir;
    WORD curX;
    WORD curY;
    WORD targetId = -1;
    WORD bestDist = 32767;
    WORD i;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BUMPER) return;
    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].moving || robots[id].stunTicks > 0 || bumperEliminated[id]) return;

    curX = robots[id].tileX;
    curY = robots[id].tileY;

    for (i = 0; i < robotCount; i++) {
        WORD dist;
        if (i == id || bumperEliminated[i]) continue;
        dist = AbsW(robots[i].tileX - curX) + AbsW(robots[i].tileY - curY);
        if (dist < bestDist) {
            bestDist = dist;
            targetId = i;
        }
    }

    if (targetId >= 0) {
        WORD tdx = robots[targetId].tileX - curX;
        WORD tdy = robots[targetId].tileY - curY;
        WORD stepDx = (tdx > 0) - (tdx < 0);
        WORD stepDy = (tdy > 0) - (tdy < 0);

        /* Adjacent to a rival: shove straight into them rather than
         * sidestepping around, so the chase actually lands a bump. */
        if (bestDist <= 1) {
            if (StartRobotMove(id, stepDx, stepDy)) return;
        }

        /* Otherwise close the gap, working the axis with more distance
         * left first so the chase doesn't hug a single edge of the rug. */
        if (AbsW(tdx) >= AbsW(tdy) && stepDx != 0) {
            if (StartRobotMove(id, stepDx, 0)) return;
        }
        if (stepDy != 0) {
            if (StartRobotMove(id, 0, stepDy)) return;
        }
        if (stepDx != 0) {
            if (StartRobotMove(id, stepDx, 0)) return;
        }
    }

    ShuffleAiDirs(dirs);
    for (dir = 0; dir < 4; dir++) {
        WORD r = dirs[dir];
        if (StartRobotMove(id, dirX[r], dirY[r])) return;
    }
}


static void ChooseHooverModeMove(WORD id)
{
    static const WORD dirX[4] = {1, 0, -1, 0};
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD preferred;
    WORD bestDir = -1;
    WORD bestScore = -32767;
    WORD dir;

    if (id < humanPlayers || id >= robotCount) return;
    if (robots[id].moving || robots[id].stunTicks > 0) return;

    /* Keep the showcase running long enough to be useful: when a hoover is
     * low on charge, use the normal shortest path to its dock, then resume
     * its straight sweep once it has refilled. */
    if (hooverModeActive && robots[id].battery <= 25) {
        /* D is a cleaning showcase rather than a battery-management game.
         * Refill between sweeps so the AI cannot strand itself at a dock or
         * wait forever when another hoover temporarily blocks a path. */
        robots[id].battery = maxBattery;
    } else if (robots[id].battery <= 25) {
        WORD dx = 0;
        WORD dy = 0;
        if (AiFindPathStep(id, RobotDockX(id), RobotDockY(id), &dx, &dy, NULL) &&
            (dx != 0 || dy != 0)) {
            StartRobotMove(id, dx, dy);
        }
        return;
    }

    /* Each AI normally holds a straight line. Occasionally bend the line so
     * the showcase does not look like four clockwork toys, then use a small
     * amount of look-ahead when a wall, table, or another hoover blocks it. */
    preferred = hooverModeDir[id] & 3;
    if (RandRange(HOOVER_RANDOM_TURN_CHANCE) == 0) {
        preferred = (preferred + 1 + RandRange(3)) & 3;
    }

    if (StartRobotMove(id, dirX[preferred], dirY[preferred])) {
        hooverModeDir[id] = preferred;
        return;
    }

    /* Score alternatives instead of always turning clockwise. This favours
     * dirt and open space, penalises immediate backtracking, and explicitly
     * refuses a tile occupied or already targeted by another hoover. */
    for (dir = 0; dir < 4; dir++) {
        WORD nx = robots[id].tileX + dirX[dir];
        WORD ny = robots[id].tileY + dirY[dir];
        WORD score = 0;
        WORD exits = 0;
        WORD look;

        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
        if (map[ny][nx] == TILE_TABLE) {
            WORD pushX = nx + dirX[dir];
            WORD pushY = ny + dirY[dir];
            if (pushX <= 0 || pushY <= 0 || pushX >= MAP_W - 1 || pushY >= MAP_H - 1 ||
                map[pushY][pushX] != TILE_FLOOR || RobotAtTile(pushX, pushY, id)) continue;
        } else if (!RobotCanPassTile(id, nx, ny)) {
            continue;
        }
        if (RobotAtTile(nx, ny, id)) continue;

        if (map[ny][nx] == TILE_DIRT) score += 20;
        if (dir == preferred) score += 10;
        if (dir == ((preferred + 2) & 3)) score -= 12;
        score -= AiRecentVisitPenalty(id, nx, ny);

        for (look = 0; look < 4; look++) {
            WORD ex = nx + dirX[look];
            WORD ey = ny + dirY[look];
            if (ex < 0 || ey < 0 || ex >= MAP_W || ey >= MAP_H) continue;
            if (!RobotCanPassTile(id, ex, ey) || RobotAtTile(ex, ey, id)) continue;
            exits++;
        }
        score += exits * 3;
        score += (WORD)RandRange(6); /* break ties without looking scripted */

        if (score > bestScore) {
            bestScore = score;
            bestDir = dir;
        }
    }

    if (bestDir >= 0 && StartRobotMove(id, dirX[bestDir], dirY[bestDir])) {
        hooverModeDir[id] = bestDir;
        return;
    }

    /* Four hoovers can briefly box one another in. A fresh random heading
     * gives them a chance to separate on the next decision tick. */
    hooverModeDir[id] = (preferred + 1 + RandRange(3)) & 3;
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

    if (gameState == GAME_MINIGAME_PLAYING) {
        if (miniGameType == MINIGAME_RACE) ChooseRaceAiMove(id);
        else if (miniGameType == MINIGAME_PUCK) ChoosePuckAiMove(id);
        else if (miniGameType == MINIGAME_BUMPER) ChooseBumperAiMove(id);
        else if (miniGameType == MINIGAME_AIRHOCKEY) ChooseAirHockeyAiMove(id);
        return;
    }

    if (hooverModeActive) {
        ChooseHooverModeMove(id);
        return;
    }

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
        WORD stormTx = -1;
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
            stormTx = -1;
            bestDist = dist;
        }

        /* The runaway broken hoover carries a bonus for whoever stops it -
         * let AI take the same shot a human already can, whenever it
         * happens to be lined up on the storm's row. */
        if (dirtStormActive && robots[id].tileY == dirtStormTileY) {
            WORD candidateTx = FP_TO_INT(dirtStormPx) / TILE_SIZE;
            WORD dist = AbsW(candidateTx - robots[id].tileX);
            if (dist > 0 && dist <= fireRange && dist < bestDist &&
                ClearBoltLane(robots[id].tileX, robots[id].tileY, candidateTx, dirtStormTileY)) {
                target = -1;
                stormTx = candidateTx;
                bestDist = dist;
            }
        }

        if (stormTx >= 0) {
            FireRobotBolt(id, (stormTx < robots[id].tileX) ? -1 : 1, 0, TRUE, TRUE);
            return;
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


static void StepBumperAiFire(void)
{
    static WORD nextAiShooter = 0;
    WORD tries;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BUMPER) return;
    if (RoundStartLocked()) return;
    if (humanPlayers >= robotCount) return;

    if (nextAiShooter < humanPlayers || nextAiShooter >= robotCount) nextAiShooter = humanPlayers;
    for (tries = 0; tries < robotCount; tries++) {
        WORD id = nextAiShooter;
        WORD target = -1;
        WORD bestDist = BUMPER_AI_FIRE_RANGE + 1;
        WORD j;

        nextAiShooter++;
        if (nextAiShooter >= robotCount) nextAiShooter = humanPlayers;

        if (id < humanPlayers || id >= robotCount) continue;
        if (bumperEliminated[id]) continue;
        if (robots[id].stunTicks > 0 || playerBolts[id].active) continue;
        if (RandRange(BUMPER_AI_FIRE_CHANCE) != 0) continue;

        for (j = 0; j < robotCount; j++) {
            WORD dist;
            if (j == id || bumperEliminated[j]) continue;
            if (robots[j].tileX != robots[id].tileX && robots[j].tileY != robots[id].tileY) continue;
            dist = AbsW(robots[j].tileX - robots[id].tileX) + AbsW(robots[j].tileY - robots[id].tileY);
            if (dist <= 0 || dist > BUMPER_AI_FIRE_RANGE || dist >= bestDist) continue;
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
            FireRobotBolt(id, dirX, dirY, FALSE, TRUE);
            return;
        }
    }
}