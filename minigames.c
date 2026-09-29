#include "robovac.h"


static WORD ChooseNextMiniGame(void)
{
    WORD choice;

    /* Party Mode plays every type exactly once, in the shuffled order
     * StartPartyMode already built, rather than the normal random pick. */
    if (partyModeActive) {
        if (partyModeQueueIndex >= MINIGAME_COUNT) partyModeQueueIndex = 0;
        return partyModeQueue[partyModeQueueIndex++];
    }

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

    /* One oil slick per leg of the loop, reusing the otherwise-unused
     * TILE_OBSTACLE art. Driving onto one (see the RACE_SLICK_SPEED check
     * in StepRobotMovement) slows that move down instead of blocking it,
     * same spirit as dirt in the normal cleaning game being something to
     * drive through rather than around. */
    map[2][9] = TILE_OBSTACLE;
    map[6][16] = TILE_OBSTACLE;
    map[11][12] = TILE_OBSTACLE;
    map[6][2] = TILE_OBSTACLE;

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


static WORD AirHockeyTeamForRobot(WORD id)
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


static void ResetAirHockeyPuckPosition(void)
{
    airhockeyPuckPx = TO_FP((SCREEN_W - AIRHOCKEY_W) / 2);
    airhockeyPuckPy = TO_FP(((MAP_H * TILE_SIZE) - AIRHOCKEY_H) / 2);
    airhockeyPuckVx = 0;
    airhockeyPuckVy = 0;
    airhockeyHitCooldownTicks = 0;
    airhockeyLastTouch = -1;
}


static void StartAirHockey(void)
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

    /* The marker tiles are open goal mouths, not obstacles.  Team 0 defends
     * the top goal and attacks downward; Team 1 mirrors it from the bottom. */
    for (x = AIRHOCKEY_GOAL_LEFT_TILE; x <= AIRHOCKEY_GOAL_RIGHT_TILE; x++) {
        map[0][x] = TILE_MARKER;
        map[MAP_H - 1][x] = TILE_MARKER;
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
        WORD team = AirHockeyTeamForRobot(i);
        WORD slot = i >> 1;
        WORD startX = 3 + ((slot * 2) % 13);

        SetRobotTile(i, startX, team == 0 ? 3 : MAP_H - 4);
        robots[i].spriteVariant = variant;
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        robots[i].spriteIndex = team == 0 ? SPR_DOWN : SPR_UP;
        robots[i].prevSpriteIndex = robots[i].spriteIndex;
        robots[i].turnTicks = 0;
        raceBoostMoves[i] = 0;
        speedFlashTicks[i] = 0;
        airhockeyBoostCooldown[i] = 0;
        aiPrevTileX[i] = robots[i].tileX;
        aiPrevTileY[i] = robots[i].tileY;
    }

    airhockeyTeamScore[0] = 0;
    airhockeyTeamScore[1] = 0;
    airhockeyTicksRemaining = AIRHOCKEY_TIME_TICKS;
    airhockeyGoalPauseTicks = 0;
    airhockeyScoringTeam = -1;
    airhockeyScoringRobot = -1;
    airhockeyBoostFlashTicks = 0;
    ResetAirHockeyPuckPosition();
#if USE_DIRTY_RECTS
    dirtyPrevAirhockeyPuckValid = FALSE;
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


static void LimitAirHockeyVelocity(void)
{
    if (airhockeyPuckVx > AIRHOCKEY_MAX_SPEED) airhockeyPuckVx = AIRHOCKEY_MAX_SPEED;
    if (airhockeyPuckVx < -AIRHOCKEY_MAX_SPEED) airhockeyPuckVx = -AIRHOCKEY_MAX_SPEED;
    if (airhockeyPuckVy > AIRHOCKEY_MAX_SPEED) airhockeyPuckVy = AIRHOCKEY_MAX_SPEED;
    if (airhockeyPuckVy < -AIRHOCKEY_MAX_SPEED) airhockeyPuckVy = -AIRHOCKEY_MAX_SPEED;
}


static void ScoreAirHockeyGoal(WORD team)
{
    if (team < 0 || team > 1) return;
    airhockeyTeamScore[team]++;
    airhockeyScoringTeam = team;
    /* ResetAirHockeyPuckPosition() clears airhockeyLastTouch, so grab the
     * scorer first. */
    airhockeyScoringRobot = airhockeyLastTouch;
    airhockeyGoalPauseTicks = AIRHOCKEY_GOAL_PAUSE_TICKS;
    PlayGoalSample();
    ResetAirHockeyPuckPosition();

    if (airhockeyTeamScore[team] >= AIRHOCKEY_GOALS_TO_WIN) FinishAirHockey();
}


static void StepAirHockeyPhysics(void)
{
    WORD x;
    WORD y;
    WORD goalLeft = AIRHOCKEY_GOAL_LEFT_TILE * TILE_SIZE;
    WORD goalRight = (AIRHOCKEY_GOAL_RIGHT_TILE + 1) * TILE_SIZE - AIRHOCKEY_W;
    WORD minX = TILE_SIZE;
    WORD maxX = (MAP_W - 1) * TILE_SIZE - AIRHOCKEY_W;
    WORD i;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return;

    if (airhockeyGoalPauseTicks > 0) {
        airhockeyGoalPauseTicks--;
        if (airhockeyGoalPauseTicks <= 0) {
            airhockeyScoringTeam = -1;
            airhockeyScoringRobot = -1;
        }
        return;
    }
    if (airhockeyHitCooldownTicks > 0) airhockeyHitCooldownTicks--;

    airhockeyPuckPx += airhockeyPuckVx;
    airhockeyPuckPy += airhockeyPuckVy;
    x = FP_TO_INT(airhockeyPuckPx);
    y = FP_TO_INT(airhockeyPuckPy);

    if (x < minX) {
        airhockeyPuckPx = TO_FP(minX);
        airhockeyPuckVx = -airhockeyPuckVx;
    } else if (x > maxX) {
        airhockeyPuckPx = TO_FP(maxX);
        airhockeyPuckVx = -airhockeyPuckVx;
    }

    x = FP_TO_INT(airhockeyPuckPx);
    y = FP_TO_INT(airhockeyPuckPy);
    if (x >= goalLeft && x <= goalRight) {
        if (y <= 0) {
            ScoreAirHockeyGoal(1);
            return;
        }
        if (y >= (MAP_H * TILE_SIZE) - AIRHOCKEY_H) {
            ScoreAirHockeyGoal(0);
            return;
        }
    } else {
        WORD minY = TILE_SIZE;
        WORD maxY = (MAP_H - 1) * TILE_SIZE - AIRHOCKEY_H;
        if (y < minY) {
            airhockeyPuckPy = TO_FP(minY);
            airhockeyPuckVy = -airhockeyPuckVy;
        } else if (y > maxY) {
            airhockeyPuckPy = TO_FP(maxY);
            airhockeyPuckVy = -airhockeyPuckVy;
        }
    }

    if (airhockeyHitCooldownTicks <= 0) {
        x = FP_TO_INT(airhockeyPuckPx);
        y = FP_TO_INT(airhockeyPuckPy);
        for (i = 0; i < robotCount; i++) {
            WORD rx = FP_TO_INT(robots[i].px);
            WORD ry = FP_TO_INT(robots[i].py);
            WORD dirX = 0;
            WORD dirY = 0;
            WORD puckCenterX;
            WORD puckCenterY;
            WORD robotCenterX;
            WORD robotCenterY;

            if (!RectsOverlap(x, y, AIRHOCKEY_W, AIRHOCKEY_H, rx, ry, ROBOT_W, ROBOT_H)) continue;

            puckCenterX = x + AIRHOCKEY_W / 2;
            puckCenterY = y + AIRHOCKEY_H / 2;
            robotCenterX = rx + ROBOT_W / 2;
            robotCenterY = ry + ROBOT_H / 2;

            switch (robots[i].spriteIndex) {
                case SPR_LEFT:  dirX = -1; break;
                case SPR_RIGHT: dirX = 1; break;
                case SPR_UP:    dirY = -1; break;
                case SPR_DOWN:  dirY = 1; break;
                default:
                    /* A robot that is not actively facing a direction (idle,
                     * or parked as a "shield") must still send the puck away
                     * from where it actually is, not toward a fixed team
                     * direction that might point straight into a wall a
                     * stationary defender is backed up against. */
                    if (AbsW(puckCenterX - robotCenterX) >= AbsW(puckCenterY - robotCenterY)) {
                        dirX = (puckCenterX < robotCenterX) ? -1 : 1;
                    } else {
                        dirY = (puckCenterY < robotCenterY) ? -1 : 1;
                    }
                    break;
            }

            airhockeyPuckVx = dirX * AIRHOCKEY_KICK_SPEED;
            airhockeyPuckVy = dirY * AIRHOCKEY_KICK_SPEED;
            if (dirX != 0) airhockeyPuckVy += (puckCenterY - robotCenterY) * (FP_ONE / 3);
            if (dirY != 0) airhockeyPuckVx += (puckCenterX - robotCenterX) * (FP_ONE / 3);
            LimitAirHockeyVelocity();
            /* Clear the robot's whole hitbox, not just a few pixels - see
             * the identical fix and reasoning in StepPuckPhysics. */
            airhockeyPuckPx += dirX * TO_FP(ROBOT_W);
            airhockeyPuckPy += dirY * TO_FP(ROBOT_H);
            airhockeyHitCooldownTicks = AIRHOCKEY_HIT_COOLDOWN_TICKS;
            airhockeyLastTouch = i;
            break;
        }
    }

    /* Gentle rolling resistance lets a loose puck settle without making a
     * clean shot die before it reaches the far goal. */
    airhockeyPuckVx = (airhockeyPuckVx * 250L) / 256L;
    airhockeyPuckVy = (airhockeyPuckVy * 250L) / 256L;
    if (airhockeyPuckVx > -16 && airhockeyPuckVx < 16) airhockeyPuckVx = 0;
    if (airhockeyPuckVy > -16 && airhockeyPuckVy < 16) airhockeyPuckVy = 0;
}


static BOOL TryAirHockeyBoost(WORD id)
{
    WORD team;
    WORD dirX = 0;
    WORD dirY = 0;
    WORD puckCenterX;
    WORD puckCenterY;
    WORD robotCenterX;
    WORD robotCenterY;
    WORD dtx;
    WORD dty;
    WORD i;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return FALSE;
    if (id < 0 || id >= robotCount) return FALSE;
    if (airhockeyGoalPauseTicks > 0) return FALSE;
    if (airhockeyBoostCooldown[id] > 0) return FALSE;

    dtx = AbsW(robots[id].tileX - (FP_TO_INT(airhockeyPuckPx + TO_FP(AIRHOCKEY_W / 2)) / TILE_SIZE));
    dty = AbsW(robots[id].tileY - (FP_TO_INT(airhockeyPuckPy + TO_FP(AIRHOCKEY_H / 2)) / TILE_SIZE));
    if (dtx > AIRHOCKEY_BOOST_RANGE || dty > AIRHOCKEY_BOOST_RANGE) return FALSE;

    team = AirHockeyTeamForRobot(id);
    switch (robots[id].spriteIndex) {
        case SPR_LEFT:  dirX = -1; break;
        case SPR_RIGHT: dirX = 1; break;
        case SPR_UP:    dirY = -1; break;
        case SPR_DOWN:  dirY = 1; break;
        default: dirY = (team == 0) ? 1 : -1; break;
    }

    puckCenterX = FP_TO_INT(airhockeyPuckPx) + AIRHOCKEY_W / 2;
    puckCenterY = FP_TO_INT(airhockeyPuckPy) + AIRHOCKEY_H / 2;
    robotCenterX = FP_TO_INT(robots[id].px) + ROBOT_W / 2;
    robotCenterY = FP_TO_INT(robots[id].py) + ROBOT_H / 2;

    airhockeyPuckVx = dirX * AIRHOCKEY_BOOST_SPEED;
    airhockeyPuckVy = dirY * AIRHOCKEY_BOOST_SPEED;
    if (dirX != 0) airhockeyPuckVy += (puckCenterY - robotCenterY) * (FP_ONE / 3);
    if (dirY != 0) airhockeyPuckVx += (puckCenterX - robotCenterX) * (FP_ONE / 3);
    LimitAirHockeyVelocity();
    airhockeyHitCooldownTicks = AIRHOCKEY_HIT_COOLDOWN_TICKS;
    airhockeyLastTouch = id;
    airhockeyBoostCooldown[id] = AIRHOCKEY_BOOST_COOLDOWN_TICKS;
    airhockeyBoostFlashTicks = AIRHOCKEY_BOOST_FLASH_TICKS;

    /* The power shot doubles as a short-range EMP jolt: any rival caught
     * next to the boosting robot is stunned rather than left free to just
     * shove the puck straight back. */
    for (i = 0; i < robotCount; i++) {
        WORD orx;
        WORD ory;
        if (i == id || AirHockeyTeamForRobot(i) == team) continue;
        orx = AbsW(robots[i].tileX - robots[id].tileX);
        ory = AbsW(robots[i].tileY - robots[id].tileY);
        if (orx <= AIRHOCKEY_BOOST_RANGE && ory <= AIRHOCKEY_BOOST_RANGE &&
            robots[i].stunTicks < AIRHOCKEY_BOOST_STUN_TICKS) {
            robots[i].stunTicks = AIRHOCKEY_BOOST_STUN_TICKS;
        }
    }

    return TRUE;
}


static void FinishAirHockey(void)
{
    WORD winningTeam;
    WORD i;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return;
    winningTeam = (airhockeyTeamScore[1] > airhockeyTeamScore[0]) ? 1 : 0;
    miniGameWinner = -1;
    for (i = 0; i < robotCount; i++) {
        WORD points = (AirHockeyTeamForRobot(i) == winningTeam) ? 3 : 1;
        miniGamePoints[i] = points;
        totalScores[i] += points;
        if (miniGameWinner < 0 && AirHockeyTeamForRobot(i) == winningTeam) miniGameWinner = i;
    }
    if (miniGameWinner < 0) miniGameWinner = 0;

    ClearMovementKeys();
    StopGameplaySamples();
    StopRoundStartSamples();
    gameState = GAME_MINIGAME_END;
    ForceGameplayFullPresent();
}


static void StepAirHockey(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_AIRHOCKEY) return;

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

    if (airhockeyGoalPauseTicks <= 0 && airhockeyTicksRemaining > 0) airhockeyTicksRemaining--;
    if (airhockeyBoostFlashTicks > 0) airhockeyBoostFlashTicks--;
    for (i = 0; i < robotCount; i++) {
        if (airhockeyBoostCooldown[i] > 0) airhockeyBoostCooldown[i]--;
    }

    StepAirHockeyPhysics();
    if (gameState != GAME_MINIGAME_PLAYING) {
        FinishGameplayDirtyRects();
        return;
    }
    if (airhockeyTicksRemaining <= 0 && airhockeyTeamScore[0] != airhockeyTeamScore[1]) {
        FinishAirHockey();
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
        if (robots[i].stunTicks > 0) robots[i].stunTicks--;
    }
    for (i = 0; i < humanPlayers; i++) ChoosePlayerMove(i);
    for (i = humanPlayers; i < robotCount; i++) ChooseAirHockeyAiMove(i);

    ServiceHooverMoveSample();
    FinishGameplayDirtyRects();
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
    /* Counts total tiles actually moved, for the post-push stun length below.
     * The code right after this declaration already performs the first
     * tile unconditionally, so a plain 1-tile shoulder bump (tiles == 1)
     * never enters the "for (step = 1; step < tiles; ...)" loop below at
     * all - step must start at 1 to reflect that already-applied first
     * tile, or the stun length read after the loop is whatever was left on
     * the stack. That was silently handing bumped robots a near-random
     * (often zero) cooldown, so the last two survivors could ping-pong
     * bumps into each other every tick with no recovery beat between them. */
    WORD step = 1;
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
        /* Relocate into the abyss tile itself before eliminating, so the
         * fall/tumble animation (which draws at robots[id].px/py) plays
         * where the robot actually landed, off the rug - not back on the
         * last valid tile it was shoved from. */
        robots[blockedId].tileX = pushX;
        robots[blockedId].tileY = pushY;
        robots[blockedId].px = TO_FP(pushX * TILE_SIZE);
        robots[blockedId].py = TO_FP(pushY * TILE_SIZE);
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
            robots[blockedId].tileX = nx;
            robots[blockedId].tileY = ny;
            robots[blockedId].px = TO_FP(nx * TILE_SIZE);
            robots[blockedId].py = TO_FP(ny * TILE_SIZE);
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
    PlayGoalSample();
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

            puckCenterX = x + PUCK_W / 2;
            puckCenterY = y + PUCK_H / 2;
            robotCenterX = rx + ROBOT_W / 2;
            robotCenterY = ry + ROBOT_H / 2;

            switch (robots[i].spriteIndex) {
                case SPR_LEFT:  dirX = -1; break;
                case SPR_RIGHT: dirX = 1; break;
                case SPR_UP:    dirY = -1; break;
                case SPR_DOWN:  dirY = 1; break;
                default:
                    /* A robot that is not actively facing a direction (idle,
                     * or just parked as a "shield") must still send the puck
                     * away from where it actually is, not toward a fixed
                     * team direction that might point straight into a wall
                     * a stationary defender is backed up against. */
                    if (AbsW(puckCenterX - robotCenterX) >= AbsW(puckCenterY - robotCenterY)) {
                        dirX = (puckCenterX < robotCenterX) ? -1 : 1;
                    } else {
                        dirY = (puckCenterY < robotCenterY) ? -1 : 1;
                    }
                    break;
            }

            puckVx = dirX * PUCK_KICK_SPEED;
            puckVy = dirY * PUCK_KICK_SPEED;
            if (dirX != 0) puckVy += (puckCenterY - robotCenterY) * (FP_ONE / 3);
            if (dirY != 0) puckVx += (puckCenterX - robotCenterX) * (FP_ONE / 3);
            LimitPuckVelocity();
            /* Clear the robot's whole hitbox, not just a few pixels - a
             * small nudge can still leave the puck overlapping, and an
             * instant wall bounce would drop it right back for a repeat
             * hit, which is how a robot could "shield" the puck in place. */
            puckPx += dirX * TO_FP(ROBOT_W);
            puckPy += dirY * TO_FP(ROBOT_H);
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


static void StartRoboBowling(void)
{
    /* Each robot gets its own lane, walled off from its neighbours, with a
     * compact 2-wide x 5-tall rack of ten pins at the far end - see the
     * BOWLING_ comment block in robovac.h for why (a shared rack meant
     * whoever was already closest got every pin first, every time). Lanes
     * are spaced evenly across the room for however many robots are
     * playing; the common case of up to four gets a full wall divider
     * between every lane, the same "optimise for the common case, degrade
     * gracefully at the edges" tradeoff RoboRace's spawn layout makes. */
    WORD x;
    WORD y;
    WORD i;
    WORD laneSpacing;

    StopGameplaySamples();
    StopRoundStartSamples();
    StopEmpPaletteCycle();
    StopNightMode();
    ClearMovementKeys();
    ClosePauseMenu();

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            map[y][x] = (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) ? TILE_WALL : TILE_FLOOR;
        }
    }

    laneSpacing = (MAP_W - 4) / (robotCount > 0 ? robotCount : 1);
    if (laneSpacing < 2) laneSpacing = 2;
    if (laneSpacing > BOWLING_LANE_WIDTH) laneSpacing = BOWLING_LANE_WIDTH;

    for (i = 0; i < robotCount; i++) {
        WORD baseX = 1 + i * laneSpacing;
        WORD row;
        WORD col;
        WORD pin;

        {
            WORD maxBaseX = MAP_W - 1 - BOWLING_PIN_COLS;
            if (baseX > maxBaseX) baseX = maxBaseX;
            /* At the very edge of the room, that clamp alone could pull this
             * lane back onto exactly the same columns as the previous one
             * (two robots' whole racks, and spawn tiles, landing on top of
             * each other) instead of just crowding closer - nudge forward
             * one more column so lanes stay distinct, but re-clamp so the
             * nudge itself can never push a pin past the right-hand wall. */
            if (i > 0 && baseX <= bowlingLaneBaseX[i - 1]) {
                baseX = bowlingLaneBaseX[i - 1] + 1;
                if (baseX > maxBaseX) baseX = maxBaseX;
            }
        }
        bowlingLaneBaseX[i] = baseX;

        /* A one-tile wall divider between this lane and the last one, as
         * long as there is actually a gap to put it in - with more robots
         * than the room comfortably fits, lanes compress and dividers are
         * skipped rather than overlapping a neighbour's pins. */
        if (i > 0) {
            WORD dividerX = baseX - 1;
            WORD prevRightEdge = bowlingLaneBaseX[i - 1] + BOWLING_PIN_COLS - 1;
            if (dividerX > prevRightEdge && dividerX > 0 && dividerX < MAP_W - 1) {
                for (y = 1; y < MAP_H - 1; y++) map[y][dividerX] = TILE_WALL;
            }
        }

        pin = 0;
        for (row = 0; row < BOWLING_PIN_ROWS && pin < BOWLING_PIN_COUNT; row++) {
            for (col = 0; col < BOWLING_PIN_COLS && pin < BOWLING_PIN_COUNT; col++) {
                WORD px = baseX + col;
                WORD py = BOWLING_PIN_BASE_Y + row;
                bowlingPinX[i][pin] = px;
                bowlingPinY[i][pin] = py;
                map[py][px] = TILE_TABLE;
                pin++;
            }
        }
        bowlingPinsStanding[i] = BOWLING_PIN_COUNT;
        bowlingBallsThisFrame[i] = 0;
        bowlingBallInFlight[i] = FALSE;
        bowlingScore[i] = 0;

        bowlingLaunchX[i] = baseX;
        bowlingLaunchY[i] = (BOWLING_LAUNCH_ROW < MAP_H - 1) ? BOWLING_LAUNCH_ROW : MAP_H - 2;
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

        SetRobotTile(i, bowlingLaunchX[i], bowlingLaunchY[i]);
        robots[i].spriteVariant = variant;
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        SetRobotMoveSprite(i, SPR_UP);
        /* playerFacingX/Y are sized for human players only - guard like
         * StartRobotMove's own facing update does, since i ranges over every
         * robot here, AI included. */
        if (i < humanPlayers) {
            playerFacingX[i] = 0;
            playerFacingY[i] = -1;
        }
        aiPrevTileX[i] = robots[i].tileX;
        aiPrevTileY[i] = robots[i].tileY;
    }

    bowlingTicksRemaining = BOWLING_TIME_TICKS;
    bowlingLastKnockedRobot = -1;
    bowlingFlashTicks = 0;

    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    MarkHudStatusTextDirty();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


static BOOL TryKnockdownPin(WORD id, WORD tx, WORD ty)
{
    if (id < 0 || id >= robotCount) return FALSE;
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return FALSE;
    if (map[ty][tx] != TILE_TABLE) return FALSE;

    map[ty][tx] = TILE_FLOOR;
    UpdateRoomTile(tx, ty);
    if (bowlingPinsStanding[id] > 0) bowlingPinsStanding[id]--;
    bowlingScore[id]++;
    bowlingLastKnockedRobot = id;
    bowlingFlashTicks = BOWLING_FLASH_TICKS;
    MarkHudStatusTextDirty();
    return TRUE;
}


/* Restores a robot's own rack to ten fresh standing pins, the way a real
 * lane resets between frames. Called from ResolveBowlingThrow once the
 * current frame is over. */
static void ResetBowlingLane(WORD id)
{
    WORD pin;

    if (id < 0 || id >= robotCount) return;
    for (pin = 0; pin < BOWLING_PIN_COUNT; pin++) {
        WORD px = bowlingPinX[id][pin];
        WORD py = bowlingPinY[id][pin];
        if (map[py][px] != TILE_TABLE) {
            map[py][px] = TILE_TABLE;
            UpdateRoomTile(px, py);
        }
    }
    bowlingPinsStanding[id] = BOWLING_PIN_COUNT;
}


/* Called once a robot's ball has left play (see the bowlingBallInFlight
 * edge-check in StepRoboBowling) to decide what happens next: a strike, or
 * an already-thrown second ball, resets the lane for a fresh frame;
 * otherwise the robot gets the frame's second throw at whatever pins are
 * still standing - "another throw... like proper bowling". */
static void ResolveBowlingThrow(WORD id)
{
    WORD ballNumber;

    if (id < 0 || id >= robotCount) return;
    ballNumber = bowlingBallsThisFrame[id] + 1;

    if (bowlingPinsStanding[id] <= 0 || ballNumber >= 2) {
        ResetBowlingLane(id);
        bowlingBallsThisFrame[id] = 0;
    } else {
        bowlingBallsThisFrame[id] = ballNumber;
    }
}


static void FinishRoboBowling(void)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    WORD pass;
    static const WORD points[3] = {3, 2, 1};

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BOWLING) return;

    for (i = 0; i < robotCount; i++) order[i] = i;
    for (pass = 0; pass < robotCount - 1; pass++) {
        for (i = 0; i < robotCount - 1 - pass; i++) {
            if (bowlingScore[order[i + 1]] > bowlingScore[order[i]]) {
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


static void StepRoboBowling(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_BOWLING) return;

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

    if (bowlingFlashTicks > 0) bowlingFlashTicks--;
    if (bowlingTicksRemaining > 0) bowlingTicksRemaining--;

    if (bowlingTicksRemaining <= 0) {
        FinishRoboBowling();
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

    StepPlayerBolts();

    /* A ball's flight ends inside StepPlayerBolt (hitting the wall at the
     * lane's far end, or its ttl running out) with no single call site to
     * hook a "throw is over" event into, so detect it here instead: whichever
     * robots had a bolt in flight last tick but don't any more just had a
     * throw resolve. */
    for (i = 0; i < robotCount; i++) {
        if (bowlingBallInFlight[i] && !playerBolts[i].active) ResolveBowlingThrow(i);
        bowlingBallInFlight[i] = playerBolts[i].active;
    }

    for (i = 0; i < humanPlayers; i++) ChoosePlayerMove(i);
    for (i = humanPlayers; i < robotCount; i++) ChooseBowlingAiMove(i);

    ServiceHooverMoveSample();
    FinishGameplayDirtyRects();
}


static void ScatterFloodBlocks(void)
{
    WORD placed = 0;
    WORD attempts = 0;
    WORD target = robotCount * FLOODHOUSE_LOOSE_PER_ROBOT;

    while (placed < target && attempts < 500) {
        WORD tx = 1 + (WORD)RandRange(MAP_W - 2);
        WORD ty = 1 + (WORD)RandRange(MAP_H - 2);
        WORD i;
        BOOL nearHome = FALSE;

        attempts++;
        if (floodBlockHeight[ty][tx] > 0) continue;
        for (i = 0; i < robotCount; i++) {
            if (AbsW(tx - floodHomeX[i]) + AbsW(ty - floodHomeY[i]) <= 1) {
                nearHome = TRUE;
                break;
            }
        }
        if (nearHome) continue;

        floodBlockHeight[ty][tx] = 1;
        placed++;
    }
    floodLooseRemaining = placed;
}


static void StartFloodHouse(void)
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
            floodBlockHeight[y][x] = 0;
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
    for (i = 0; i < robotCount; i++) {
        UBYTE variant = robots[i].spriteVariant;

        SetRobotTile(i, floodHomeX[i], floodHomeY[i]);
        robots[i].spriteVariant = variant;
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        floodCarried[i] = 0;
        floodSurrounded[i] = FALSE;
        aiPrevTileX[i] = robots[i].tileX;
        aiPrevTileY[i] = robots[i].tileY;
    }

    ScatterFloodBlocks();

    floodTicksRemaining = FLOODHOUSE_BUILD_TICKS;
    floodPauseTicks = 0;
    floodFlashTicks = 0;
    floodLastEventRobot = -1;
    floodLastEventKind = FLOODHOUSE_EVENT_NONE;
    floodPaletteTicks = 0;

    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    MarkHudStatusTextDirty();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


static void FloodHandleRobotArrival(WORD id)
{
    WORD tx;
    WORD ty;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_FLOODHOUSE) return;
    if (id < 0 || id >= robotCount) return;
    if (floodCarried[id] >= FLOODHOUSE_CARRY_MAX) return;

    tx = robots[id].tileX;
    ty = robots[id].tileY;
    if (floodBlockHeight[ty][tx] <= 0) return;

    floodBlockHeight[ty][tx]--;
    floodCarried[id]++;
    UpdateRoomTile(tx, ty);
    floodFlashTicks = FLOODHOUSE_FLASH_TICKS;
    floodLastEventRobot = id;
    floodLastEventKind = FLOODHOUSE_EVENT_STOLE;
    MarkHudStatusTextDirty();
}


static void TryFloodRaidRobot(WORD blockedId, WORD attackerId)
{
    if (blockedId < 0 || blockedId >= robotCount) return;
    if (attackerId < 0 || attackerId >= robotCount) return;
    if (floodCarried[blockedId] <= 0) return;

    floodCarried[blockedId]--;
    if (floodCarried[attackerId] < FLOODHOUSE_CARRY_MAX) floodCarried[attackerId]++;
    floodFlashTicks = FLOODHOUSE_FLASH_TICKS;
    floodLastEventRobot = attackerId;
    floodLastEventKind = FLOODHOUSE_EVENT_RAIDED;
    MarkHudStatusTextDirty();
}


static BOOL TryFloodBuild(WORD id)
{
    WORD dirX = 0;
    WORD dirY = 0;
    WORD tx;
    WORD ty;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_FLOODHOUSE) return FALSE;
    if (id < 0 || id >= robotCount) return FALSE;
    if (floodCarried[id] <= 0) return FALSE;

    switch (robots[id].spriteIndex) {
        case SPR_LEFT:  dirX = -1; break;
        case SPR_RIGHT: dirX = 1; break;
        case SPR_UP:    dirY = -1; break;
        case SPR_DOWN:  dirY = 1; break;
        default: dirY = 1; break;
    }

    tx = robots[id].tileX + dirX;
    ty = robots[id].tileY + dirY;
    /* tx/ty in range only guards the array read below - the outer wall ring
     * (x==0, y==0, x==MAP_W-1, y==MAP_H-1) is well inside those bounds, so
     * without the floor check a robot facing the boundary could drop a
     * block into the wall itself: it would still bake into the room buffer
     * and read as a block floating outside the playable floor. */
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return FALSE;
    if (map[ty][tx] != TILE_FLOOR) return FALSE;
    if (floodBlockHeight[ty][tx] >= FLOODHOUSE_STACK_MAX) return FALSE;
    if (RobotAtTile(tx, ty, id)) return FALSE;

    floodBlockHeight[ty][tx]++;
    floodCarried[id]--;
    UpdateRoomTile(tx, ty);
    floodFlashTicks = FLOODHOUSE_FLASH_TICKS;
    floodLastEventRobot = id;
    floodLastEventKind = FLOODHOUSE_EVENT_BUILT;
    MarkHudStatusTextDirty();
    return TRUE;
}


static WORD FloodHomeWallCount(WORD id)
{
    static const WORD dirX[4] = {1, 0, -1, 0};
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD count = 0;
    WORD i;

    if (id < 0 || id >= robotCount) return 0;
    for (i = 0; i < 4; i++) {
        WORD tx = floodHomeX[id] + dirX[i];
        WORD ty = floodHomeY[id] + dirY[i];
        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
        if (floodBlockHeight[ty][tx] > 0) count++;
    }
    return count;
}


static WORD FloodBlocksOwned(WORD id)
{
    static const WORD dirX[4] = {1, 0, -1, 0};
    static const WORD dirY[4] = {0, 1, 0, -1};
    WORD total;
    WORD i;

    if (id < 0 || id >= robotCount) return 0;
    total = floodCarried[id];
    for (i = 0; i < 4; i++) {
        WORD tx = floodHomeX[id] + dirX[i];
        WORD ty = floodHomeY[id] + dirY[i];
        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
        total += floodBlockHeight[ty][tx];
    }
    return total;
}


static LONG FloodRank(WORD id)
{
    LONG owned = (LONG)FloodBlocksOwned(id);
    if (floodSurrounded[id]) return 1000000L + owned;
    return owned;
}


/* The water reaching the floor is a visible moment, not an instant cut to
 * the result screen: every unprotected robot gets the same stun-warning
 * flash a bolt hit already uses (robots[id].stunTicks, drawn generically in
 * DrawRobotBob) for the length of the pause, and gameplay freezes for that
 * stretch (see the floodPauseTicks branch in StepFloodHouse) before
 * FinishFloodHouse actually ends the round. */
static void ResolveFloodHouse(void)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        floodSurrounded[i] = (FloodHomeWallCount(i) >= 4) ? TRUE : FALSE;
        if (!floodSurrounded[i]) robots[i].stunTicks = FLOODHOUSE_FLOOD_PAUSE_TICKS;
    }
    floodPauseTicks = FLOODHOUSE_FLOOD_PAUSE_TICKS;
    floodPaletteTicks = FLOODHOUSE_PALETTE_TICKS;
}


static void FinishFloodHouse(void)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    WORD pass;
    static const WORD points[3] = {3, 2, 1};

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_FLOODHOUSE) return;

    for (i = 0; i < robotCount; i++) order[i] = i;
    for (pass = 0; pass < robotCount - 1; pass++) {
        for (i = 0; i < robotCount - 1 - pass; i++) {
            if (FloodRank(order[i + 1]) > FloodRank(order[i])) {
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


static void StepFloodHouse(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_FLOODHOUSE) return;

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

    if (floodFlashTicks > 0) floodFlashTicks--;

    /* The flood itself is a held moment, not an instant cut to results:
     * once the clock runs out, gameplay freezes here for
     * FLOODHOUSE_FLOOD_PAUSE_TICKS while unprotected robots flash, and only
     * then does the round actually end. */
    if (floodPauseTicks > 0) {
        floodPauseTicks--;
        ForceGameplayFullPresent();
        if (floodPauseTicks <= 0) FinishFloodHouse();
        FinishGameplayDirtyRects();
        return;
    }

    if (floodTicksRemaining > 0) floodTicksRemaining--;

    if (floodTicksRemaining <= 0) {
        ResolveFloodHouse();
        ForceGameplayFullPresent();
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
    for (i = humanPlayers; i < robotCount; i++) ChooseFloodAiMove(i);

    ServiceHooverMoveSample();
    FinishGameplayDirtyRects();
}


/* ---------------------------------------------------------------------
 * Pictionary - see the PICTIONARY_ comment block in robovac.h for the
 * overall design (only human players ever draw, turns rotate through
 * them, drawing is a toggle rather than true hold-to-draw).
 * --------------------------------------------------------------------- */

static void ClearPictionaryCanvas(void)
{
    WORD x;
    WORD y;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            if (pictionaryPainted[y][x]) {
                pictionaryPainted[y][x] = 0;
                UpdateRoomTile(x, y);
            }
        }
    }
    pictionaryPaintedCount = 0;
}


/* Sets up the next human's turn as drawer: a fresh word, a blank canvas,
 * and every robot repositioned (drawer to the middle of the floor,
 * everyone else lined up out of the way along the bottom so they cannot
 * be mistaken for part of the drawing). Returns FALSE once every human
 * player has already had a turn, so the caller can end the round. */
static BOOL StartPictionaryTurn(void)
{
    WORD i;

    if (pictionaryTurnIndex >= humanPlayers) return FALSE;

    pictionaryDrawer = pictionaryTurnIndex;
    pictionaryWordIndex = (WORD)RandRange(PICTIONARY_WORD_COUNT);
    pictionaryTurnTicks = PICTIONARY_TURN_TICKS;
    pictionaryLastGuesser = -1;
    pictionaryFlashTicks = 0;

    ClearPictionaryCanvas();

    for (i = 0; i < robotCount; i++) {
        UBYTE variant = robots[i].spriteVariant;
        WORD tx;
        WORD ty;

        pictionaryPenDown[i] = FALSE;
        pictionaryGuessed[i] = (i == pictionaryDrawer) ? TRUE : FALSE;

        if (i == pictionaryDrawer) {
            tx = MAP_W / 2;
            ty = MAP_H / 2;
        } else {
            WORD spot = i - ((i > pictionaryDrawer) ? 1 : 0);
            tx = 2 + ((spot * 3) % (MAP_W - 4));
            ty = MAP_H - 2;
        }
        /* SetRobotTile resets spriteVariant to 0 - every other minigame's
         * Start* function saves and restores it around the call for the
         * same reason: without this every robot would suddenly look like
         * the same default colour. */
        SetRobotTile(i, tx, ty);
        robots[i].spriteVariant = variant;
        aiPrevTileX[i] = robots[i].tileX;
        aiPrevTileY[i] = robots[i].tileY;
    }

    snprintf(lastPowerText, sizeof(lastPowerText), "%s IS DRAWING", RobotTag(pictionaryDrawer));
    lastPowerTicks = 80;
    MarkHudStatusTextDirty();
    ForceGameplayFullPresent();
    return TRUE;
}


static void StartPictionary(void)
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
            map[y][x] = (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) ? TILE_WALL : TILE_FLOOR;
            pictionaryPainted[y][x] = 0;
        }
    }
    pictionaryPaintedCount = 0;

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
        robots[i].battery = maxBattery;
        robots[i].powerType = POWER_NONE;
        robots[i].powerMovesLeft = 0;
        robots[i].stunTicks = 0;
        pictionaryScore[i] = 0;
    }

    pictionaryTurnIndex = 0;
    StartPictionaryTurn();

    roundCountdownTicks = ROUND_COUNTDOWN_TOTAL_FRAMES;
    roundGoTicks = 0;
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    MarkHudStatusTextDirty();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


static void TryPictionaryPaint(WORD id)
{
    WORD tx;
    WORD ty;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PICTIONARY) return;
    if (id != pictionaryDrawer) return;
    if (!pictionaryPenDown[id]) return;

    tx = robots[id].tileX;
    ty = robots[id].tileY;
    if (tx <= 0 || ty <= 0 || tx >= MAP_W - 1 || ty >= MAP_H - 1) return;
    if (pictionaryPainted[ty][tx]) return;

    pictionaryPainted[ty][tx] = 1;
    pictionaryPaintedCount++;
    UpdateRoomTile(tx, ty);
}


/* The fire button toggles the drawer's pen instead of requiring it held -
 * see the PICTIONARY_ comment in robovac.h for why. */
static void TryPictionaryToggle(WORD id)
{
    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PICTIONARY) return;
    if (id != pictionaryDrawer) return;

    pictionaryPenDown[id] = !pictionaryPenDown[id];
    if (pictionaryPenDown[id]) TryPictionaryPaint(id);
}


static void TryPictionaryGuess(WORD id)
{
    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PICTIONARY) return;
    if (id < 0 || id >= robotCount) return;
    if (id == pictionaryDrawer) return;
    if (pictionaryGuessed[id]) return;

    pictionaryGuessed[id] = TRUE;
    pictionaryScore[id] += PICTIONARY_GUESS_POINTS;
    pictionaryLastGuesser = id;
    pictionaryFlashTicks = PICTIONARY_FLASH_TICKS;
    MarkHudStatusTextDirty();
}


static void FinishPictionary(void)
{
    WORD order[MAX_ROBOTS];
    WORD i;
    WORD pass;
    static const WORD points[3] = {3, 2, 1};

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PICTIONARY) return;

    for (i = 0; i < robotCount; i++) order[i] = i;
    for (pass = 0; pass < robotCount - 1; pass++) {
        for (i = 0; i < robotCount - 1 - pass; i++) {
            if (pictionaryScore[order[i + 1]] > pictionaryScore[order[i]]) {
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


/* A turn ends either on the clock or once every guesser has answered (see
 * StepPictionary) - either way, the drawer earns a bonus if anyone
 * actually guessed it, then the next human's turn begins, or the round
 * ends once everyone has drawn once. */
static void AdvancePictionaryTurn(void)
{
    if (pictionaryLastGuesser >= 0 && pictionaryDrawer >= 0 && pictionaryDrawer < robotCount) {
        pictionaryScore[pictionaryDrawer] += PICTIONARY_DRAW_BONUS_POINTS;
    }

    pictionaryTurnIndex++;
    if (!StartPictionaryTurn()) {
        FinishPictionary();
    }
}


static void StepPictionary(void)
{
    WORD i;
    WORD countdownNumber;

    if (gameState != GAME_MINIGAME_PLAYING || miniGameType != MINIGAME_PICTIONARY) return;

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

    if (pictionaryFlashTicks > 0) pictionaryFlashTicks--;
    if (pictionaryTurnTicks > 0) pictionaryTurnTicks--;

    if (pictionaryTurnTicks <= 0) {
        AdvancePictionaryTurn();
        FinishGameplayDirtyRects();
        return;
    }

    /* Once everyone eligible has guessed there is no reason to sit out the
     * rest of the clock - gated on a little elapsed time first so a round
     * with no rivals at all (nobody left to guess) never skips itself
     * instantly. */
    if (pictionaryTurnTicks < PICTIONARY_TURN_TICKS - 100) {
        BOOL allGuessed = TRUE;
        for (i = 0; i < robotCount; i++) {
            if (i == pictionaryDrawer) continue;
            if (!pictionaryGuessed[i]) { allGuessed = FALSE; break; }
        }
        if (allGuessed) {
            AdvancePictionaryTurn();
            FinishGameplayDirtyRects();
            return;
        }
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

    /* Only the drawer's movement matters (it is what paints the canvas -
     * see TryPictionaryPaint) but every human can still walk around while
     * they wait their turn, same as any other minigame. Guessers, human or
     * AI, never need a movement chooser: guessing is a button press or a
     * dice roll, not something that requires reaching a tile. */
    for (i = 0; i < humanPlayers; i++) ChoosePlayerMove(i);
    StepPictionaryAiGuesses();

    ServiceHooverMoveSample();
    FinishGameplayDirtyRects();
}


static void RestartCurrentMiniGame(void)
{
    if (miniGameType == MINIGAME_PUCK) StartRoboPuck();
    else if (miniGameType == MINIGAME_BUMPER) StartBumperBots();
    else if (miniGameType == MINIGAME_AIRHOCKEY) StartAirHockey();
    else if (miniGameType == MINIGAME_BOWLING) StartRoboBowling();
    else if (miniGameType == MINIGAME_FLOODHOUSE) StartFloodHouse();
    else if (miniGameType == MINIGAME_PICTIONARY) StartPictionary();
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