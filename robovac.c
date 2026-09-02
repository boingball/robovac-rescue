#include "robovac.h"

#include "game.c"
#include "ai.c"
#include "render.c"
#include "audio.c"
#include "minigames.c"
#include "network.c"


static BOOL HandleAiDifficultyMenuRawKey(UWORD code, BOOL keyUpEvent)
{
    if (!aiDifficultyMenuOpen) return FALSE;
    if (keyUpEvent) return TRUE;

    if (code == RAW_Q || code == RAW_ESC) {
        CloseAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_UP) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 2) % 3;
        return TRUE;
    }
    if (code == RAW_DOWN) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 1) % 3;
        return TRUE;
    }
    if (code == RAW_1 || code == RAW_E) {
        aiDifficultyMenuSelection = 0;
        ActivateAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_2 || code == RAW_N) {
        aiDifficultyMenuSelection = 1;
        ActivateAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_3 || code == RAW_H) {
        aiDifficultyMenuSelection = 2;
        ActivateAiDifficultyMenu();
        return TRUE;
    }
    if (code == RAW_RETURN || code == RAW_SPACE) {
        ActivateAiDifficultyMenu();
        return TRUE;
    }

    return TRUE;
}


static BOOL HandleAiSelectMenuRawKey(UWORD code, BOOL keyUpEvent)
{
    if (!aiSelectMenuOpen) return FALSE;
    if (keyUpEvent) return TRUE;

    if (code == RAW_Q || code == RAW_ESC) {
        CloseAiSelectMenu();
        return TRUE;
    }
    if (code == RAW_UP) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 3) & 3;
        return TRUE;
    }
    if (code == RAW_DOWN) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 1) & 3;
        return TRUE;
    }
    if (code == RAW_0 || code == RAW_1 || code == RAW_2 || code == RAW_3) {
        aiSelectMenuSelection = (code == RAW_0) ? 0 : (WORD)code;
        ActivateAiSelectMenu();
        return TRUE;
    }
    if (code == RAW_RETURN || code == RAW_SPACE) {
        ActivateAiSelectMenu();
        return TRUE;
    }

    return TRUE;
}


static BOOL HandlePauseMenuRawKey(UWORD code, BOOL keyUpEvent)
{
    if (!pauseMenuOpen) return FALSE;
    if (keyUpEvent) return TRUE;

    if (code == RAW_Q || code == RAW_ESC) {
        ClosePauseMenu();
        return TRUE;
    }
    if (code == RAW_UP || code == RAW_DOWN) {
        pauseMenuSelection = 1 - pauseMenuSelection;
        return TRUE;
    }
    if (code == RAW_RETURN || code == RAW_SPACE) {
        ActivatePauseMenuSelection();
        return TRUE;
    }

    return TRUE;
}



static BOOL ActivateSpaceOrFireAction(void)
{
    if (gameState == GAME_ROUND_END) {
        if (ShouldStartMiniGameAfterRound(roundIndex)) StartMiniGameIntro();
        else { roundIndex++; MarkHudStatusTextDirty(); ResetLevel(); }
        return TRUE;
    }
    if (gameState == GAME_MINIGAME_INTRO) { RestartCurrentMiniGame(); return TRUE; }
    if (gameState == GAME_MINIGAME_END) { roundIndex++; MarkHudStatusTextDirty(); ResetLevel(); return TRUE; }
    if (gameState == GAME_MATCH_END && bonusAvailable) { StartBonusRound(); return TRUE; }
    if (gameState == GAME_BONUS_END || gameState == GAME_MATCH_END) { EnterTitleScreen(); return TRUE; }
    if (gameState == GAME_TITLE && titleTwoPlayerArmed && !titlePlayer2Locked) { TitleLockPlayer2(); return TRUE; }
    /* Solo confirms land here too, not just the two-player flow: previously
     * this skipped straight to the difficulty popup using whatever aiRivals
     * happened to be left over (default 1), so a joystick-only player who
     * had no way to type "1"/"2"/"3"/"O" first could never actually choose
     * a rival count - they always got 1 AI. Show the same "how many rivals"
     * popup two-player mode gets instead. */
    if (gameState == GAME_TITLE) { OpenAiSelectMenu(aiRivals); return TRUE; }
    return FALSE;
}


static void HandleRawKey(UWORD rawCode)
{
    BOOL keyUpEvent = (rawCode & 0x80) ? TRUE : FALSE;
    UWORD code = rawCode & 0x7F;

    /* Attract mode: any keypress hands control straight back to a real
     * player instead of being interpreted as a P1/P2 command. */
    if (demoModeActive) {
        if (!keyUpEvent) {
            demoModeActive = FALSE;
            EnterTitleScreen();
        }
        return;
    }

    if (HandleAiDifficultyMenuRawKey(code, keyUpEvent)) {
        return;
    }

    if (HandleAiSelectMenuRawKey(code, keyUpEvent)) {
        return;
    }

    if (HandlePauseMenuRawKey(code, keyUpEvent)) {
        return;
    }

    if (code == RAW_ESC && !keyUpEvent) {
        running = FALSE;
        return;
    }

    if (!keyUpEvent && code == RAW_Q && IsArenaPlaying()) {
        OpenPauseMenu();
        return;
    }

    /* Hidden Big Head mode: a purely cosmetic toggle, works any time during
     * play. `B` was already taken by Player 1's fire, so this rides `G`. */
    if (!keyUpEvent && code == RAW_G) {
        bigHeadMode = !bigHeadMode;
        if (IsArenaPlaying()) {
            ForceGameplayFullPresent();
            snprintf(lastPowerText, sizeof(lastPowerText), "BIG MODE %s", bigHeadMode ? "ON" : "OFF");
            lastPowerTicks = 80;
        }
        return;
    }

    if (!keyUpEvent && gameState == GAME_INTRO) {
        EnterTitleScreen();
        return;
    }

    if (!keyUpEvent && gameState == GAME_TITLE) {
        if (code == RAW_LEFT) { if (!titleTwoPlayerArmed || titlePlayer2Locked) { titleSelectPlayer = 0; MarkTitlePanelDirty(); TitleChooseVariant(0, -1); } return; }
        if (code == RAW_RIGHT) { if (!titleTwoPlayerArmed || titlePlayer2Locked) { titleSelectPlayer = 0; MarkTitlePanelDirty(); TitleChooseVariant(0, 1); } return; }
        if (code == RAW_Z) { if (!titleTwoPlayerArmed || titlePlayer2Locked) TitleArmTwoPlayer(); titleSelectPlayer = 1; MarkTitlePanelDirty(); TitleChooseVariant(1, -1); return; }
        if (code == RAW_C) { if (!titleTwoPlayerArmed || titlePlayer2Locked) TitleArmTwoPlayer(); titleSelectPlayer = 1; MarkTitlePanelDirty(); TitleChooseVariant(1, 1); return; }
        if (code == RAW_V) { TitlePlayer2Fire(); return; }
        /* Don't hijack the title screen into a demo while the AI
         * difficulty/rival menus are up mid-selection - starting a match
         * pulls the rug out from under whatever the player was choosing
         * and can leave that menu's cached overlay stuck on screen. */
        if (code == RAW_D && !aiDifficultyMenuOpen && !aiSelectMenuOpen) { StartHooverMode(); return; }
        if (code == RAW_0 && titleTwoPlayerArmed && titlePlayer2Locked) { OpenAiSelectMenu(0); return; }
        if (code == RAW_1) { StartWithRivals(1); return; }
        if (code == RAW_2) { StartWithRivals(2); return; }
        if (code == RAW_3) { StartWithRivals(3); return; }
        if (code == RAW_4) { CycleGameSpeed(); return; }
        if (code == RAW_O) { StartWithRivals(9); return; }
        if (code == RAW_E) { maxBattery = 110; batteryCostPerMove = 1; MarkHudStatusTextDirty(); return; }
        if (code == RAW_S) { maxBattery = 55; batteryCostPerMove = 2; MarkHudStatusTextDirty(); return; }
        if (code == RAW_H) { maxBattery = 36; batteryCostPerMove = 3; MarkHudStatusTextDirty(); return; }
    }

    if (!keyUpEvent && code == RAW_B) { FirePlayerBolt(0); return; }
    if (!keyUpEvent && code == RAW_V) { FirePlayerBolt(1); return; }

    if (!keyUpEvent && code == RAW_R) {
        if (gameState == GAME_PLAYING) { ResetLevel(); return; }
        if (gameState == GAME_BONUS_PLAYING) { StartBonusRound(); return; }
        if (gameState == GAME_MINIGAME_PLAYING) { RestartCurrentMiniGame(); return; }
    }

    if (!keyUpEvent && (code == RAW_SPACE || code == RAW_RETURN)) {
        if (ActivateSpaceOrFireAction()) return;
    }

    switch (code) {
        case RAW_LEFT:  keyLeft[0]  = !keyUpEvent; break;
        case RAW_RIGHT: keyRight[0] = !keyUpEvent; break;
        case RAW_UP:    keyUp[0]    = !keyUpEvent; break;
        case RAW_DOWN:  keyDown[0]  = !keyUpEvent; break;
        case RAW_Z:     keyLeft[1]  = !keyUpEvent; break;
        case RAW_C:     keyRight[1] = !keyUpEvent; break;
        case RAW_S:     keyUp[1]    = !keyUpEvent; break;
        case RAW_X:     keyDown[1]  = !keyUpEvent; break;
        default: break;
    }
}

static BOOL ReadJoystickFire(WORD id)
{
    UBYTE pra = ciaa.ciapra;

    /* On-screen J1/P1 deliberately uses the physical Amiga joystick port
     * (custom.joy1dat plus CIAA PRA bit 7).  On-screen J2/P2 uses the mouse
     * port (custom.joy0dat plus CIAA PRA bit 6), with fire-only protection
     * applied in PollJoysticks outside the title join/confirm flow.
     */
    if (id == 0) return (pra & 0x80) ? FALSE : TRUE;
    return (pra & 0x40) ? FALSE : TRUE;
}


static void ResetJoystickConfirmHold(WORD id)
{
    if (id < 0 || id >= MAX_HUMAN_PLAYERS) return;
    joyFireHoldTicks[id] = 0;
    joyFireHoldTriggered[id] = FALSE;
    joyBluePrev[id] = FALSE;
}


static void ResetAllJoystickConfirmHolds(void)
{
    WORD i;
    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        ResetJoystickConfirmHold(i);
    }
}


static BOOL ReadJoystickBlueButton(WORD id)
{
    /* The second fire button (a CD32 pad's blue button, or a two-button
     * stick's second button) is wired to the port's POTY pin, not the CIA
     * fire line.  PollJoysticks drives the pot data lines high via POTGO each
     * frame; a pressed second button then grounds its POTY line, so the
     * matching POTINP data bit reads back low:
     *   Player 1 (joystick port, id 0): POTY = POTINP bit 14 (DATRY)
     *   Player 2 (mouse port,   id 1): POTY = POTINP bit 10 (DATLY)
     * This reads the CD32 blue button in the pad's default (un-shifted) mode
     * without the full CD32 shift-register protocol the other pad buttons
     * would need.  Hold-to-confirm stays as a fallback for one-button sticks.
     */
    UWORD pin = custom.potinp;
    if (id == 0) return (pin & 0x4000) ? FALSE : TRUE;
    return (pin & 0x0400) ? FALSE : TRUE;
}


static BOOL UpdateJoystickConfirmHold(WORD id, BOOL fire)
{
    if (id < 0 || id >= MAX_HUMAN_PLAYERS) return FALSE;

    if (!fire) {
        joyFireHoldTicks[id] = 0;
        joyFireHoldTriggered[id] = FALSE;
        return FALSE;
    }

    if (joyFireHoldTriggered[id]) return FALSE;
    if (joyFireHoldTicks[id] < JOY_CONFIRM_HOLD_FRAMES) joyFireHoldTicks[id]++;
    if (joyFireHoldTicks[id] >= JOY_CONFIRM_HOLD_FRAMES) {
        joyFireHoldTriggered[id] = TRUE;
        return TRUE;
    }
    return FALSE;
}


static BOOL ActivateTitleJoystickConfirmAction(void)
{
    if (aiDifficultyMenuOpen) { ActivateAiDifficultyMenu(); return TRUE; }
    if (aiSelectMenuOpen) { ActivateAiSelectMenu(); return TRUE; }
    return ActivateSpaceOrFireAction();
}


static void HandleAiDifficultyJoystick(BOOL up, BOOL down, BOOL firePressed)
{
    static BOOL prevUp = FALSE;
    static BOOL prevDown = FALSE;

    if (!aiDifficultyMenuOpen) {
        prevUp = up;
        prevDown = down;
        return;
    }

    if (up && !prevUp) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 2) % 3;
    }
    if (down && !prevDown) {
        aiDifficultyMenuSelection = (aiDifficultyMenuSelection + 1) % 3;
    }
    if (firePressed) {
        ActivateAiDifficultyMenu();
    }

    prevUp = up;
    prevDown = down;
}


static void HandleAiSelectJoystick(BOOL up, BOOL down, BOOL firePressed)
{
    static BOOL prevUp = FALSE;
    static BOOL prevDown = FALSE;

    if (!aiSelectMenuOpen) {
        prevUp = up;
        prevDown = down;
        return;
    }

    if (up && !prevUp) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 3) & 3;
    }
    if (down && !prevDown) {
        aiSelectMenuSelection = (aiSelectMenuSelection + 1) & 3;
    }
    if (firePressed) {
        ActivateAiSelectMenu();
    }

    prevUp = up;
    prevDown = down;
}


static void HandleTitleJoystick(WORD id, BOOL left, BOOL right, BOOL up, BOOL down, BOOL firePressed, BOOL confirmPressed)
{
    static BOOL prevLeft[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};
    static BOOL prevRight[MAX_HUMAN_PLAYERS] = {FALSE, FALSE};

    if (gameState != GAME_TITLE) {
        prevLeft[id] = left;
        prevRight[id] = right;
        if (id == 0) {
            HandleAiSelectJoystick(FALSE, FALSE, FALSE);
            HandleAiDifficultyJoystick(FALSE, FALSE, FALSE);
        }
        return;
    }

    if (!joyEnabled[id]) {
        prevLeft[id] = left;
        prevRight[id] = right;
        if (id == 0) {
            HandleAiSelectJoystick(FALSE, FALSE, FALSE);
            HandleAiDifficultyJoystick(FALSE, FALSE, FALSE);
        }
        return;
    }

    if (aiDifficultyMenuOpen) {
        if (id == 0) {
            /* A plain fire tap selects the highlighted entry here too, not
             * just blue/hold-to-confirm, so a single-fire-button stick or an
             * undetected CD32 pad can still drive this menu. */
            HandleAiDifficultyJoystick(up, down, confirmPressed || firePressed);
        }
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (aiSelectMenuOpen) {
        if (id == 0) {
            HandleAiSelectJoystick(up, down, confirmPressed || firePressed);
        }
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (confirmPressed) {
        ActivateTitleJoystickConfirmAction();
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (firePressed) {
        if (id == 0) {
            /* J1 fire shares the left mouse button line, so on the title
             * screen it only confirms/focuses Player 1's carousel selection.
             * Starting/advancing is left to Space, keyboard AI shortcuts, or
             * the J1 second button/RMB path handled by Intuition.
             */
            titleSelectPlayer = 0;
            MarkTitlePanelDirty();
        } else if (id == 1) {
            TitleArmTwoPlayer();
        }
        prevLeft[id] = left;
        prevRight[id] = right;
        return;
    }

    if (id == 0) {
        if (!titleTwoPlayerArmed || titlePlayer2Locked) {
            titleSelectPlayer = 0;
            MarkTitlePanelDirty();
            if (left && !prevLeft[id]) TitleChooseVariant(0, -1);
            if (right && !prevRight[id]) TitleChooseVariant(0, 1);
        }
    } else if (titleTwoPlayerArmed && !titlePlayer2Locked) {
        titleSelectPlayer = 1;
        MarkTitlePanelDirty();
        if (left && !prevLeft[id]) TitleChooseVariant(1, -1);
        if (right && !prevRight[id]) TitleChooseVariant(1, 1);
    }

    prevLeft[id] = left;
    prevRight[id] = right;
}


static void PollJoysticks(void)
{
    UWORD dat[MAX_HUMAN_PLAYERS];
    WORD i;

    /* CD32 pads were previously read through lowlevel.library's ReadJoyPort()
     * to expose all seven buttons via its shift-register protocol. That call
     * ran every single frame right before the direct joy1dat/CIA reads below,
     * and on real hardware left P1's port completely unresponsive (movement
     * and fire both dead) -- almost certainly the two fighting over the same
     * POTGO/shift-register lines. A CD32 pad's d-pad and red fire button are
     * already wired to the standard joystick lines for compatibility, and its
     * blue button already reads through the POTY pin trick in
     * ReadJoystickBlueButton below, so dropping the lowlevel path only costs
     * the pad's Play button (Q still pauses from the keyboard) in exchange
     * for the port actually working. */

    /* Drive the four pot data lines high as outputs so the second-button
     * (two-button stick button 2) read in ReadJoystickBlueButton is valid this
     * frame; a pressed button then pulls its POTY line low. */
    custom.potgo = 0xff00;

    dat[0] = custom.joy1dat; /* on-screen J1/P1 = physical joystick port */
    dat[1] = custom.joy0dat; /* on-screen J2/P2 = mouse port */

    for (i = 0; i < MAX_HUMAN_PLAYERS; i++) {
        WORD stateBefore = gameState;
        BOOL left, right, up, down, fire, blue;
        BOOL bluePressed;
        BOOL directionActive;
        BOOL firePressed;
        BOOL holdConfirmed;
        BOOL confirmPressed;

        left  = JOY_LEFT(dat[i]);
        right = JOY_RIGHT(dat[i]);
        up    = JOY_UP(dat[i]);
        down  = JOY_DOWN(dat[i]);
        fire  = ReadJoystickFire(i);
        blue  = ReadJoystickBlueButton(i);

        directionActive = left || right || up || down;

        /* Outside the title screen, keep mouse-port protection for the direct
         * read: a mouse left click is electrically the same as J2/P2 fire.
         * During the title screen, allow fire-only input so player 2 can
         * join/arm before moving the stick and so hold-to-confirm works. The
         * intro screen gets the same exemption so a P2 joystick's fire button
         * can skip it (a stray mouse click there already skips the intro too,
         * via the SELECTDOWN handler in PollWindowMessages).
         */
        if (gameState != GAME_TITLE && gameState != GAME_INTRO && i == 1 && !joyEnabled[i] && !directionActive) {
            fire = FALSE;
        }

        bluePressed = (blue && !joyBluePrev[i]) ? TRUE : FALSE;

        if (gameState == GAME_TITLE && (directionActive || fire || blue)) titleIdleTicks = 0;

        if (gameState == GAME_INTRO) {
            if (fire || blue) {
                joyFirePrev[i] = fire;
                joyBluePrev[i] = blue;
                EnterTitleScreen();
                return;
            }
            joyFirePrev[i] = fire;
            joyBluePrev[i] = blue;
            continue;
        }

        if (pauseMenuOpen) {
            BOOL upEdge = up && !pauseJoyUpPrev[i];
            BOOL downEdge = down && !pauseJoyDownPrev[i];
            BOOL fireEdge = fire && !joyFirePrev[i];

            pauseJoyUpPrev[i] = up;
            pauseJoyDownPrev[i] = down;
            joyFirePrev[i] = fire;
            joyBluePrev[i] = blue;

            if (upEdge || downEdge) {
                pauseMenuSelection = 1 - pauseMenuSelection;
            }
            if (fireEdge || bluePressed) {
                ActivatePauseMenuSelection();
            }
            continue;
        }

        if (demoModeActive) {
            BOOL newDemoInput;

            if (!demoJoyPrimed) {
                demoPrevLeft[i] = left;
                demoPrevRight[i] = right;
                demoPrevUp[i] = up;
                demoPrevDown[i] = down;
                demoPrevFire[i] = fire;
                demoPrevBlue[i] = blue;
                joyFirePrev[i] = fire;
                joyBluePrev[i] = blue;
                if (i == MAX_HUMAN_PLAYERS - 1) demoJoyPrimed = TRUE;
                continue;
            }

            newDemoInput = (left && !demoPrevLeft[i]) ||
                           (right && !demoPrevRight[i]) ||
                           (up && !demoPrevUp[i]) ||
                           (down && !demoPrevDown[i]) ||
                           (fire && !demoPrevFire[i]) ||
                           (blue && !demoPrevBlue[i]);

            demoPrevLeft[i] = left;
            demoPrevRight[i] = right;
            demoPrevUp[i] = up;
            demoPrevDown[i] = down;
            demoPrevFire[i] = fire;
            demoPrevBlue[i] = blue;

            if (newDemoInput) {
                demoModeActive = FALSE;
                joyFirePrev[i] = fire;
                joyBluePrev[i] = blue;
                EnterTitleScreen();
                return;
            }
        }

        firePressed = (fire && !joyFirePrev[i]) ? TRUE : FALSE;
        if (gameState == GAME_TITLE) {
            holdConfirmed = UpdateJoystickConfirmHold(i, fire);
            confirmPressed = bluePressed || holdConfirmed;
        } else {
            ResetJoystickConfirmHold(i);
            holdConfirmed = FALSE;
            confirmPressed = FALSE;
        }

        if (firePressed || (gameState == GAME_TITLE && fire)) {
            if (!joyEnabled[i] && gameState == GAME_TITLE) MarkTitleAllDirty();
            joyEnabled[i] = TRUE;
        }

        if (gameState == GAME_TITLE) {
            HandleTitleJoystick(i, left, right, up, down, firePressed, confirmPressed);
        }

        joyLeft[i] = joyEnabled[i] ? left : FALSE;
        joyRight[i] = joyEnabled[i] ? right : FALSE;
        joyUp[i] = joyEnabled[i] ? up : FALSE;
        joyDown[i] = joyEnabled[i] ? down : FALSE;

        if (stateBefore == GAME_PLAYING || stateBefore == GAME_BONUS_PLAYING ||
            stateBefore == GAME_MINIGAME_PLAYING) {
            if (joyEnabled[i] && firePressed) FirePlayerBolt(i);
        } else if (stateBefore != GAME_TITLE && joyEnabled[i] && firePressed) {
            ActivateSpaceOrFireAction();
        }
        joyFirePrev[i] = fire;
        joyBluePrev[i] = blue;
    }
}


static void PollWindowMessages(void)
{
    struct IntuiMessage *msg;

    if (!win || !win->UserPort) return;

    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ULONG cls = msg->Class;
        UWORD code = msg->Code;

        ReplyMsg((struct Message *)msg);

        if (gameState == GAME_TITLE) titleIdleTicks = 0;

        if (cls == IDCMP_RAWKEY) {
            HandleRawKey(code);
        } else if (cls == IDCMP_MOUSEBUTTONS) {
            if (demoModeActive && code == SELECTDOWN) {
                demoModeActive = FALSE;
                EnterTitleScreen();
                continue;
            }
            if (code == SELECTDOWN && gameState == GAME_INTRO) EnterTitleScreen();
            if (code == MENUDOWN && !IsArenaPlaying()) {
                if (aiDifficultyMenuOpen) ActivateAiDifficultyMenu();
                else if (aiSelectMenuOpen) ActivateAiSelectMenu();
                else ActivateSpaceOrFireAction();
            }
            /* J1 fire shares the left mouse button line on Amiga hardware, so
             * title-screen SELECTDOWN must not start/activate menus.  Let
             * PollJoysticks consume the fire press and enable J1 instead.
             * J1 second fire/RMB arrives as MENUDOWN and can start/advance.
             */
        }
    }
}


static void DrainWindowMessages(void)
{
    struct IntuiMessage *msg;

    if (!win || !win->UserPort) return;

    while ((msg = (struct IntuiMessage *)GetMsg(win->UserPort))) {
        ReplyMsg((struct Message *)msg);
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

    InitTitleEffectQuality();

    audioSilenceWord = (UWORD *)AllocMem(sizeof(UWORD), MEMF_CHIP | MEMF_PUBLIC | MEMF_CLEAR);
    if (!audioSilenceWord) {
        printf("Could not allocate chip RAM for audio silence guard; one-shot samples may repeat\n");
    }

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
        renderRP.BitMap = NULL;
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    InitRastPort(&renderRP);
    renderRP.BitMap = renderBM;

    InitRastPort(&roomRP);
    roomRP.BitMap = roomBM;

    if (!InitRoundStartOverlayCache()) {
        printf("Round-start overlay cache allocation failed; simple countdown text fallback enabled\n");
    }

    if (!InitTileCache()) {
        FreeRoundStartOverlayCache();
        FreeBitMap(roomBM);
        roomBM = NULL;
        roomRP.BitMap = NULL;
        FreeBitMap(renderBM);
        renderBM = NULL;
        renderRP.BitMap = NULL;
        CloseScreen(scr);
        scr = NULL;
        return FALSE;
    }

    if (!InitRobotBobs()) {
        printf("Robot BOB cache allocation failed; fallback drawing enabled\n");
    }

    if (!AllocTitleStaticCache()) {
        printf("Could not reserve title static cache before menu music load; direct title rendering fallback active\n");
    }

    if (!LoadIntroTitleImage()) {
        printf("Optional PROGDIR:tiles/robovac-title.iff not loaded; starting at menu\n");
    }

    LoadMenuMusicStream();
    LoadRoundStartSamples();
    LoadGameplaySamples();

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
        CloseGameScreen();
        return FALSE;
    }

    return TRUE;
}


static void CloseGameScreen(void)
{
    FreeGameplaySamples();
    FreeRoundStartSamples();
    FreeMenuMusicSample();
    FreeTitleCopperGradient();

    if (win) {
        DrainWindowMessages();
        CloseWindow(win);
        win = NULL;
    }

    FreeIntroTitleImage();
    FreeTitleStaticCache();
    FreeTitleCarouselCache();
    FreeBonusBossCache();
    FreeBoltCache();
    FreeRobotScaledCache();
    FreeSpeedTrailCache();
    FreePuckCache();

    if (robotCacheBM) {
        FreeBitMap(robotCacheBM);
        robotCacheBM = NULL;
    }

    if (robotMaskBM) {
        FreeBitMap(robotMaskBM);
        robotMaskBM = NULL;
    }
    robotRP.BitMap = NULL;

    FreeRoundStartOverlayCache();

    if (tileCacheBM) {
        FreeBitMap(tileCacheBM);
        tileCacheBM = NULL;
    }
    tileRP.BitMap = NULL;

    if (roomBM) {
        FreeBitMap(roomBM);
        roomBM = NULL;
    }
    roomRP.BitMap = NULL;

    if (renderBM) {
        FreeBitMap(renderBM);
        renderBM = NULL;
    }
    renderRP.BitMap = NULL;

    if (scr) {
        CloseScreen(scr);
        scr = NULL;
    }

    if (audioSilenceWord) {
        FreeMem(audioSilenceWord, sizeof(UWORD));
        audioSilenceWord = NULL;
    }
}


int main(void)
{
    printf("RoboVac Rescue smooth AI prototype starting...\n");
    SeedRandom();

    if (!OpenGameScreen()) {
        return 20;
    }

    gameState = introTitleBM ? GAME_INTRO : GAME_TITLE;
    if (gameState == GAME_TITLE) {
        PrepareTitlePresentation();
    }

    while (running) {
        PollWindowMessages();
        PollJoysticks();

        if (gameState == GAME_TITLE && !aiDifficultyMenuOpen && !aiSelectMenuOpen) {
            titleIdleTicks++;
            if (titleIdleTicks >= TITLE_IDLE_DEMO_TICKS) {
                titleIdleTicks = 0;
                StartDemoMode();
            }
        } else {
            titleIdleTicks = 0;
        }

        StepGame();
        DrawFrame();
        WaitTOF();
        PresentFrame();
        ServiceTitleMusicForState();
    }

    CloseGameScreen();

    printf("RoboVac Rescue ended.\n");
    return 0;
}

