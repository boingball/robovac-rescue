#include "robovac.h"



static void AudioSafeWait(void)
{
    volatile WORD i;

    for (i = 0; i < 64; i++) {
        ;
    }
}


static void AudioDmaLatchWait(void)
{
    volatile WORD i;

    for (i = 0; i < 1024; i++) {
        ;
    }
}


static void AudioPrepareChannel(WORD channel, UBYTE owner)
{
    UWORD dmaBit;

    if (channel < 0 || channel > 3) return;

    dmaBit = AudioDmaBit(channel);
    custom.dmacon = dmaBit;
    custom.aud[channel].ac_vol = 0;
    AudioSafeWait();
    custom.aud[channel].ac_len = 1;
    if (audioSilenceWord) custom.aud[channel].ac_ptr = audioSilenceWord;
    audioChannelOwner[channel] = owner;
}


static void AudioReleaseChannel(WORD channel, UBYTE owner)
{
    UWORD dmaBit;

    if (channel < 0 || channel > 3) return;

    dmaBit = AudioDmaBit(channel);
    custom.dmacon = dmaBit;
    custom.aud[channel].ac_vol = 0;
    AudioSafeWait();
    custom.aud[channel].ac_len = 1;
    if (audioSilenceWord) custom.aud[channel].ac_ptr = audioSilenceWord;
    if (audioChannelOwner[channel] == owner || owner == AUDIO_OWNER_NONE) {
        audioChannelOwner[channel] = AUDIO_OWNER_NONE;
    }
}


static UBYTE SampleAudioOwner(struct OneShotSample *sample)
{
    if (sample == &menuMusicSample) return AUDIO_OWNER_MENU_MUSIC;
    if (sample == &getReadySample || sample == &countdownSample || sample == &goSample) return AUDIO_OWNER_ROUND_VOICE;
    if (sample == &mainMusicSample) return AUDIO_OWNER_MAIN_MUSIC;
    if (sample == &hooverMoveSample) return AUDIO_OWNER_HOOVER_LOOP;
    if (sample == &boltFireSample) return AUDIO_OWNER_BOLT_FIRE;
    if (sample == &goalSample) return AUDIO_OWNER_GOAL;
    return AUDIO_OWNER_NONE;
}


static void StopOneShotSample(struct OneShotSample *sample, WORD channel)
{
    if (!sample->loaded && !sample->playing) return;

    AudioReleaseChannel(channel, SampleAudioOwner(sample));
    sample->playing = FALSE;
    sample->ticksRemaining = 0;
}


static void PlayOneShotSample(struct OneShotSample *sample, WORD channel)
{
    UWORD dmaBit;
    UBYTE owner;

    if (!sample->loaded || !sample->data || sample->lengthWords == 0) return;

    owner = SampleAudioOwner(sample);
    AudioPrepareChannel(channel, owner);
    dmaBit = AudioDmaBit(channel);
    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;
    custom.aud[channel].ac_per = sample->period;
    custom.aud[channel].ac_vol = sample->volume;
    custom.dmacon = DMAF_SETCLR | dmaBit;
    AudioDmaLatchWait();

    if (audioSilenceWord) {
        custom.aud[channel].ac_ptr = audioSilenceWord;
        custom.aud[channel].ac_len = 1;
    }

    sample->ticksRemaining = sample->playbackTicks;
    sample->playing = TRUE;
}


static void StepOneShotSample(struct OneShotSample *sample, WORD channel)
{
    if (!sample->playing) return;
    if (sample->ticksRemaining > 0) sample->ticksRemaining--;
    if (sample->ticksRemaining <= 0) StopOneShotSample(sample, channel);
}


static void PlayLoopedSample(struct OneShotSample *sample, WORD channel)
{
    UWORD dmaBit;

    if (!sample->loaded || !sample->data || sample->lengthWords == 0) return;

    AudioPrepareChannel(channel, SampleAudioOwner(sample));
    dmaBit = AudioDmaBit(channel);
    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;
    custom.aud[channel].ac_per = sample->period;
    custom.aud[channel].ac_vol = sample->volume;
    custom.dmacon = DMAF_SETCLR | dmaBit;
    AudioDmaLatchWait();

    if (sample->loopLengthWords > 0) {
        custom.aud[channel].ac_ptr = (UWORD *)(sample->data + ((ULONG)sample->loopStartWords * 2UL));
        custom.aud[channel].ac_len = sample->loopLengthWords;
    } else {
        custom.aud[channel].ac_ptr = (UWORD *)sample->data;
        custom.aud[channel].ac_len = sample->lengthWords;
    }

    sample->ticksRemaining = 0;
    sample->playing = TRUE;
}


static void PlayFullLoopedSample(struct OneShotSample *sample, WORD channel)
{
    UWORD dmaBit;

    if (!sample->loaded || !sample->data || sample->lengthWords == 0) return;

    AudioPrepareChannel(channel, SampleAudioOwner(sample));
    dmaBit = AudioDmaBit(channel);
    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;
    custom.aud[channel].ac_per = sample->period;
    custom.aud[channel].ac_vol = sample->volume;
    custom.dmacon = DMAF_SETCLR | dmaBit;
    AudioDmaLatchWait();

    custom.aud[channel].ac_ptr = (UWORD *)sample->data;
    custom.aud[channel].ac_len = sample->lengthWords;

    sample->ticksRemaining = 0;
    sample->playing = TRUE;
}


static void StopGetReadySample(void)
{
    StopOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
}


static void StopCountdownSample(void)
{
    StopOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
}


static void StopGoSample(void)
{
    StopOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}


static void StopRoundStartSamples(void)
{
    StopGetReadySample();
    StopCountdownSample();
    StopGoSample();
}


static void StartRoundCountdownAudio(void)
{
    StopGameplaySamples();
    StopMenuMusic();
    StopRoundStartSamples();
    roundCountdownLastSoundNumber = 0;
    PlayGetReadySample();
}


static void PlayGetReadySample(void)
{
    PlayOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
}


static void PlayCountdownSample(void)
{
    StopCountdownSample();
    PlayOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
    /* countdown.8svx is one long dong. Give each number one PAL countdown
     * step, so the same sample is deliberately struck once for 3, 2, and 1
     * instead of letting a single sample get clipped at the end of the
     * countdown. */
    if (countdownSample.playing && countdownSample.ticksRemaining > ROUND_COUNTDOWN_STEP_FRAMES) {
        countdownSample.ticksRemaining = ROUND_COUNTDOWN_STEP_FRAMES;
    }
}


static void PlayGoSample(void)
{
    StopRoundStartSamples();
    PlayOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}


static void StepRoundStartSamples(void)
{
    StepOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
    StepOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
    StepOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}


static void StartMainGameMusic(void)
{
    if (mainMusicSample.playing) return;
    StopMenuMusic();
    StopGetReadySample();
    StopCountdownSample();
    PlayLoopedSample(&mainMusicSample, MAIN_MUSIC_LEFT_AUDIO_CHANNEL);
    PlayLoopedSample(&mainMusicSample, MAIN_MUSIC_RIGHT_AUDIO_CHANNEL);
}


static void StopGameplaySamples(void)
{
    StopOneShotSample(&mainMusicSample, MAIN_MUSIC_LEFT_AUDIO_CHANNEL);
    StopOneShotSample(&mainMusicSample, MAIN_MUSIC_RIGHT_AUDIO_CHANNEL);
    StopOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
    StopOneShotSample(&boltFireSample, BOLT_FIRE_AUDIO_CHANNEL);
    StopOneShotSample(&goalSample, GOAL_AUDIO_CHANNEL);
}


static void ServiceHooverMoveSample(void)
{
    if (AnyHooverMoving()) {
        if (!hooverMoveSample.playing) {
            StopOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
            PlayLoopedSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
        }
    } else if (hooverMoveSample.playing) {
        StopOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
    }
}


static void PlayBoltFireSample(void)
{
    PlayOneShotSample(&boltFireSample, BOLT_FIRE_AUDIO_CHANNEL);
}


static void PlayGoalSample(void)
{
    PlayOneShotSample(&goalSample, GOAL_AUDIO_CHANNEL);
}


static void FreeOneShotSample(struct OneShotSample *sample, WORD channel)
{
    StopOneShotSample(sample, channel);
    if (sample->data) {
        FreeMem(sample->data, sample->dataSize);
    }
    memset(sample, 0, sizeof(*sample));
}


static BOOL LoadOneShotSample(const char *path, struct OneShotSample *sample, const char *description)
{
    BPTR fh;
    LONG size;
    LONG readSize;
    UBYTE *fileData;
    LONG pos;
    LONG bodyOffset = -1;
    LONG bodySize = 0;
    UWORD sampleRate = 0;
    UBYTE compression = 0;
    ULONG volume = 0x10000UL;
    ULONG oneShotSamples = 0;
    ULONG repeatSamples = 0;
    LONG allocSize;
    ULONG playbackTicks;

    memset(sample, 0, sizeof(*sample));

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        printf("Optional %s not loaded; %s disabled\n", path, description);
        return FALSE;
    }

    size = GetFileSize(fh);
    if (size < 20) {
        Close(fh);
        printf("%s is too small for an 8SVX sample\n", path);
        return FALSE;
    }

    fileData = (UBYTE *)AllocMem(size, MEMF_PUBLIC | MEMF_CLEAR);
    if (!fileData) {
        Close(fh);
        printf("Could not allocate memory for %s\n", path);
        return FALSE;
    }

    readSize = Read(fh, fileData, size);
    Close(fh);
    if (readSize != size) {
        FreeMem(fileData, size);
        printf("Could not read %s\n", path);
        return FALSE;
    }

    if (memcmp(fileData, "FORM", 4) != 0 || memcmp(fileData + 8, "8SVX", 4) != 0) {
        FreeMem(fileData, size);
        printf("%s is not an IFF 8SVX sample\n", path);
        return FALSE;
    }

    pos = 12;
    while (pos + 8 <= size) {
        ULONG chunkSize = ReadBE32(fileData + pos + 4);
        LONG dataPos = pos + 8;
        if (dataPos + (LONG)chunkSize > size) break;

        if (memcmp(fileData + pos, "VHDR", 4) == 0 && chunkSize >= 20) {
            oneShotSamples = ReadBE32(fileData + dataPos);
            repeatSamples = ReadBE32(fileData + dataPos + 4);
            sampleRate = ReadBE16(fileData + dataPos + 12);
            compression = fileData[dataPos + 15];
            volume = ReadBE32(fileData + dataPos + 16);
        } else if (memcmp(fileData + pos, "BODY", 4) == 0) {
            bodyOffset = dataPos;
            bodySize = chunkSize;
        }

        pos = dataPos + (LONG)chunkSize + (chunkSize & 1);
    }

    if (bodyOffset < 0 || bodySize <= 1 || sampleRate == 0 || compression != 0) {
        FreeMem(fileData, size);
        printf("%s is not a playable uncompressed 8SVX sample\n", path);
        return FALSE;
    }

    allocSize = bodySize + (bodySize & 1);
    sample->data = (UBYTE *)AllocMem(allocSize, MEMF_CHIP | MEMF_PUBLIC | MEMF_CLEAR);
    if (!sample->data) {
        FreeMem(fileData, size);
        printf("Could not allocate chip RAM for %s\n", path);
        return FALSE;
    }

    CopyMem(fileData + bodyOffset, sample->data, bodySize);
    FreeMem(fileData, size);

    sample->dataSize = allocSize;
    sample->lengthWords = (UWORD)(allocSize / 2);
    sample->period = (UWORD)(PAULA_CLOCK_HZ / sampleRate);
    if (sample->period < 124) sample->period = 124;
    sample->sampleRate = (UWORD)sampleRate;
    playbackTicks = (((ULONG)bodySize * 50UL) + (ULONG)sampleRate - 1UL) / (ULONG)sampleRate;
    if (playbackTicks < 1UL) playbackTicks = 1UL;
    if (playbackTicks > 32767UL) playbackTicks = 32767UL;
    sample->playbackTicks = (UWORD)playbackTicks;
    sample->ticksRemaining = 0;
    if (repeatSamples > 1UL && repeatSamples < (ULONG)bodySize) {
        ULONG loopStartBytes;
        ULONG loopLengthBytes;

        if (oneShotSamples + repeatSamples <= (ULONG)bodySize) {
            loopStartBytes = oneShotSamples;
        } else {
            loopStartBytes = (ULONG)bodySize - repeatSamples;
        }
        loopLengthBytes = repeatSamples;
        loopStartBytes &= ~1UL;
        loopLengthBytes &= ~1UL;
        if (loopLengthBytes >= 2UL && loopStartBytes + loopLengthBytes <= (ULONG)allocSize) {
            sample->loopStartWords = (UWORD)(loopStartBytes / 2UL);
            sample->loopLengthWords = (UWORD)(loopLengthBytes / 2UL);
        }
    }
    sample->volume = (volume >= 0x10000UL) ? 64 : (UBYTE)((volume * 64UL) / 0x10000UL);
    if (sample->volume == 0) sample->volume = 64;
    sample->loaded = TRUE;
    return TRUE;
}


static BOOL LoadRoundStartSamples(void)
{
    BOOL loaded = FALSE;

    loaded |= LoadOneShotSample(GET_READY_SAMPLE_PATH, &getReadySample, "round countdown voice");
    loaded |= LoadOneShotSample(COUNTDOWN_SAMPLE_PATH, &countdownSample, "countdown number voice");
    loaded |= LoadOneShotSample(GO_SAMPLE_PATH, &goSample, "go voice");
    return loaded;
}


static void FreeRoundStartSamples(void)
{
    FreeOneShotSample(&getReadySample, GET_READY_AUDIO_CHANNEL);
    FreeOneShotSample(&countdownSample, COUNTDOWN_AUDIO_CHANNEL);
    FreeOneShotSample(&goSample, GO_AUDIO_CHANNEL);
}


/* Keep the complete 8SVX BODY in ordinary/Fast RAM, but feed Paula through
 * two small chip-RAM buffers. Paula itself tells us when a block has finished
 * through INTREQR, so no guessed 50 Hz chunk timer is involved and AUDxLEN
 * never has to describe more than one legal DMA block. */
static BOOL LoadMenuMusicStream(void)
{
    BPTR fh;
    UBYTE header[12];
    UBYTE chunkHeader[8];
    LONG fileSize;
    LONG pos;
    LONG bodyStart = 0;
    ULONG bodySize = 0;
    UWORD sampleRate = 0;
    UBYTE compression = 0;
    ULONG volume = 0x10000UL;
    BOOL haveVHDR = FALSE;
    BOOL haveBODY = FALSE;
    ULONG allocSize;
    UBYTE *data;
    ULONG got;
    LONG readSize;
    WORD i;

    memset(&menuMusicSample, 0, sizeof(menuMusicSample));
    menuMusicSourceBytes = 0;
    menuMusicNextOffsetBytes = 0;
    menuMusicCurrentBuf = 0;

    fh = Open((STRPTR)MENU_MUSIC_SAMPLE_PATH, MODE_OLDFILE);
    if (!fh) {
        printf("Optional %s not loaded; menu music disabled\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    fileSize = GetFileSize(fh);
    if (fileSize < 20 || Read(fh, header, 12) != 12 ||
        memcmp(header, "FORM", 4) != 0 || memcmp(header + 8, "8SVX", 4) != 0) {
        Close(fh);
        printf("%s is not an IFF 8SVX sample\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    pos = 12;
    while (pos + 8 <= fileSize) {
        ULONG chunkSize;
        LONG dataPos;

        if (Seek(fh, pos, OFFSET_BEGINNING) == -1) break;
        if (Read(fh, chunkHeader, 8) != 8) break;
        chunkSize = ReadBE32(chunkHeader + 4);
        dataPos = pos + 8;
        if (dataPos + (LONG)chunkSize > fileSize) break;

        if (memcmp(chunkHeader, "VHDR", 4) == 0 && chunkSize >= 20) {
            UBYTE vhdr[20];
            if (Read(fh, vhdr, 20) == 20) {
                sampleRate = ReadBE16(vhdr + 12);
                compression = vhdr[15];
                volume = ReadBE32(vhdr + 16);
                haveVHDR = TRUE;
            }
        } else if (memcmp(chunkHeader, "BODY", 4) == 0) {
            bodyStart = dataPos;
            bodySize = chunkSize;
            haveBODY = TRUE;
            break;
        }

        pos = dataPos + (LONG)chunkSize + ((LONG)chunkSize & 1);
    }

    if (!haveVHDR || !haveBODY || bodySize <= 1 || sampleRate == 0 || compression != 0) {
        Close(fh);
        printf("%s is not a playable uncompressed 8SVX sample\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    /* Reserve the DMA-visible buffers first so a no-Fast-RAM fallback cannot
     * consume all chip RAM before Paula gets its two streaming buffers. */
    for (i = 0; i < 2; i++) {
        if (!menuMusicChunkBuf[i]) {
            menuMusicChunkBuf[i] = (UBYTE *)AllocMem(MENU_MUSIC_STREAM_CHUNK_BYTES,
                                                     MEMF_CHIP | MEMF_PUBLIC);
        }
    }
    if (!menuMusicChunkBuf[0] || !menuMusicChunkBuf[1]) {
        Close(fh);
        FreeMenuMusicSample();
        printf("Could not allocate chip RAM buffers for %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    allocSize = bodySize & ~1UL;
    data = (UBYTE *)AllocMem(allocSize, MEMF_FAST | MEMF_PUBLIC);
    if (!data) {
        /* Machines with no Fast RAM can still try the old all-public-memory
         * route; the two chip streaming buffers are already safely reserved. */
        data = (UBYTE *)AllocMem(allocSize, MEMF_PUBLIC);
    }
    if (!data) {
        Close(fh);
        FreeMenuMusicSample();
        printf("Could not allocate source RAM for %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }
    menuMusicSample.data = data;
    menuMusicSample.dataSize = (LONG)allocSize;

    if (Seek(fh, bodyStart, OFFSET_BEGINNING) == -1) {
        Close(fh);
        FreeMenuMusicSample();
        printf("Could not read %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    got = 0;
    while (got < allocSize) {
        readSize = Read(fh, data + got, (LONG)(allocSize - got));
        if (readSize <= 0) break;
        got += (ULONG)readSize;
    }
    Close(fh);
    got &= ~1UL;

    if (got < 2UL) {
        FreeMenuMusicSample();
        printf("Could not read %s\n", MENU_MUSIC_SAMPLE_PATH);
        return FALSE;
    }

    menuMusicSourceBytes = got;
    /* lengthWords deliberately stays zero: the source is not itself a legal
     * Paula DMA block and must never be handed to PlayLoopedSample(). */
    menuMusicSample.lengthWords = 0;
    menuMusicSample.period = (UWORD)(PAULA_CLOCK_HZ / sampleRate);
    if (menuMusicSample.period < 124) menuMusicSample.period = 124;
    menuMusicSample.sampleRate = sampleRate;
    menuMusicSample.volume = (volume >= 0x10000UL) ? 64 : (UBYTE)((volume * 64UL) / 0x10000UL);
    if (menuMusicSample.volume == 0) menuMusicSample.volume = 64;
    menuMusicSample.loaded = TRUE;
    return TRUE;
}


static UWORD FillMenuMusicChunk(WORD bufferIndex)
{
    ULONG remaining;
    ULONG chunkBytes;

    if (bufferIndex < 0 || bufferIndex > 1) return 0;
    if (!menuMusicSample.loaded || !menuMusicSample.data || !menuMusicChunkBuf[bufferIndex]) return 0;
    if (menuMusicSourceBytes < 2UL) return 0;

    if (menuMusicNextOffsetBytes >= menuMusicSourceBytes) menuMusicNextOffsetBytes = 0;
    remaining = menuMusicSourceBytes - menuMusicNextOffsetBytes;
    chunkBytes = (remaining > MENU_MUSIC_STREAM_CHUNK_BYTES) ?
                 MENU_MUSIC_STREAM_CHUNK_BYTES : remaining;
    chunkBytes &= ~1UL;

    if (chunkBytes < 2UL) {
        menuMusicNextOffsetBytes = 0;
        remaining = menuMusicSourceBytes;
        chunkBytes = (remaining > MENU_MUSIC_STREAM_CHUNK_BYTES) ?
                     MENU_MUSIC_STREAM_CHUNK_BYTES : remaining;
        chunkBytes &= ~1UL;
    }
    if (chunkBytes < 2UL) return 0;

    CopyMem(menuMusicSample.data + menuMusicNextOffsetBytes,
            menuMusicChunkBuf[bufferIndex], chunkBytes);
    menuMusicNextOffsetBytes += chunkBytes;
    if (menuMusicNextOffsetBytes >= menuMusicSourceBytes) menuMusicNextOffsetBytes = 0;

    return (UWORD)(chunkBytes / 2UL);
}


static void StopMenuMusic(void)
{
    StopOneShotSample(&menuMusicSample, MENU_MUSIC_LEFT_AUDIO_CHANNEL);
    StopOneShotSample(&menuMusicSample, MENU_MUSIC_RIGHT_AUDIO_CHANNEL);
    custom.intreq = MENU_MUSIC_INT_MASK;

    if (menuMusicIntenaSaved) {
        if (menuMusicSavedAudioIntena != 0) {
            custom.intena = INTF_SETCLR | menuMusicSavedAudioIntena;
        }
        menuMusicSavedAudioIntena = 0;
        menuMusicIntenaSaved = FALSE;
    }

    menuMusicNextOffsetBytes = 0;
    menuMusicCurrentBuf = 0;
}


static void StartMenuMusic(void)
{
    UWORD firstWords;
    UWORD secondWords;
    UWORD leftDmaBit;
    UWORD rightDmaBit;

    if (menuMusicSample.playing) return;
    if (!menuMusicSample.loaded || !menuMusicSample.data || menuMusicSourceBytes < 2UL) return;
    if (!menuMusicChunkBuf[0] || !menuMusicChunkBuf[1]) return;

    menuMusicNextOffsetBytes = 0;
    firstWords = FillMenuMusicChunk(0);
    secondWords = FillMenuMusicChunk(1);
    if (firstWords == 0 || secondWords == 0) return;

    menuMusicSavedAudioIntena = custom.intenar & MENU_MUSIC_INT_MASK;
    menuMusicIntenaSaved = TRUE;
    /* Poll INTREQR ourselves; do not let a level-4 audio ISR consume the
     * block-finished flag before the title loop sees it. */
    custom.intena = MENU_MUSIC_INT_MASK;
    custom.intreq = MENU_MUSIC_INT_MASK;

    AudioPrepareChannel(MENU_MUSIC_LEFT_AUDIO_CHANNEL, AUDIO_OWNER_MENU_MUSIC);
    AudioPrepareChannel(MENU_MUSIC_RIGHT_AUDIO_CHANNEL, AUDIO_OWNER_MENU_MUSIC);
    leftDmaBit = AudioDmaBit(MENU_MUSIC_LEFT_AUDIO_CHANNEL);
    rightDmaBit = AudioDmaBit(MENU_MUSIC_RIGHT_AUDIO_CHANNEL);

    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[0];
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_len = firstWords;
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_per = menuMusicSample.period;
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_vol = menuMusicSample.volume;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[0];
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_len = firstWords;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_per = menuMusicSample.period;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_vol = menuMusicSample.volume;

    custom.dmacon = DMAF_SETCLR | leftDmaBit | rightDmaBit;
    AudioDmaLatchWait();

    /* Paula has latched buffer 0. These visible pointer/length registers are
     * now the reload block that becomes active when buffer 0 finishes. */
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[1];
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_len = secondWords;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[1];
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_len = secondWords;

    /* Discard any start-up request; the next pair of flags means block 0 has
     * genuinely completed on both stereo channels. */
    custom.intreq = MENU_MUSIC_INT_MASK;
    menuMusicCurrentBuf = 0;
    menuMusicSample.playing = TRUE;
}


static void ServiceMenuMusicStream(void)
{
    UWORD pending;
    WORD refillBuf;
    UWORD words;

    if (!menuMusicSample.playing) return;

    pending = custom.intreqr & MENU_MUSIC_INT_MASK;
    /* Both channels play the same buffer. Wait until both have crossed the
     * boundary before overwriting the buffer they just stopped using. */
    if (pending != MENU_MUSIC_INT_MASK) return;
    custom.intreq = MENU_MUSIC_INT_MASK;

    menuMusicCurrentBuf ^= 1;
    refillBuf = menuMusicCurrentBuf ^ 1;
    words = FillMenuMusicChunk(refillBuf);
    if (words == 0) {
        StopMenuMusic();
        return;
    }

    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[refillBuf];
    custom.aud[MENU_MUSIC_LEFT_AUDIO_CHANNEL].ac_len = words;
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_ptr = (UWORD *)menuMusicChunkBuf[refillBuf];
    custom.aud[MENU_MUSIC_RIGHT_AUDIO_CHANNEL].ac_len = words;
}


static void ServiceMenuMusicForState(void)
{
    if (gameState == GAME_INTRO || gameState == GAME_TITLE) {
        StartMenuMusic();
        ServiceMenuMusicStream();
    } else if (menuMusicSample.playing) {
        StopMenuMusic();
    }
}


static void FreeMenuMusicSample(void)
{
    WORD i;

    StopMenuMusic();
    if (menuMusicSample.data) {
        FreeMem(menuMusicSample.data, menuMusicSample.dataSize);
    }
    for (i = 0; i < 2; i++) {
        if (menuMusicChunkBuf[i]) {
            FreeMem(menuMusicChunkBuf[i], MENU_MUSIC_STREAM_CHUNK_BYTES);
            menuMusicChunkBuf[i] = NULL;
        }
    }
    menuMusicSourceBytes = 0;
    menuMusicNextOffsetBytes = 0;
    menuMusicCurrentBuf = 0;
    memset(&menuMusicSample, 0, sizeof(menuMusicSample));
}


static BOOL LoadGameplaySamples(void)
{
    BOOL loaded = FALSE;

    loaded |= LoadOneShotSample(MAIN_MUSIC_SAMPLE_PATH, &mainMusicSample, "main game music");
    loaded |= LoadOneShotSample(BOLT_FIRE_SAMPLE_PATH, &boltFireSample, "bolt fire effect");
    loaded |= LoadOneShotSample(GOAL_SAMPLE_PATH, &goalSample, "goal scored effect");
    loaded |= LoadOneShotSample(HOOVER_MOVE_SAMPLE_PATH, &hooverMoveSample, "hoover movement loop");
    if (hooverMoveSample.loaded) {
        hooverMoveSample.lengthWords = (UWORD)((hooverMoveSample.dataSize & ~1L) / 2L);
        if (hooverMoveSample.loopLengthWords > 0 &&
            ((ULONG)hooverMoveSample.loopStartWords + (ULONG)hooverMoveSample.loopLengthWords >
             (ULONG)hooverMoveSample.lengthWords)) {
            hooverMoveSample.loopStartWords = 0;
            hooverMoveSample.loopLengthWords = hooverMoveSample.lengthWords;
        }
    }
    return loaded;
}


static void FreeGameplaySamples(void)
{
    StopOneShotSample(&mainMusicSample, MAIN_MUSIC_RIGHT_AUDIO_CHANNEL);
    FreeOneShotSample(&mainMusicSample, MAIN_MUSIC_LEFT_AUDIO_CHANNEL);
    FreeOneShotSample(&hooverMoveSample, HOOVER_MOVE_AUDIO_CHANNEL);
    FreeOneShotSample(&boltFireSample, BOLT_FIRE_AUDIO_CHANNEL);
    FreeOneShotSample(&goalSample, GOAL_AUDIO_CHANNEL);
}


static UWORD ReadBE16(const UBYTE *p)
{
    return (UWORD)(((UWORD)p[0] << 8) | p[1]);
}


static LONG GetFileSize(BPTR fh)
{
    LONG size;

    if (Seek(fh, 0, OFFSET_END) == -1) return -1;
    size = Seek(fh, 0, OFFSET_CURRENT);
    if (size == -1) return -1;
    if (Seek(fh, 0, OFFSET_BEGINNING) == -1) return -1;
    return size;
}



static ULONG ReadBE32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}


static UWORD AudioDmaBit(WORD channel)
{
    switch (channel) {
        case 0: return DMAF_AUD0;
        case 1: return DMAF_AUD1;
        case 2: return DMAF_AUD2;
        default: return DMAF_AUD3;
    }
}


static void ServiceTitleMusicForState(void)
{
    if (gameState == GAME_TITLE && !titleFirstFullPresentDone) return;
    ServiceMenuMusicForState();
}