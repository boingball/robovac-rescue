#include "robovac.h"


static WORD ChooseNextMiniGame(void)
{
    WORD choice;

    if (MINIGAME_COUNT <= 1) return MINIGAME_RACE;
    do {
        choice = 1 + (WORD)RandRange(MINIGAME_COUNT);
    } while (choice == lastMiniGame);
    return choice;
}


static void StartMiniGameIntro(void)
{
    WORD i;

    StopMenuMusic();
    StopGameplaySamples();
    StopRoundStartSamples();
    StopEmpPaletteCycle();
    StopNightMode();
    ClosePauseMenu();
    ClearMovementKeys();

    miniGameType = ChooseNextMiniGame();
    lastMiniGame = miniGameType;
    miniGameIntroTicks = MINIGAME_INTRO_TICKS;
    miniGameWinner = -1;
    for (i = 0; i < MAX_ROBOTS; i++) miniGamePoints[i] = 0;

    gameState = GAME_MINIGAME_INTRO;
    ForceGameplayFullPresent();
}


static void StartRoboRace(void)
{
    WORD x;
    WORD y;
    WORD i;

    StopGameplaySamples();
    StopRoundStartSamples();
    StopEmpPaletteCycle();
    StopNightMode();
    ClearMovementKeys();
    ClosePauseMenu();

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            map[y][x] = (raceLayout[y][x] == '#') ? TILE_WALL : TILE_FLOOR;
        }
    }

    /* Draw the four gates with the spare marker tile. */
    for (i = 0; i < RACE_CHECKPOINT_COUNT; i++) {
        WORD gx;
        WORD gy;
        for (gy = raceCheckpoints[i].minY; gy <= raceCheckpoints[i].maxY; gy++) {
            for (gx = raceCheckpoints[i].minX; gx <= raceCheckpoints[i].maxX; gx++) {
                map[gy][gx] = TILE_MARKER;
            }
        }
    }

    /* Existing dock artwork doubles as four battery boost pads. */
    map[10][17] = TILE_DOCK;
    map[2][16] = TILE_DOCK;
    map[2][2] = TILE_DOCK;
    map[10][2] = TILE_DOCK;

    ClearDirtList();
    { WORD bi; for (bi = 0; bi < MAX_ROBOTS; bi++) playerBolts[bi].active = FALSE; }
    { WORD bi; for (bi = 0; bi < MAX_BOSS_BOLTS; bi++) bossBolts[bi].active = FALSE; }
    dirtStormActive = FALSE;
    empCountdownTicks = 0;
    empCountdownOwner = -1;
    lastPowerText[0] = '\0';
    lastPowerTicks = 0;

    gameState = GAME_MINIGAME_PLAYING;
    InitRobots();
    for (i = 0; i < robotCount; i++) {
        UBYTE variant = robots[i].spriteVariant;
        WORD h;

        SetRobotTile(i, raceStartX[i], raceStartY[i]);
        robots[i].spriteVariant = variant;
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        raceLap[i] = 0;
        raceNextCheckpoint[i] = 1;
        racePlace[i] = -1;
        raceBoostMoves[i] = 0;
        speedFlashTicks[i] = 0;
        aiPrevTileX[i] = raceStartX[i];
        aiPrevTileY[i] = raceStartY[i];
        for (h = 0; h < AI_RECENT_TILE_COUNT; h++) {
            aiRecentTileX[i][h] = raceStartX[i];
            aiRecentTileY[i][h] = raceStartY[i];
        }
    }
    for (i = robotCount; i < MAX_ROBOTS; i++) {
        raceLap[i] = 0;
        raceNextCheckpoint[i] = 1;
        racePlace[i] = -1;
        raceBoostMoves[i] = 0;
    }

    raceFinishCount = 0;
    raceTicksRemaining = RACE_TIME_TICKS;
    raceFinishGraceTicks = 0;
    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    MarkHudStatusTextDirty();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


static WORD PuckTeamForRobot(WORD id)
{
    return id & 1;
}


static void ResetPuckPosition(void)
{
    puckPx = TO_FP((SCREEN_W - PUCK_W) / 2);
    puckPy = TO_FP(((MAP_H * TILE_SIZE) - PUCK_H) / 2);
    puckVx = 0;
    puckVy = 0;
    puckHitCooldownTicks = 0;
    puckLastTouch = -1;
}


static void StartRoboPuck(void)
{
    WORD x;
    WORD y;
    WORD i;

    StopGameplaySamples();
    StopRoundStartSamples();
    StopEmpPaletteCycle();
    StopNightMode();
    ClearMovementKeys();
    ClosePauseMenu();

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) {
                map[y][x] = TILE_WALL;
            } else {
                map[y][x] = TILE_FLOOR;
            }
        }
    }

    /* The marker tiles are open goal mouths, not obstacles.  Team 0 starts
     * on the left and attacks right; Team 1 mirrors it. */
    for (y = PUCK_GOAL_TOP_TILE; y <= PUCK_GOAL_BOTTOM_TILE; y++) {
        map[y][0] = TILE_MARKER;
        map[y][MAP_W - 1] = TILE_MARKER;
    }

    ClearDirtList();
    for (i = 0; i < MAX_ROBOTS; i++) playerBolts[i].active = FALSE;
    for (i = 0; i < MAX_BOSS_BOLTS; i++) bossBolts[i].active = FALSE;
    dirtStormActive = FALSE;
    empCountdownTicks = 0;
    empCountdownOwner = -1;
    lastPowerText[0] = '\0';
    lastPowerTicks = 0;

    gameState = GAME_MINIGAME_PLAYING;
    InitRobots();
    for (i = 0; i < robotCount; i++) {
        UBYTE variant = robots[i].spriteVariant;
        WORD team = PuckTeamForRobot(i);
        WORD slot = i >> 1;
        WORD startY = 3 + ((slot * 2) % 9);

        SetRobotTile(i, team == 0 ? 3 : MAP_W - 4, startY);
        robots[i].spriteVariant = variant;
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        robots[i].spriteIndex = team == 0 ? SPR_RIGHT : SPR_LEFT;
        robots[i].prevSpriteIndex = robots[i].spriteIndex;
        robots[i].turnTicks = 0;
        raceBoostMoves[i] = 0;
        speedFlashTicks[i] = 0;
        aiPrevTileX[i] = robots[i].tileX;
        aiPrevTileY[i] = robots[i].tileY;
    }

    puckTeamScore[0] = 0;
    puckTeamScore[1] = 0;
    puckTicksRemaining = PUCK_TIME_TICKS;
    puckGoalPauseTicks = 0;
    puckScoringTeam = -1;
    puckScoringRobot = -1;
    ResetPuckPosition();
#if USE_DIRTY_RECTS
    dirtyPrevPuckValid = FALSE;
#endif
    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    MarkHudStatusTextDirty();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


static void StartBumperBots(void)
{
    WORD x;
    WORD y;
    WORD i;

    StopGameplaySamples();
    StopRoundStartSamples();
    StopEmpPaletteCycle();
    StopNightMode();
    ClearMovementKeys();
    ClosePauseMenu();

    /* The whole map is void/abyss (the wall art doubles as "off the rug");
     * only the small rectangle in the middle is solid floor. */
    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            BOOL inArena = (x >= BUMPER_ARENA_MIN_X && x <= BUMPER_ARENA_MAX_X &&
                            y >= BUMPER_ARENA_MIN_Y && y <= BUMPER_ARENA_MAX_Y);
            map[y][x] = inArena ? TILE_FLOOR : TILE_WALL;
        }
    }

    ClearDirtList();
    for (i = 0; i < MAX_ROBOTS; i++) playerBolts[i].active = FALSE;
    for (i = 0; i < MAX_BOSS_BOLTS; i++) bossBolts[i].active = FALSE;
    dirtStormActive = FALSE;
    empCountdownTicks = 0;
    empCountdownOwner = -1;
    lastPowerText[0] = '\0';
    lastPowerTicks = 0;

    gameState = GAME_MINIGAME_PLAYING;
    InitRobots();
    bumperAliveCount = robotCount;
    bumperEliminationSeq = 0;
    bumperEliminatedRobot = -1;
    bumperEliminatedBy = -1;
    bumperEliminatedFlashTicks = 0;
    for (i = 0; i < robotCount; i++) {
        UBYTE variant = robots[i].spriteVariant;

        SetRobotTile(i, bumperStartX[i], bumperStartY[i]);
        robots[i].spriteVariant = variant;
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        bumperEliminated[i] = FALSE;
        bumperEliminatedSeq[i] = -1;
        bumperFallTicks[i] = 0;
        bumperSliding[i] = FALSE;
        aiPrevTileX[i] = robots[i].tileX;
        aiPrevTileY[i] = robots[i].tileY;
    }

    bumperTicksRemaining = BUMPER_TIME_TICKS;

    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    MarkHudStatusTextDirty();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


static BOOL RaceCheckpointReached(WORD id, WORD checkpoint)
{
    const struct RaceCheckpoint *gate;
    WORD x;
    WORD y;

    if (id < 0 || id >= robotCount) return FALSE;
    if (checkpoint < 0 || checkpoint >= RACE_CHECKPOINT_COUNT) return FALSE;
    gate = &raceCheckpoints[checkpoint];
    x = robots[id].tileX;
    y = robots[id].tileY;
    return (x >= gate->minX && x <= gate->maxX &&
            y >= gate->minY && y <= gate->maxY) ? TRUE : FALSE;
}


static void RaceHandleRobotArrival(WORD id)
{
    WORD checkpoint;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_RACE) return;
    if (id < 0 || id >= robotCount) return;

    if (map[robots[id].tileY][robots[id].tileX] == TILE_DOCK) {
        raceBoostMoves[id] = RACE_BOOST_MOVES;
        speedFlashTicks[id] = SPEED_FLASH_TICKS;
    }

    if (racePlace[id] >= 0) return;
    checkpoint = raceNextCheckpoint[id];
    if (!RaceCheckpointReached(id, checkpoint)) return;

    if (checkpoint == 0) {
        raceLap[id]++;
        if (raceLap[id] >= RACE_LAPS) {
            racePlace[id] = raceFinishCount++;
            robots[id].moving = FALSE;
            if (raceFinishCount == 1) raceFinishGraceTicks = RACE_FINISH_GRACE_TICKS;
        }
    }
    raceNextCheckpoint[id] = (checkpoint + 1) % RACE_CHECKPOINT_COUNT;
    MarkHudStatusTextDirty();
}


static LONG RaceProgressScore(WORD id)
{
    WORD checkpoint;
    WORD gatesPassed;
    WORD distance;

    if (racePlace[id] >= 0) return 20000L - ((LONG)racePlace[id] * 1000L);

    checkpoint = raceNextCheckpoint[id];
    if (checkpoint == 0) gatesPassed = 3;
    else gatesPassed = checkpoint - 1;
    distance = AbsW(robots[id].tileX - raceCheckpoints[checkpoint].targetX) +
               AbsW(robots[id].tileY - raceCheckpoints[checkpoint].targetY);
    return ((LONG)(raceLap[id] * RACE_CHECKPOINT_COUNT + gatesPassed) * 100L) - distance;
}


static void FinishRoboRace(void)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    WORD pass;
    static const WORD points[3] = {3, 2, 1};

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_RACE) return;

    for (i = 0; i < robotCount; i++) order[i] = i;
    for (pass = 0; pass < robotCount - 1; pass++) {
        for (i = 0; i < robotCount - 1 - pass; i++) {
            if (RaceProgressScore(order[i + 1]) > RaceProgressScore(order[i])) {
                WORD t = order[i];
                order[i] = order[i + 1];
                order[i + 1] = t;
            }
        }
    }

    miniGameWinner = order[0];
    for (i = 0; i < robotCount && i < 3; i++) {
        miniGamePoints[order[i]] = points[i];
        totalScores[order[i]] += points[i];
    }

    ClearMovementKeys();
    StopGameplaySamples();
    StopRoundStartSamples();
    gameState = GAME_MINIGAME_END;
    ForceGameplayFullPresent();
}


/* Anyone still standing outranks anyone eliminated; among the eliminated,
 * whoever lasted longer (a later elimination sequence number) ranks
 * higher. Score is just a tiebreaker within either group. */
static LONG BumperRank(WORD id)
{
    if (!bumperEliminated[id]) return 1000000L + robots[id].score;
    return ((LONG)bumperEliminatedSeq[id] * 1000L) + robots[id].score;
}


static void FinishBumperBots(void)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    WORD pass;
    static const WORD points[3] = {3, 2, 1};

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BUMPER) return;

    for (i = 0; i < robotCount; i++) order[i] = i;
    for (pass = 0; pass < robotCount - 1; pass++) {
        for (i = 0; i < robotCount - 1 - pass; i++) {
            if (BumperRank(order[i + 1]) > BumperRank(order[i])) {
                WORD t = order[i];
                order[i] = order[i + 1];
                order[i + 1] = t;
            }
        }
    }

    miniGameWinner = order[0];
    for (i = 0; i < robotCount && i < 3; i++) {
        miniGamePoints[order[i]] = points[i];
        totalScores[order[i]] += points[i];
    }

    ClearMovementKeys();
    StopGameplaySamples();
    StopRoundStartSamples();
    gameState = GAME_MINIGAME_END;
    ForceGameplayFullPresent();
}


static BOOL TryRaceBumpRobot(WORD blockedId, WORD dx, WORD dy)
{
    WORD pushX;
    WORD pushY;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_RACE) return FALSE;
    if (blockedId < 0 || blockedId >= robotCount || robots[blockedId].moving) return FALSE;
    if (racePlace[blockedId] >= 0) return FALSE;

    pushX = robots[blockedId].tileX + dx;
    pushY = robots[blockedId].tileY + dy;
    if (!RobotCanPassTile(blockedId, pushX, pushY)) return FALSE;
    if (RobotAtTile(pushX, pushY, blockedId)) return FALSE;

    robots[blockedId].tileX = pushX;
    robots[blockedId].tileY = pushY;
    robots[blockedId].targetX = pushX;
    robots[blockedId].targetY = pushY;
    robots[blockedId].px = TO_FP(pushX * TILE_SIZE);
    robots[blockedId].py = TO_FP(pushY * TILE_SIZE);
    robots[blockedId].targetPx = robots[blockedId].px;
    robots[blockedId].targetPy = robots[blockedId].py;
    robots[blockedId].moving = FALSE;
    robots[blockedId].stunTicks = RACE_BUMP_STUN_TICKS;
    robots[blockedId].boltStunned = FALSE;
    RaceHandleRobotArrival(blockedId);
    return TRUE;
}


static BOOL BumperTileInArena(WORD tx, WORD ty)
{
    return (tx >= BUMPER_ARENA_MIN_X && tx <= BUMPER_ARENA_MAX_X &&
            ty >= BUMPER_ARENA_MIN_Y && ty <= BUMPER_ARENA_MAX_Y) ? TRUE : FALSE;
}


/* Robot ids stay in bumperEliminated order 0..robotCount-1; a robot keeps
 * its last valid arena tile after going out (RobotAtTile/RobotIdAtTile
 * already skip eliminated ids), it just stops being drawn or steerable. */
static void BumperEliminateRobot(WORD id, WORD attackerId)
{
    if (id < 0 || id >= robotCount || bumperEliminated[id]) return;

    bumperEliminated[id] = TRUE;
    bumperEliminatedSeq[id] = bumperEliminationSeq++;
    bumperAliveCount--;
    robots[id].moving = FALSE;
    robots[id].stunTicks = 0;
    bumperFallTicks[id] = BUMPER_FALL_TICKS;
    bumperSliding[id] = FALSE;

    bumperEliminatedRobot = id;
    bumperEliminatedBy = (attackerId >= 0 && attackerId < robotCount && attackerId != id) ? attackerId : -1;
    bumperEliminatedFlashTicks = BUMPER_ELIM_FLASH_TICKS;

    if (bumperEliminatedBy >= 0) {
        robots[bumperEliminatedBy].score += BUMPER_ELIMINATION_POINTS;
        totalScores[bumperEliminatedBy] += BUMPER_ELIMINATION_POINTS;
        snprintf(lastPowerText, sizeof(lastPowerText), "%s KO'D %s", RobotTag(bumperEliminatedBy), RobotTag(id));
    } else {
        snprintf(lastPowerText, sizeof(lastPowerText), "%s FELL OFF", RobotTag(id));
    }
    lastPowerTicks = 80;

    MarkHudStatusTextDirty();
    ForceGameplayFullPresent();
}


/* Shoves blockedId up to `tiles` steps in the (dx,dy) direction - one tile
 * for a shoulder bump, several for a bolt hit. Sliding stops early if
 * another robot is in the way; running off the rug eliminates the target
 * instead of relocating it. Returns FALSE only when the very first step is
 * impossible, matching TryRaceBumpRobot's contract with StartRobotMove.
 *
 * The logical tile/px/py relocate instantly, same as the pre-existing
 * table/race-bump shoves - the mover's own StartRobotMove call advances
 * into the vacated tile in the same frame, so that tile must already read
 * as empty. Only the on-screen sprite eases into place afterward (see
 * bumperVisualPx/Py, stepped in StepBumperBots and drawn in DrawRobotBob)
 * so a push reads as a shove rather than a teleport. */
static BOOL BumperPushRobot(WORD blockedId, WORD dx, WORD dy, WORD tiles, WORD attackerId)
{
    WORD step;
    WORD pushX;
    WORD pushY;
    LONG fromPx;
    LONG fromPy;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BUMPER) return FALSE;
    if (blockedId < 0 || blockedId >= robotCount || bumperEliminated[blockedId]) return FALSE;
    if (robots[blockedId].moving || robots[blockedId].stunTicks > 0) return FALSE;
    if (dx == 0 && dy == 0) return FALSE;

    fromPx = robots[blockedId].px;
    fromPy = robots[blockedId].py;

    pushX = robots[blockedId].tileX + dx;
    pushY = robots[blockedId].tileY + dy;

    if (!BumperTileInArena(pushX, pushY)) {
        BumperEliminateRobot(blockedId, attackerId);
        return TRUE;
    }
    if (RobotAtTile(pushX, pushY, blockedId)) return FALSE;

    robots[blockedId].tileX = pushX;
    robots[blockedId].tileY = pushY;
    robots[blockedId].targetX = pushX;
    robots[blockedId].targetY = pushY;
    robots[blockedId].px = TO_FP(pushX * TILE_SIZE);
    robots[blockedId].py = TO_FP(pushY * TILE_SIZE);
    robots[blockedId].targetPx = robots[blockedId].px;
    robots[blockedId].targetPy = robots[blockedId].py;
    robots[blockedId].moving = FALSE;
    robots[blockedId].boltStunned = FALSE;
    if (dx < 0) SetRobotMoveSprite(blockedId, SPR_LEFT);
    else if (dx > 0) SetRobotMoveSprite(blockedId, SPR_RIGHT);
    else if (dy < 0) SetRobotMoveSprite(blockedId, SPR_UP);
    else if (dy > 0) SetRobotMoveSprite(blockedId, SPR_DOWN);

    for (step = 1; step < tiles; step++) {
        WORD nx = robots[blockedId].tileX + dx;
        WORD ny = robots[blockedId].tileY + dy;

        if (!BumperTileInArena(nx, ny)) {
            BumperEliminateRobot(blockedId, attackerId);
            return TRUE;
        }
        if (RobotAtTile(nx, ny, blockedId)) break;

        robots[blockedId].tileX = nx;
        robots[blockedId].tileY = ny;
        robots[blockedId].targetX = nx;
        robots[blockedId].targetY = ny;
        robots[blockedId].px = TO_FP(nx * TILE_SIZE);
        robots[blockedId].py = TO_FP(ny * TILE_SIZE);
        robots[blockedId].targetPx = robots[blockedId].px;
        robots[blockedId].targetPy = robots[blockedId].py;
    }

    /* Stun lasts at least as long as the visual slide takes to cover the
     * distance actually travelled (step tiles at BUMPER_SLIDE_SPEED each),
     * so a robot can't dart off under its own control while still sliding
     * from the last hit - a straight recovery beat rather than moves
     * overlapping mid-slide. */
    robots[blockedId].stunTicks = BUMPER_BUMP_STUN_TICKS * step;

    bumperVisualPx[blockedId] = fromPx;
    bumperVisualPy[blockedId] = fromPy;
    bumperSliding[blockedId] = TRUE;

    return TRUE;
}


static void LimitPuckVelocity(void)
{
    if (puckVx > PUCK_MAX_SPEED) puckVx = PUCK_MAX_SPEED;
    if (puckVx < -PUCK_MAX_SPEED) puckVx = -PUCK_MAX_SPEED;
    if (puckVy > PUCK_MAX_SPEED) puckVy = PUCK_MAX_SPEED;
    if (puckVy < -PUCK_MAX_SPEED) puckVy = -PUCK_MAX_SPEED;
}


static void ScorePuckGoal(WORD team)
{
    if (team < 0 || team > 1) return;
    puckTeamScore[team]++;
    puckScoringTeam = team;
    /* ResetPuckPosition() clears puckLastTouch, so grab the scorer first. */
    puckScoringRobot = puckLastTouch;
    puckGoalPauseTicks = PUCK_GOAL_PAUSE_TICKS;
    ResetPuckPosition();

    if (puckTeamScore[team] >= PUCK_GOALS_TO_WIN) FinishRoboPuck();
}


static void StepPuckPhysics(void)
{
    WORD x;
    WORD y;
    WORD goalTop = PUCK_GOAL_TOP_TILE * TILE_SIZE;
    WORD goalBottom = (PUCK_GOAL_BOTTOM_TILE + 1) * TILE_SIZE - PUCK_H;
    WORD minY = TILE_SIZE;
    WORD maxY = (MAP_H - 1) * TILE_SIZE - PUCK_H;
    WORD i;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PUCK) return;

    if (puckGoalPauseTicks > 0) {
        puckGoalPauseTicks--;
        if (puckGoalPauseTicks <= 0) {
            puckScoringTeam = -1;
            puckScoringRobot = -1;
        }
        return;
    }
    if (puckHitCooldownTicks > 0) puckHitCooldownTicks--;

    puckPx += puckVx;
    puckPy += puckVy;
    x = FP_TO_INT(puckPx);
    y = FP_TO_INT(puckPy);

    if (y < minY) {
        puckPy = TO_FP(minY);
        puckVy = -puckVy;
    } else if (y > maxY) {
        puckPy = TO_FP(maxY);
        puckVy = -puckVy;
    }

    x = FP_TO_INT(puckPx);
    y = FP_TO_INT(puckPy);
    if (y >= goalTop && y <= goalBottom) {
        if (x <= 0) {
            ScorePuckGoal(1);
            return;
        }
        if (x >= SCREEN_W - PUCK_W) {
            ScorePuckGoal(0);
            return;
        }
    } else {
        WORD minX = TILE_SIZE;
        WORD maxX = SCREEN_W - TILE_SIZE - PUCK_W;
        if (x < minX) {
            puckPx = TO_FP(minX);
            puckVx = -puckVx;
        } else if (x > maxX) {
            puckPx = TO_FP(maxX);
            puckVx = -puckVx;
        }
    }

    if (puckHitCooldownTicks <= 0) {
        x = FP_TO_INT(puckPx);
        y = FP_TO_INT(puckPy);
        for (i = 0; i < robotCount; i++) {
            WORD rx = FP_TO_INT(robots[i].px);
            WORD ry = FP_TO_INT(robots[i].py);
            WORD dirX = 0;
            WORD dirY = 0;
            WORD puckCenterX;
            WORD puckCenterY;
            WORD robotCenterX;
            WORD robotCenterY;

            if (!RectsOverlap(x, y, PUCK_W, PUCK_H, rx, ry, ROBOT_W, ROBOT_H)) continue;
            switch (robots[i].spriteIndex) {
                case SPR_LEFT:  dirX = -1; break;
                case SPR_RIGHT: dirX = 1; break;
                case SPR_UP:    dirY = -1; break;
                case SPR_DOWN:  dirY = 1; break;
                default: dirX = (PuckTeamForRobot(i) == 0) ? 1 : -1; break;
            }

            puckCenterX = x + PUCK_W / 2;
            puckCenterY = y + PUCK_H / 2;
            robotCenterX = rx + ROBOT_W / 2;
            robotCenterY = ry + ROBOT_H / 2;
            puckVx = dirX * PUCK_KICK_SPEED;
            puckVy = dirY * PUCK_KICK_SPEED;
            if (dirX != 0) puckVy += (puckCenterY - robotCenterY) * (FP_ONE / 3);
            if (dirY != 0) puckVx += (puckCenterX - robotCenterX) * (FP_ONE / 3);
            LimitPuckVelocity();
            puckPx += dirX * TO_FP(3);
            puckPy += dirY * TO_FP(3);
            puckHitCooldownTicks = PUCK_HIT_COOLDOWN_TICKS;
            puckLastTouch = i;
            break;
        }
    }

    /* Gentle rolling resistance lets a loose puck settle without making a
     * clean shot die before it reaches the far goal. */
    puckVx = (puckVx * 250L) / 256L;
    puckVy = (puckVy * 250L) / 256L;
    if (puckVx > -16 && puckVx < 16) puckVx = 0;
    if (puckVy > -16 && puckVy < 16) puckVy = 0;
}


static void FinishRoboPuck(void)
{
    WORD winningTeam;
    WORD i;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PUCK) return;
    winningTeam = (puckTeamScore[1] > puckTeamScore[0]) ? 1 : 0;
    miniGameWinner = -1;
    for (i = 0; i < robotCount; i++) {
        WORD points = (PuckTeamForRobot(i) == winningTeam) ? 3 : 1;
        miniGamePoints[i] = points;
        totalScores[i] += points;
        if (miniGameWinner < 0 && PuckTeamForRobot(i) == winningTeam) miniGameWinner = i;
    }
    if (miniGameWinner < 0) miniGameWinner = 0;

    ClearMovementKeys();
    StopGameplaySamples();
    StopRoundStartSamples();
    gameState = GAME_MINIGAME_END;
    ForceGameplayFullPresent();
}


static void StepRoboPuck(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PUCK) return;

    if (pauseMenuOpen) {
        BeginGameplayDirtyRects();
        ServiceHooverMoveSample();
        ForceGameplayFullPresent();
        FinishGameplayDirtyRects();
        return;
    }

    BeginGameplayDirtyRects();
    StepRoundStartSamples();
    if (roundCountdownTicks > 0) {
        countdownNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
        if (countdownNumber != roundCountdownLastSoundNumber) {
            PlayCountdownSample();
            roundCountdownLastSoundNumber = countdownNumber;
        }
        roundCountdownTicks--;
        ForceGameplayFullPresent();
        if (roundCountdownTicks > 0) {
            FinishGameplayDirtyRects();
            return;
        }
        roundGoTicks = ROUND_GO_FRAMES;
        if (!roundGoSoundPlayed) {
            PlayGoSample();
            roundGoSoundPlayed = TRUE;
        }
        StartMainGameMusic();
    }
    if (roundGoTicks > 0) roundGoTicks--;

    if (puckGoalPauseTicks <= 0 && puckTicksRemaining > 0) puckTicksRemaining--;
    StepPuckPhysics();
    if (gameState != GAME_MINIGAME_PLAYING) {
        FinishGameplayDirtyRects();
        return;
    }
    if (puckTicksRemaining <= 0 && puckTeamScore[0] != puckTeamScore[1]) {
        FinishRoboPuck();
        FinishGameplayDirtyRects();
        return;
    }

    if (!ShouldAdvanceGameplayFrame()) {
        ServiceHooverMoveSample();
        FinishGameplayDirtyRects();
        return;
    }

    for (i = 0; i < robotCount; i++) {
        StepRobotMovement(i);
        if (robots[i].turnTicks > 0) robots[i].turnTicks--;
    }
    for (i = 0; i < humanPlayers; i++) ChoosePlayerMove(i);
    for (i = humanPlayers; i < robotCount; i++) ChoosePuckAiMove(i);

    ServiceHooverMoveSample();
    FinishGameplayDirtyRects();
}


static void StepBumperBots(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BUMPER) return;

    if (pauseMenuOpen) {
        BeginGameplayDirtyRects();
        ServiceHooverMoveSample();
        ForceGameplayFullPresent();
        FinishGameplayDirtyRects();
        return;
    }

    BeginGameplayDirtyRects();
    StepRoundStartSamples();
    if (roundCountdownTicks > 0) {
        countdownNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
        if (countdownNumber != roundCountdownLastSoundNumber) {
            PlayCountdownSample();
            roundCountdownLastSoundNumber = countdownNumber;
        }
        roundCountdownTicks--;
        ForceGameplayFullPresent();
        if (roundCountdownTicks > 0) {
            FinishGameplayDirtyRects();
            return;
        }
        roundGoTicks = ROUND_GO_FRAMES;
        if (!roundGoSoundPlayed) {
            PlayGoSample();
            roundGoSoundPlayed = TRUE;
        }
        StartMainGameMusic();
    }
    if (roundGoTicks > 0) roundGoTicks--;

    if (bumperEliminatedFlashTicks > 0) bumperEliminatedFlashTicks--;
    if (bumperTicksRemaining > 0) bumperTicksRemaining--;

    for (i = 0; i < robotCount; i++) {
        if (bumperFallTicks[i] <= 0) continue;
        bumperFallTicks[i]--;
        /* The fall animation moves and blinks every tick regardless of the
         * gameplay speed setting; forcing a full present for its short
         * (<0.5s) lifetime is far cheaper than teaching the dirty-rect
         * tracker about a sinking/spinning sprite, the way Dirt Storm
         * would have needed before it got its own dirty-rect tracking. */
        ForceGameplayFullPresent();
    }

    for (i = 0; i < robotCount; i++) {
        LONG dxp;
        LONG dyp;

        if (!bumperSliding[i]) continue;

        dxp = robots[i].px - bumperVisualPx[i];
        dyp = robots[i].py - bumperVisualPy[i];
        if (dxp > BUMPER_SLIDE_SPEED) dxp = BUMPER_SLIDE_SPEED;
        else if (dxp < -BUMPER_SLIDE_SPEED) dxp = -BUMPER_SLIDE_SPEED;
        if (dyp > BUMPER_SLIDE_SPEED) dyp = BUMPER_SLIDE_SPEED;
        else if (dyp < -BUMPER_SLIDE_SPEED) dyp = -BUMPER_SLIDE_SPEED;
        bumperVisualPx[i] += dxp;
        bumperVisualPy[i] += dyp;
        if (bumperVisualPx[i] == robots[i].px && bumperVisualPy[i] == robots[i].py) {
            bumperSliding[i] = FALSE;
        }
        /* Same reasoning as the fall animation above: a shove is brief and
         * infrequent enough that forcing a full present is simpler and
         * cheaper than dirty-rect-tracking a sprite that moves independently
         * of robots[i].px/py. */
        ForceGameplayFullPresent();
    }

    if (bumperTicksRemaining <= 0 || bumperAliveCount <= 1) {
        FinishBumperBots();
        FinishGameplayDirtyRects();
        return;
    }

    if (!ShouldAdvanceGameplayFrame()) {
        ServiceHooverMoveSample();
        FinishGameplayDirtyRects();
        return;
    }

    for (i = 0; i < robotCount; i++) {
        if (bumperEliminated[i]) continue;
        StepRobotMovement(i);
        if (robots[i].turnTicks > 0) robots[i].turnTicks--;
        if (robots[i].stunTicks > 0) robots[i].stunTicks--;
    }

    StepPlayerBolts();
    StepBumperAiFire();

    for (i = 0; i < humanPlayers; i++) {
        if (!bumperEliminated[i]) ChoosePlayerMove(i);
    }
    for (i = humanPlayers; i < robotCount; i++) {
        if (!bumperEliminated[i]) ChooseBumperAiMove(i);
    }

    ServiceHooverMoveSample();
    if (bumperAliveCount <= 1) {
        FinishBumperBots();
    }
    FinishGameplayDirtyRects();
}


static void RestartCurrentMiniGame(void)
{
    if (miniGameType == MINIGAME_PUCK) StartRoboPuck();
    else if (miniGameType == MINIGAME_BUMPER) StartBumperBots();
    else StartRoboRace();
}


static void StepRoboRace(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_RACE) return;

    if (pauseMenuOpen) {
        BeginGameplayDirtyRects();
        ServiceHooverMoveSample();
        ForceGameplayFullPresent();
        FinishGameplayDirtyRects();
        return;
    }

    BeginGameplayDirtyRects();
    StepRoundStartSamples();

    if (roundCountdownTicks > 0) {
        countdownNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
        if (countdownNumber != roundCountdownLastSoundNumber) {
            PlayCountdownSample();
            roundCountdownLastSoundNumber = countdownNumber;
        }

        roundCountdownTicks--;
        ForceGameplayFullPresent();
        if (roundCountdownTicks > 0) {
            FinishGameplayDirtyRects();
            return;
        }

        roundGoTicks = ROUND_GO_FRAMES;
        if (!roundGoSoundPlayed) {
            PlayGoSample();
            roundGoSoundPlayed = TRUE;
        }
        StartMainGameMusic();
    }
    if (roundGoTicks > 0) roundGoTicks--;

    /* The clock is real PAL time even when the optional slower gameplay
     * speed skips a movement tick. */
    if (raceTicksRemaining > 0) raceTicksRemaining--;
    if (raceFinishGraceTicks > 0) raceFinishGraceTicks--;

    if (!ShouldAdvanceGameplayFrame()) {
        ServiceHooverMoveSample();
        if (raceTicksRemaining <= 0 ||
            (raceFinishCount > 0 && raceFinishGraceTicks <= 0)) {
            FinishRoboRace();
        }
        FinishGameplayDirtyRects();
        return;
    }

    for (i = 0; i < robotCount; i++) {
        StepRobotMovement(i);
        if (speedFlashTicks[i] > 0) speedFlashTicks[i]--;
        if (robots[i].turnTicks > 0) robots[i].turnTicks--;
        if (robots[i].stunTicks > 0) robots[i].stunTicks--;
    }

    for (i = 0; i < humanPlayers; i++) ChoosePlayerMove(i);
    for (i = humanPlayers; i < robotCount; i++) ChooseRaceAiMove(i);

    ServiceHooverMoveSample();
    if (raceFinishCount >= robotCount || raceTicksRemaining <= 0 ||
        (raceFinishCount > 0 && raceFinishGraceTicks <= 0)) {
        FinishRoboRace();
    }
    FinishGameplayDirtyRects();
}