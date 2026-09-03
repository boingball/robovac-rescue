#include "robovac.h"


static BOOL IsArenaPlaying(void)
{
    return (gameState == GAME_PLAYING ||
            gameState == GAME_BONUS_PLAYING ||
            gameState == GAME_MINIGAME_PLAYING) ? TRUE : FALSE;
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


static UWORD RandRange(UWORD n)
{
    rng = rng * 1103515245UL + 12345UL;
    if (!n) return 0;
    return (UWORD)((rng >> 16) % n);
}


static void SeedRandom(void)
{
    struct DateStamp stamp;

    DateStamp(&stamp);
    rng ^= ((ULONG)stamp.ds_Days << 16);
    rng ^= ((ULONG)stamp.ds_Minute << 8);
    rng ^= (ULONG)stamp.ds_Tick;
    if (rng == 0) rng = 0x1234ABCDUL;
}


static WORD AbsW(WORD v)
{
    return v < 0 ? -v : v;
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
    /* RoboHockey confines each team to its own half of the table - a paddle
     * can never cross the halfway row, home side or away. */
    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_AIRHOCKEY &&
        id >= 0 && id < robotCount) {
        WORD team = AirHockeyTeamForRobot(id);
        if (team == 0 && ty >= AIRHOCKEY_HALF_Y) return FALSE;
        if (team == 1 && ty < AIRHOCKEY_HALF_Y) return FALSE;
    }
    if (id >= 0 && id < robotCount && robots[id].powerType == POWER_QUAD && robots[id].powerMovesLeft > 0) {
        return TRUE;
    }
    if (id >= 0 && id < robotCount && robots[id].powerType == POWER_WALL_SMASH && robots[id].powerMovesLeft > 0 &&
        map[ty][tx] == TILE_WALL && tx > 0 && ty > 0 && tx < MAP_W - 1 && ty < MAP_H - 1) {
        return TRUE;
    }
    /* Robo Bowling's pin tables never actually block a move - walking into
     * one just knocks it down (see StartRobotMove's TILE_TABLE branch) - so
     * AiFindPathStep must be able to plan a route onto one, not just up to
     * its edge. */
    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BOWLING && map[ty][tx] == TILE_TABLE) {
        return TRUE;
    }
    return !IsBlocked(tx, ty);
}


static BOOL RobotAtTile(WORD tx, WORD ty, WORD ignoreId)
{
    WORD i;

    for (i = 0; i < robotCount; i++) {
        if (i == ignoreId) continue;
        /* An eliminated Bumper Bots robot keeps its last valid tile
         * coordinates (see BumperEliminateRobot) but is out of play, so it
         * must not keep blocking the tile it fell from. */
        if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER &&
            bumperEliminated[i]) continue;

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
        if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER &&
            bumperEliminated[i]) continue;

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


static BOOL AnyHooverMoving(void)
{
    WORD i;

    if (!IsArenaPlaying() || roundCountdownTicks > 0 || pauseMenuOpen) return FALSE;
    for (i = 0; i < robotCount; i++) {
        if (robots[i].moving) return TRUE;
    }
    return FALSE;
}


static BOOL RoundStartLocked(void)
{
    return (IsArenaPlaying() &&
            roundCountdownTicks > 0) ? TRUE : FALSE;
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
    WORD hooverStartX[MAX_ROBOTS];
    WORD hooverStartY[MAX_ROBOTS];

    for (i = 0; i < MAX_ROBOTS; i++) {
        hooverStartX[i] = -1;
        hooverStartY[i] = -1;
    }

    if (hooverModeActive) {
        /* Start the showcase spread around the room rather than putting all
         * of its personality on the four fixed match spawn points. Dirt has
         * already been laid down, so only genuinely open floor is selected. */
        for (i = 0; i < 4; i++) {
            WORD tries;
            for (tries = 0; tries < 200; tries++) {
                WORD x = 1 + RandRange(MAP_W - 2);
                WORD y = 1 + RandRange(MAP_H - 2);
                WORD j;
                BOOL used = FALSE;

                if (map[y][x] != TILE_FLOOR) continue;
                for (j = 0; j < i; j++) {
                    if (hooverStartX[j] == x && hooverStartY[j] == y) {
                        used = TRUE;
                        break;
                    }
                }
                if (used) continue;
                hooverStartX[i] = x;
                hooverStartY[i] = y;
                break;
            }
            if (hooverStartX[i] < 0) {
                hooverStartX[i] = RobotStartX(i);
                hooverStartY[i] = RobotStartY(i);
            }
        }
    }

    robotCount = humanPlayers + aiRivals;
    /* Demo/attract mode deliberately runs with zero human players so every
     * robot is AI-controlled; every other caller still needs at least 1. */
    if (humanPlayers < 1 && !demoModeActive) humanPlayers = 1;
    if (humanPlayers < 0) humanPlayers = 0;
    if (humanPlayers > MAX_HUMAN_PLAYERS) humanPlayers = MAX_HUMAN_PLAYERS;
    if (robotCount < humanPlayers) robotCount = humanPlayers;
    if (robotCount > MAX_ROBOTS) robotCount = MAX_ROBOTS;

    for (i = 0; i < robotCount; i++) {
        WORD startX = (hooverModeActive && i < 4) ? hooverStartX[i] : RobotStartX(i);
        WORD startY = (hooverModeActive && i < 4) ? hooverStartY[i] : RobotStartY(i);

        SetRobotTile(i, startX, startY);
        aiPrevTileX[i] = startX;
        aiPrevTileY[i] = startY;
        {
            WORD h;
            for (h = 0; h < AI_RECENT_TILE_COUNT; h++) {
                aiRecentTileX[i][h] = startX;
                aiRecentTileY[i][h] = startY;
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
        robots[i].tablesPushed = 0;
        speedFlashTicks[i] = 0;
        hooverModeDir[i] = hooverModeActive ? (WORD)RandRange(4) : ((i & 1) ? 2 : 0);
        if (i < humanPlayers) {
            robots[i].spriteVariant = (UBYTE)selectedPlayerVariant[i];
        } else if (hooverModeActive) {
            /* Hoover Mode is a screensaver/showcase, so give each new room a
             * fresh mix of robot personalities instead of repeating the
             * selected-player sequence. */
            robots[i].spriteVariant = (UBYTE)RandRange(ROBOT_VARIANTS);
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
    if (gameState == GAME_PLAYING && robots[id].battery > 0) {
        robots[id].battery -= DIRT_CLEAN_BATTERY_COST;
        if (robots[id].battery < 0) robots[id].battery = 0;
    }
    UpdateRoomTile(tx, ty);
    MaybeStartDirtStorm();
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
        robots[id].turnTicks = (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_RACE) ?
                               RACE_TURN_TICKS : ROBOT_TURN_TICKS;
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

    if (robots[id].powerType == POWER_DOUBLE_SPEED) {
        /* The speed power gets a short cached blitter trail. */
        speedFlashTicks[id] = SPEED_FLASH_TICKS;
    }

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


static void DirtStormBeginPass(void)
{
    dirtStormTileY = 1 + RandRange(MAP_H - 2);
    /* Start a tile width off the left wall so it visibly flies in rather
     * than popping into existence. */
    dirtStormPx = -(LONG)(TILE_SIZE * FP_ONE);
    dirtStormLastDropTileX = -999;
    dirtStormVariant = (UBYTE)RandRange(ROBOT_VARIANTS);
    dirtStormSpinPhase = 0;
    dirtStormActive = TRUE;
}


static void MaybeStartDirtStorm(void)
{
    if (gameState != GAME_PLAYING) return;
    if (demoModeActive || hooverModeActive) return;
    if (dirtStormTriggeredThisRound) return;
    if (dirtStormActive || dirtStormPassesLeft > 0) return;
    /* Fire while there's still a healthy few tiles left, not right on the
     * final one - the storm's first dirt only lands a couple of frames
     * after this, so triggering here keeps CheckEndState from ending the
     * round on dirtLeft hitting 0 before the storm can refill it. */
    if (dirtLeft <= 0 || dirtLeft > DIRT_STORM_TRIGGER_DIRT_LEFT) return;

    /* Only roll once per round either way, so a round that doesn't get the
     * storm this time won't keep re-rolling on every tile cleaned after. */
    dirtStormTriggeredThisRound = TRUE;
    if ((WORD)RandRange(100) >= DIRT_STORM_TRIGGER_CHANCE_PCT) return;

    dirtStormPassesLeft = DIRT_STORM_PASSES;
    dirtStormGapTicks = 0;
    DirtStormBeginPass();
}


static void StepDirtStorm(void)
{
    WORD tx;

    if (gameState != GAME_PLAYING) return;

    if (!dirtStormActive) {
        if (dirtStormGapTicks > 0) {
            dirtStormGapTicks--;
            if (dirtStormGapTicks <= 0 && dirtStormPassesLeft > 0) DirtStormBeginPass();
        }
        return;
    }

    dirtStormPx += DIRT_STORM_SPEED;
    dirtStormSpinPhase = (dirtStormSpinPhase + 1) & 15;
    tx = FP_TO_INT(dirtStormPx) / TILE_SIZE;

    if (tx != dirtStormLastDropTileX) {
        dirtStormLastDropTileX = tx;
        if (tx >= 0 && tx < MAP_W &&
            ValidDirtTile(tx, dirtStormTileY) &&
            !RobotAtTile(tx, dirtStormTileY, -1) &&
            (WORD)RandRange(100) < DIRT_STORM_DROP_CHANCE_PCT) {
            map[dirtStormTileY][tx] = TILE_DIRT;
            AddDirtListTile(tx, dirtStormTileY);
            UpdateRoomTile(tx, dirtStormTileY);
        }
    }

    if (FP_TO_INT(dirtStormPx) >= SCREEN_W) {
        /* No forced full present needed here: BeginGameplayDirtyRects erases
         * the last drawn position from dirtyPrevDirtStorm* every frame, the
         * same two-generation dirty-rect scheme the RoboPuck ball uses, so
         * the final off-screen frame is cleaned up automatically. */
        dirtStormActive = FALSE;
        dirtStormPassesLeft--;
        if (dirtStormPassesLeft > 0) dirtStormGapTicks = DIRT_STORM_GAP_TICKS;
        return;
    }
}


static void StopDirtStorm(WORD stopperId)
{
    if (!dirtStormActive) return;
    dirtStormActive = FALSE;
    dirtStormPassesLeft = 0;
    dirtStormGapTicks = 0;

    if (stopperId >= 0 && stopperId < robotCount) {
        robots[stopperId].score += DIRT_STORM_STOP_POINTS;
        totalScores[stopperId] += DIRT_STORM_STOP_POINTS;
        snprintf(lastPowerText, sizeof(lastPowerText), "%s STOPPED THE SWEEPER +%d", RobotTag(stopperId), DIRT_STORM_STOP_POINTS);
        lastPowerTicks = 80;
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

    {
        /* Every robot that will actually spawn needs its own dock tile, not
         * just the first four - the secret 9-rival mode (O) silently left
         * robots 5-10 with no reachable TILE_DOCK, so they could never
         * recharge once InitRobots placed them at RobotStartX/Y below. */
        WORD dockCount = humanPlayers + aiRivals;
        WORD di;
        if (dockCount > MAX_ROBOTS) dockCount = MAX_ROBOTS;
        for (di = 0; di < dockCount; di++) {
            map[RobotDockY(di)][RobotDockX(di)] = TILE_DOCK;
        }
    }

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
    roundGoSoundPlayed = FALSE;
    ResetGameplaySpeedFrameCounter();
    empCountdownTicks = 0;
    empCountdownOwner = -1;
    dirtStormActive = FALSE;
    dirtStormTriggeredThisRound = FALSE;
    dirtStormPassesLeft = 0;
    dirtStormGapTicks = 0;
    gameState = GAME_PLAYING;
    ClosePauseMenu();

    InitRobots();
    CountDirt();
    BuildRoomBuffer();
    ForceGameplayFullPresent();
    StartRoundCountdownAudio();
}


/* Every cleaning round is followed by a Robo Party interlude, except the
 * match's last round (which ends the match instead - see the GAME_MATCH_END
 * branch in CheckEndState). A longer match, with more robots and so a
 * higher MatchRoundCount(), simply gets more of both. */
static BOOL ShouldStartMiniGameAfterRound(WORD completedRound)
{
    if (completedRound >= MatchRoundCount() - 1) return FALSE;
    return TRUE;
}


static BOOL TryPushTable(WORD id, WORD tx, WORD ty, WORD dx, WORD dy)
{
    WORD pushX = tx + dx;
    WORD pushY = ty + dy;

    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return FALSE;
    if (map[ty][tx] != TILE_TABLE) return FALSE;
    if (pushX <= 0 || pushY <= 0 || pushX >= MAP_W - 1 || pushY >= MAP_H - 1) return FALSE;
    /* Do not bury dirt, docks or another piece of furniture under a table. */
    if (map[pushY][pushX] != TILE_FLOOR) return FALSE;
    if (RobotAtTile(pushX, pushY, id)) return FALSE;

    map[ty][tx] = TILE_FLOOR;
    map[pushY][pushX] = TILE_TABLE;
    UpdateRoomTile(tx, ty);
    UpdateRoomTile(pushX, pushY);
    robots[id].tablesPushed++;
    totalTablePushes[id]++;
    snprintf(lastPowerText, sizeof(lastPowerText), "%s TABLE SHOVED", RobotTag(id));
    lastPowerTicks = 45;
    MarkHudStatusTextDirty();
    ForceGameplayFullPresent();
    return TRUE;
}


static BOOL StartRobotMove(WORD id, WORD dx, WORD dy)
{
    WORD nx;
    WORD ny;
    WORD dockDistNow;
    WORD dockDistNext;

    if (id < 0 || id >= robotCount) return FALSE;
    if (robots[id].moving) return FALSE;
    if (gameState != GAME_MINIGAME_PLAYING && robots[id].battery < batteryCostPerMove) {
        if (robots[id].battery > 0) return FALSE;
        if (robots[id].emergencyMovesLeft <= 0) return FALSE;
    }

    nx = robots[id].tileX + dx;
    ny = robots[id].tileY + dy;

    if (nx >= 0 && ny >= 0 && nx < MAP_W && ny < MAP_H && map[ny][nx] == TILE_TABLE) {
        if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BOWLING) {
            if (!TryKnockdownPin(id, nx, ny)) return FALSE;
        } else if (!TryPushTable(id, nx, ny, dx, dy)) {
            return FALSE;
        }
    }
    if (!RobotCanPassTile(id, nx, ny)) return FALSE;
    if (RobotAtTile(nx, ny, id)) {
        WORD blockedId = RobotIdAtTile(nx, ny, id);
        /* During the bonus boss fight, walking into a teammate who is out of
         * charge shoves them one tile further along instead of just
         * blocking, the same way the boss shoves a grazed robot, so a
         * stranded robot can be nudged back toward a dock instead of
         * getting stuck. */
        if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_RACE) {
            if (!TryRaceBumpRobot(blockedId, dx, dy)) return FALSE;
        } else if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER) {
            if (!BumperPushRobot(blockedId, dx, dy, BUMPER_BUMP_PUSH_TILES, id)) return FALSE;
        } else if (gameState != GAME_BONUS_PLAYING ||
                   !TryPushStrandedRobot(blockedId, dx, dy)) {
            return FALSE;
        }
    }

    if (robots[id].powerType == POWER_WALL_SMASH && robots[id].powerMovesLeft > 0 && map[ny][nx] == TILE_WALL) {
        map[ny][nx] = TILE_FLOOR;
        UpdateRoomTile(nx, ny);
    }

    if (gameState != GAME_MINIGAME_PLAYING && robots[id].battery < batteryCostPerMove) {
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
    if (gameState != GAME_MINIGAME_PLAYING) {
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

        if (id >= 0 && id < humanPlayers) moves++;
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

    if (gameState == GAME_MINIGAME_PLAYING) {
        if (miniGameType == MINIGAME_RACE) {
            if (raceBoostMoves[id] > 0) raceBoostMoves[id]--;
            RaceHandleRobotArrival(id);
        }
        return;
    }

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
        LONG stepSpeed;
        if (gameState == GAME_MINIGAME_PLAYING) {
            if (miniGameType == MINIGAME_RACE) {
                stepSpeed = (raceBoostMoves[id] > 0) ? RACE_BOOST_SPEED : RACE_MOVE_SPEED;
            } else {
                /* Puck and Bumper Bots share the same snappier step speed. */
                stepSpeed = PUCK_ROBOT_MOVE_SPEED;
            }
        } else {
            stepSpeed = (robots[id].battery <= 0) ? EMERGENCY_MOVE_SPEED : MOVE_SPEED;
        }
        if (gameState != GAME_MINIGAME_PLAYING &&
            robots[id].powerType == POWER_DOUBLE_SPEED &&
            robots[id].powerMovesLeft > 0 && robots[id].battery > 0) {
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


static void ChoosePlayerMove(WORD id)
{
    if (id < 0 || id >= humanPlayers) return;
    if (robots[id].moving) return;
    if (robots[id].stunTicks > 0) return;
    /* racePlace[] is only ever reset by StartRoboRace, so a stale "already
     * finished" value from an earlier race must not carry over and block
     * movement in a later Puck or Bumper Bots round. */
    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_RACE && racePlace[id] >= 0) return;

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


/* Shared by the normal cleaning-round match end (CheckEndState) and Party
 * Mode's all-minigame match end (ActivateSpaceOrFireAction) - whichever
 * path got here, the final winner and bonus-round eligibility are worked
 * out from totalScores[] the same way. */
static void FinalizeMatchEnd(void)
{
    WORD i;

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
        if (hooverModeActive) StartHooverMode();
        else StartDemoMode();
        return;
    }

    if (roundIndex >= MatchRoundCount() - 1) {
        FinalizeMatchEnd();
    } else {
        gameState = GAME_ROUND_END;
    }
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
    hooverModeActive = FALSE;
    partyModeActive = FALSE;
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
    WORD countdownNumber;

    if (gameState == GAME_INTRO) {
        StepIntro();
        return;
    }

    if (gameState == GAME_MINIGAME_INTRO) {
        if (miniGameIntroTicks > 0) miniGameIntroTicks--;
        if (miniGameIntroTicks <= 0) {
            RestartCurrentMiniGame();
        }
        return;
    }

    if (gameState == GAME_MINIGAME_PLAYING) {
        if (miniGameType == MINIGAME_PUCK) StepRoboPuck();
        else if (miniGameType == MINIGAME_BUMPER) StepBumperBots();
        else if (miniGameType == MINIGAME_AIRHOCKEY) StepAirHockey();
        else if (miniGameType == MINIGAME_BOWLING) StepRoboBowling();
        else StepRoboRace();
        return;
    }

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING) {
        StopEmpPaletteCycle();
        StopNightMode();
        return;
    }

    if (pauseMenuOpen) {
        /* Suspend any active EMP/night-mode palette dimming while paused,
         * rather than letting UpdateNightMode/UpdateEmpPaletteCycle keep
         * cycling it - the pause menu text shares low pens (7/14) with the
         * dimmed room colours, so an EMP blackout mid-pause otherwise left
         * the menu unreadable. Neither effect's own countdown ticks down
         * below, so resuming picks the dim back up exactly where it left
         * off. */
        StopEmpPaletteCycle();
        StopNightMode();
        BeginGameplayDirtyRects();
        ServiceHooverMoveSample();
        ForceGameplayFullPresent();
        FinishGameplayDirtyRects();
        return;
    }

    UpdateNightMode();
    /* Apply EMP after any night-mode transition so the EMP room dimming is
     * the final palette state for the frame. */
    UpdateEmpPaletteCycle();

    BeginGameplayDirtyRects();

    StepRoundStartSamples();

    if (roundCountdownTicks > 0) {
        countdownNumber = ((roundCountdownTicks - 1) / ROUND_COUNTDOWN_STEP_FRAMES) + 1;
        if (countdownNumber != roundCountdownLastSoundNumber) {
            PlayCountdownSample();
            roundCountdownLastSoundNumber = countdownNumber;
        }

        if (frameTicks < roundCountdownTicks) {
            roundCountdownTicks -= frameTicks;
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

    /* Dirt Storm is a display-rate effect.  Advancing its fixed-point BOB
     * before the optional gameplay-speed skip keeps the fly-by smooth at
     * PAL 50 Hz even when normal movement runs at the default 33 Hz. */
    if (gameState == GAME_PLAYING) StepDirtStorm();

    if (!ShouldAdvanceGameplayFrame()) {
        ServiceHooverMoveSample();
        FinishGameplayDirtyRects();
        return;
    }

    StepBonusBoss();

    for (i = 0; i < robotCount; i++) {
        StepRobotMovement(i);
        if (speedFlashTicks[i] > 0) speedFlashTicks[i]--;
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
    WORD i;

    if (!IsArenaPlaying()) return;
    pauseMenuOpen = TRUE;
    pauseMenuSelection = 0;
    ForceGameplayFullPresent();
    ClearMovementKeys();
    /* Treat whatever direction a stick already happens to be held in as
     * already consumed, so opening the pause menu with the stick pushed
     * (e.g. mid-move) can't immediately toggle the selection by itself. */
    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        pauseJoyUpPrev[i] = TRUE;
        pauseJoyDownPrev[i] = TRUE;
    }
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
    /* Both solo and two-player flows use this same "how many AI" popup;
     * the two-player call sites (TitleLockPlayer2/StartWithRivals) already
     * arm humanPlayers/titleTwoPlayerArmed/titlePlayer2Locked before they
     * get here, so this must not force them - doing so used to hijack a
     * solo game into two-player mode the moment this opened. */
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
    OpenAiDifficultyMenu(humanPlayers, rivals);
}


static void ActivatePauseMenuSelection(void)
{
    if (pauseMenuSelection == 0) {
        if (gameState == GAME_MINIGAME_PLAYING) RestartCurrentMiniGame();
        else if (gameState == GAME_BONUS_PLAYING) StartBonusRound();
        else ResetLevel();
    } else {
        ClosePauseMenu();
        EnterTitleScreen();
    }
}


static void StartMatch(WORD players, WORD rivals)
{
    WORD i;
    StopMenuMusic();
    partyModeActive = FALSE;
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
    for (i = 0; i < MAX_ROBOTS; i++) {
        roundWins[i] = 0;
        totalScores[i] = 0;
        totalTablePushes[i] = 0;
    }
    titleTwoPlayerArmed = FALSE;
    titlePlayer2Locked = FALSE;
    titleSelectPlayer = 0;
    aiDifficultyMenuOpen = FALSE;
    ResetAllJoystickConfirmHolds();
    ResetLevel();
}


/* Party Mode: skips cleaning rounds entirely and plays every Robo Party
 * mini-game exactly once, in a shuffled order, ending on the normal
 * match-end leaderboard (and bonus round, if anyone qualifies) - a fast,
 * one-key way to see (or show off) every party game without playing a full
 * match around them. Solo plus three AI rivals, Normal difficulty, no menu
 * in the way. */
static void StartPartyMode(void)
{
    WORD i;

    for (i = 0; i < MINIGAME_COUNT; i++) partyModeQueue[i] = i + 1;
    for (i = MINIGAME_COUNT - 1; i > 0; i--) {
        WORD j = (WORD)RandRange(i + 1);
        WORD tmp = partyModeQueue[i];
        partyModeQueue[i] = partyModeQueue[j];
        partyModeQueue[j] = tmp;
    }
    partyModeQueueIndex = 0;

    aiDifficulty = 1;
    StartMatch(1, 3);
    partyModeActive = TRUE;
    StartMiniGameIntro();
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
    hooverModeActive = FALSE;
    /* The first joystick poll after an idle attract start establishes a baseline.
     * Only a new edge after that baseline is allowed to cancel the demo. */
    demoJoyPrimed = FALSE;
    humanPlayers = 0;
    aiRivals = 4;
    aiDifficulty = 0;
    roundIndex = 0;
    MarkHudStatusTextDirty();
    bonusAvailable = FALSE;
    bonusBossHealth = 0;
    for (i = 0; i < MAX_ROBOTS; i++) {
        roundWins[i] = 0;
        totalScores[i] = 0;
        totalTablePushes[i] = 0;
    }
    titleTwoPlayerArmed = FALSE;
    titlePlayer2Locked = FALSE;
    titleSelectPlayer = 0;
    aiDifficultyMenuOpen = FALSE;
    aiSelectMenuOpen = FALSE;
    ResetAllJoystickConfirmHolds();
    /* The menu-driven match-start paths (ActivateAiDifficultyMenu/
     * ActivateAiSelectMenu) always mark the title screen fully dirty before
     * handing off to gameplay, via their CloseAiDifficultyMenu/
     * CloseAiSelectMenu calls. The idle attract start is a direct shortcut
     * with no menu to close,
     * so without this the title screen's cached bitmap could still be
     * flagged clean and never get overdrawn, leaving it visible under/after
     * the demo starts. */
    MarkTitleAllDirty();
    ResetLevel();
}


/* Hidden Hoover Mode: a self-running cleaning-pattern showcase. The same
 * four AI hoovers used by the attract demo are given deterministic straight
 * sweeps, making D a fun screensaver for the room layouts and furniture. */
static void StartHooverMode(void)
{
    WORD i;

    StopMenuMusic();
    demoModeActive = TRUE;
    hooverModeActive = TRUE;
    demoJoyPrimed = FALSE;
    humanPlayers = 0;
    aiRivals = 4;
    aiDifficulty = 0;
    roundIndex = 0;
    MarkHudStatusTextDirty();
    bonusAvailable = FALSE;
    bonusBossHealth = 0;
    for (i = 0; i < MAX_ROBOTS; i++) {
        roundWins[i] = 0;
        totalScores[i] = 0;
        totalTablePushes[i] = 0;
    }
    titleTwoPlayerArmed = FALSE;
    titlePlayer2Locked = FALSE;
    titleSelectPlayer = 0;
    aiDifficultyMenuOpen = FALSE;
    aiSelectMenuOpen = FALSE;
    ResetAllJoystickConfirmHolds();
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
    BOOL bumperPlaying = (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER);

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING && !bumperPlaying) return;
    if (RoundStartLocked()) return;
    if (id < 0 || id >= robotCount) return;
    if (robots[id].stunTicks > 0) return;
    if (bumperPlaying && bumperEliminated[id]) return;
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
    BOOL bumperPlaying = (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER);
    BOOL airHockeyPlaying = (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_AIRHOCKEY);

    if (gameState != GAME_PLAYING && gameState != GAME_BONUS_PLAYING && !bumperPlaying && !airHockeyPlaying) return;
    if (RoundStartLocked()) return;
    if (id < 0 || id >= humanPlayers || id >= MAX_HUMAN_PLAYERS) return;

    /* RoboHockey's fire button is an EMP-charged power shot on the puck
     * rather than a directional bolt - route it there instead of falling
     * through to the normal aim-and-fire logic below. */
    if (airHockeyPlaying) {
        TryAirHockeyBoost(id);
        return;
    }

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

    if (gameState == GAME_MINIGAME_PLAYING && miniGameType == MINIGAME_BUMPER) {
        for (i = 0; i < robotCount; i++) {
            if (i == ownerId || bumperEliminated[i]) continue;
            if (AbsW(robots[i].tileX - tx) + AbsW(robots[i].tileY - ty) <= 1) {
                BumperPushRobot(i, bolt->dirX, bolt->dirY, BUMPER_BOLT_PUSH_TILES, ownerId);
                bolt->active = FALSE;
                return;
            }
        }
        return;
    }

    if (dirtStormActive) {
        WORD stormTx = FP_TO_INT(dirtStormPx) / TILE_SIZE;
        if (AbsW(stormTx - tx) + AbsW(dirtStormTileY - ty) <= 1) {
            StopDirtStorm(ownerId);
            bolt->active = FALSE;
            return;
        }
    }

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