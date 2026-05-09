#pragma once

#define FORCE_INLINE __attribute__((always_inline)) inline

// Dicas para o Branch Predictor do GCC: Otimização extrema do Pipeline Xtensa
#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// O Switch do LoopMode foi retirado do "caminho quente". Ele só será processado 
// naquele microsegundo em que o sample cruzar a borda exata.
#define  ADVANCE_SAMPLE_POS \
    if (dir) { \
        pos += inc; \
        if (UNLIKELY((pos >> 16) >= lEnd)) { \
            if (vo->sampleLoopMode == LOOP_FORWARD) pos -= ((uint64_t)(lEnd - lStart) << 16); \
            else if (vo->sampleLoopMode == LOOP_PINGPONG) { dir = false; pos = ((uint64_t)lEnd << 16) - (pos - ((uint64_t)lEnd << 16)); } \
            else { vo->sampleFinished = true; break; } \
        } \
    } else { \
        if (LIKELY(pos >= inc)) pos -= inc; else pos = 0; \
        if (UNLIKELY((pos >> 16) <= lStart)) { \
            if (vo->sampleLoopMode == LOOP_PINGPONG) { dir = true; pos = ((uint64_t)lStart << 16) + (((uint64_t)lStart << 16) - pos); } \
            else if (vo->sampleLoopMode == LOOP_REVERSE) pos += ((uint64_t)(lEnd - lStart) << 16); \
            else { vo->sampleFinished = true; break; } \
        } \
    }

// Render: PCM Sample
static FORCE_INLINE IRAM_ATTR void renderBlockSample(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->sampleFinished) return;
    const SampleData* sData = &registeredSamples[vo->curSampleId];
    if (!sData->data) return;

    const uint32_t len = sData->length;
    uint64_t pos = vo->samplePos1616;
    const uint32_t inc = vo->sampleInc1616;
    const uint32_t lStart = vo->sampleLoopStart;
    const uint32_t lEnd = (vo->sampleLoopEnd > 0 && vo->sampleLoopEnd <= len) ? vo->sampleLoopEnd : len;
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    bool dir = vo->sampleDirection;

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        
        switch (sData->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    mixBuffer[i] += (data[(uint32_t)(pos >> 16)] * finalVol) >> 16;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    mixBuffer[i] += ((((int16_t)data[(uint32_t)(pos >> 16)] - 128) << 8) * finalVol) >> 16;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx = (uint32_t)(pos >> 16);
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
        }
    } else {
        switch (sData->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i] += (data[(uint32_t)(pos >> 16)] * finalVol) >> 16;
                    currentEnv += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i] += ((((int16_t)data[(uint32_t)(pos >> 16)] - 128) << 8) * finalVol) >> 16;
                    currentEnv += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    uint32_t idx = (uint32_t)(pos >> 16);
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    currentEnv += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
        }
    }
    vo->samplePos1616 = pos;
    vo->sampleDirection = dir;
}

// Render: Wavetable
static FORCE_INLINE IRAM_ATTR void renderBlockWavetable(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (!vo->wtData) return;

    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    const uint32_t size = vo->wtSize;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    v4u32 vInc = {0, inc, inc * 2, inc * 3};
    v4u32 vIncStep = {inc * 4, inc * 4, inc * 4, inc * 4};
    v4i32 vEnvStep = {0, envStep, envStep * 2, envStep * 3};
    v4i32 vEnvStep4 = {envStep * 4, envStep * 4, envStep * 4, envStep * 4};
    v4i32 vEnv = {currentEnv, currentEnv, currentEnv, currentEnv};
    vEnv += vEnvStep;
    v4u32 vPh = {ph, ph, ph, ph};
    vPh += vInc;
#endif

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc * samples; return; }

        switch (vo->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)vo->wtData;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                v4i32 vVolVec = {finalVol, finalVol, finalVol, finalVol};
                for (int i = 0; i < samples; i += 4) {
                    v4u32 vIdx = ((vPh >> 16) * size) >> 16;
                    v4i32 vals = { data[vIdx[0]], data[vIdx[1]], data[vIdx[2]], data[vIdx[3]] };
                    *(v4i32*)&mixBuffer[i] += (vals * vVolVec) >> 16;
                    vPh += vIncStep;
                }
                ph += inc * samples;
#else
                // OTIMIZAÇÃO: Substituição de 64-bits por 32-bits purista. 
                for (int i = 0; i < samples; i++) { mixBuffer[i] += (data[((ph >> 16) * size) >> 16] * finalVol) >> 16; ph += inc; }
#endif
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                v4i32 vVolVec = {finalVol, finalVol, finalVol, finalVol};
                for (int i = 0; i < samples; i += 4) {
                    v4u32 vIdx = ((vPh >> 16) * size) >> 16;
                    v4i32 vals = { (int32_t)data[vIdx[0]], (int32_t)data[vIdx[1]], (int32_t)data[vIdx[2]], (int32_t)data[vIdx[3]] };
                    vals = (vals - 128) << 8;
                    *(v4i32*)&mixBuffer[i] += (vals * vVolVec) >> 16;
                    vPh += vIncStep;
                }
                ph += inc * samples;
#else
                for (int i = 0; i < samples; i++) { mixBuffer[i] += ((((int16_t)data[((ph >> 16) * size) >> 16] - 128) << 8) * finalVol) >> 16; ph += inc; }
#endif
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx = ((ph >> 16) * size) >> 16;
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ph += inc;
                }
                break;
            }
        }
    } else {
        switch (vo->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)vo->wtData;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4u32 vIdx = ((vPh >> 16) * size) >> 16;
                    v4i32 vals = { data[vIdx[0]], data[vIdx[1]], data[vIdx[2]], data[vIdx[3]] };
                    *(v4i32*)&mixBuffer[i] += (vals * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    mixBuffer[i] += (data[((ph >> 16) * size) >> 16] * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4u32 vIdx = ((vPh >> 16) * size) >> 16;
                    v4i32 vals = { (int32_t)data[vIdx[0]], (int32_t)data[vIdx[1]], (int32_t)data[vIdx[2]], (int32_t)data[vIdx[3]] };
                    vals = (vals - 128) << 8;
                    *(v4i32*)&mixBuffer[i] += (vals * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    mixBuffer[i] += ((((int16_t)data[((ph >> 16) * size) >> 16] - 128) << 8) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx = ((ph >> 16) * size) >> 16;
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            }
        }
    }
    vo->phase = ph;
}

// Render: Basic Oscillators (Saw, Sine, Pulse, Tri)
static FORCE_INLINE IRAM_ATTR void renderBlockBasic(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    const WaveType type = vo->type;
    const uint32_t pw = vo->pulseWidth;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    v4u32 vInc = {0, inc, inc * 2, inc * 3};
    v4u32 vIncStep = {inc * 4, inc * 4, inc * 4, inc * 4};
    v4i32 vEnvStep = {0, envStep, envStep * 2, envStep * 3};
    v4i32 vEnvStep4 = {envStep * 4, envStep * 4, envStep * 4, envStep * 4};
    v4i32 vEnv = {currentEnv, currentEnv, currentEnv, currentEnv};
    vEnv += vEnvStep;
    v4u32 vPh = {ph, ph, ph, ph};
    vPh += vInc;
#endif

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc * samples; return; }

        switch (type) {
            case WAVE_SAW:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    v4i32 vVolVec = {finalVol, finalVol, finalVol, finalVol};
                    for (int i = 0; i < samples; i += 4) {
                        v4i32 shifted = (v4i32)(vPh >> 16);
                        shifted = (shifted << 16) >> 16;
                        *(v4i32*)&mixBuffer[i] += (shifted * vVolVec) >> 16;
                        vPh += vIncStep;
                    }
                    ph += inc * samples;
                }
#else
                for (int i = 0; i < samples; i++) { mixBuffer[i] += ((int16_t)(ph >> 16) * finalVol) >> 16; ph += inc; }
#endif
                break;
            case WAVE_SINE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    v4i32 vVolVec = {finalVol, finalVol, finalVol, finalVol};
                    for (int i = 0; i < samples; i += 4) {
                        v4i32 sines = { sineLUT[vPh[0] >> SINE_SHIFT], sineLUT[vPh[1] >> SINE_SHIFT], sineLUT[vPh[2] >> SINE_SHIFT], sineLUT[vPh[3] >> SINE_SHIFT] };
                        *(v4i32*)&mixBuffer[i] += (sines * vVolVec) >> 16;
                        vPh += vIncStep;
                    }
                    ph += inc * samples;
                }
#else
                for (int i = 0; i < samples; i++) { mixBuffer[i] += (sineLUT[ph >> SINE_SHIFT] * finalVol) >> 16; ph += inc; }
#endif
                break;
            case WAVE_PULSE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    int32_t sVolPos = (finalVol * 32767) >> 16;
                    v4u32 vPw = {pw, pw, pw, pw};
                    v4i32 vVolNeg = {-sVolPos, -sVolPos, -sVolPos, -sVolPos};
                    v4i32 vVolPos2 = {sVolPos * 2, sVolPos * 2, sVolPos * 2, sVolPos * 2}; 
                    
                    for (int i = 0; i < samples; i += 4) {
                        v4i32 diff = (v4i32)((vPh >> 1) - (vPw >> 1));
                        v4i32 mask = diff >> 31;
                        *(v4i32*)&mixBuffer[i] += vVolNeg + (mask & vVolPos2); 
                        vPh += vIncStep;
                    }
                    ph += inc * samples;
                }
#else
                for (int i = 0; i < samples; i++) { mixBuffer[i] += (((ph < pw) ? 32767 : -32767) * finalVol) >> 16; ph += inc; }
#endif
                break;
            case WAVE_TRIANGLE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    v4i32 vVolVec = {finalVol, finalVol, finalVol, finalVol};
                    for (int i = 0; i < samples; i += 4) {
                        v4i32 saw = (v4i32)(vPh >> 16);
                        saw = (saw << 16) >> 16; 
                        v4i32 sawMask = saw >> 31; 
                        v4i32 tri = (((saw ^ sawMask) * 2) - 32767);
                        *(v4i32*)&mixBuffer[i] += (tri * vVolVec) >> 16;
                        vPh += vIncStep;
                    }
                    ph += inc * samples;
                }
#else
                for (int i = 0; i < samples; i++) {
                    int16_t saw = (int16_t)(ph >> 16);
                    mixBuffer[i] += ((int16_t)(((saw ^ (saw >> 15)) * 2) - 32767) * finalVol) >> 16;
                    ph += inc;
                }
#endif
                break;
            default: break;
        }
    } else {
        switch (type) {
            case WAVE_SAW:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4i32 shifted = (v4i32)(vPh >> 16);
                    shifted = (shifted << 16) >> 16;
                    *(v4i32*)&mixBuffer[i] += (shifted * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i] += ((int16_t)(ph >> 16) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            case WAVE_SINE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4i32 sines = { sineLUT[vPh[0] >> SINE_SHIFT], sineLUT[vPh[1] >> SINE_SHIFT], sineLUT[vPh[2] >> SINE_SHIFT], sineLUT[vPh[3] >> SINE_SHIFT] };
                    *(v4i32*)&mixBuffer[i] += (sines * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i] += (sineLUT[ph >> SINE_SHIFT] * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            case WAVE_PULSE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    v4u32 vPw = {pw, pw, pw, pw};
                    for (int i = 0; i < samples; i += 4) {
                        v4i32 vEnvShifted = vEnv >> 14;
                        vEnvShifted &= ~(vEnvShifted >> 31);
                        v4i32 vFinalVol = (vEnvShifted * (int32_t)volBase) >> 14;
                        
                        v4i32 diff = (v4i32)((vPh >> 1) - (vPw >> 1));
                        v4i32 mask = diff >> 31;
                        v4i32 vVolPos = (vFinalVol * 32767) >> 16;
                        
                        *(v4i32*)&mixBuffer[i] += (-vVolPos) + (mask & (vVolPos * 2)); 
                        
                        vPh += vIncStep; vEnv += vEnvStep4;
                    }
                    ph += inc * samples; currentEnv += envStep * samples;
                }
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i] += (((ph < pw) ? 32767 : -32767) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            case WAVE_TRIANGLE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4i32 saw = (v4i32)(vPh >> 16);
                    saw = (saw << 16) >> 16; 
                    v4i32 sawMask = saw >> 31;
                    v4i32 tri = (((saw ^ sawMask) * 2) - 32767);
                    *(v4i32*)&mixBuffer[i] += (tri * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    int16_t saw = (int16_t)(ph >> 16);
                    mixBuffer[i] += ((int16_t)(((saw ^ (saw >> 15)) * 2) - 32767) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            default: break;
        }
    }
    vo->phase = ph;
}

// Render: Noise
static FORCE_INLINE IRAM_ATTR void renderBlockNoise(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t rng = vo->rngState;
    uint32_t ph = vo->phase;
    uint32_t inc = (vo->phaseInc + vo->vibOffset) << 4;
    int16_t currentSample = vo->noiseSample;

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc * samples; return; }

        for (int i = 0; i < samples; i++) {
            uint32_t nextPh = ph + inc;
            if (UNLIKELY(nextPh < ph)) { rng = (rng * 1664525) + 1013904223; currentSample = (int16_t)(rng >> 16); }
            ph = nextPh;
            mixBuffer[i] += (currentSample * finalVol) >> 16;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            uint32_t nextPh = ph + inc;
            if (UNLIKELY(nextPh < ph)) { rng = (rng * 1664525) + 1013904223; currentSample = (int16_t)(rng >> 16); }
            ph = nextPh;
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            mixBuffer[i] += (currentSample * finalVol) >> 16;
            currentEnv += envStep;
        }
    }
    vo->rngState = rng;
    vo->phase = ph;
    vo->noiseSample = currentSample;
}

// Render: Stream from RAM Buffer
static FORCE_INLINE IRAM_ATTR void renderBlockStream(Voice* __restrict vo, StreamTrack* __restrict streamsArr, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->streamTrackId < 0 || vo->streamTrackId >= MAX_STREAMS) return;
    StreamTrack* trk = &streamsArr[vo->streamTrackId];
    if (!trk->playing) return;

    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t inc = vo->sampleInc1616;
    uint32_t accum = vo->streamFracAccum;
    uint16_t tail = trk->tail;
    uint16_t head = trk->head;

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        for (int i = 0; i < samples; i++) {
            accum += inc;
            uint32_t stepsToConsume = accum >> 16;
            accum &= 0xFFFF;

            if (stepsToConsume > 0) {
                uint16_t available = (STREAM_BUF_SAMPLES + head - tail) & STREAM_BUF_MASK;
                if (stepsToConsume > available) stepsToConsume = available;
                tail = (tail + stepsToConsume) & STREAM_BUF_MASK;
                trk->samplesPlayed += stepsToConsume;
            }

            int16_t val1 = trk->buffer[tail];
            int16_t val2 = trk->buffer[(tail + 1) & STREAM_BUF_MASK];
            int32_t interp = val1 + (((val2 - val1) * (int32_t)(accum >> 1)) >> 15);
            mixBuffer[i] += (interp * finalVol) >> 16;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            accum += inc;
            uint32_t stepsToConsume = accum >> 16;
            accum &= 0xFFFF;

            if (stepsToConsume > 0) {
                uint16_t available = (STREAM_BUF_SAMPLES + head - tail) & STREAM_BUF_MASK;
                if (stepsToConsume > available) stepsToConsume = available;
                tail = (tail + stepsToConsume) & STREAM_BUF_MASK;
                trk->samplesPlayed += stepsToConsume;
            }

            int16_t val1 = trk->buffer[tail];
            int16_t val2 = trk->buffer[(tail + 1) & STREAM_BUF_MASK];
            int32_t interp = val1 + (((val2 - val1) * (int32_t)(accum >> 1)) >> 15);

            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            mixBuffer[i] += (interp * finalVol) >> 16;
            currentEnv += envStep;
        }
    }
    vo->streamFracAccum = accum;
    trk->tail = tail;
}

// Lógica de Envelope ADSR Clássica Otimizada.
static FORCE_INLINE IRAM_ATTR void updateAdsrBlock(Voice* vo, int samples, int32_t& startEnv, int32_t& envStep) {
    if (vo->inst) {
        startEnv = ENV_MAX;
        vo->currEnvVal = ENV_MAX;
        envStep = 0;
        return;
    }

    startEnv = vo->currEnvVal;

    if (vo->envState == ENV_IDLE || vo->envState == ENV_SUSTAIN) {
        envStep = 0;
        return;
    }

    uint32_t steps = (uint32_t)samples;
    uint32_t target = startEnv;

    switch (vo->envState) {
    case ENV_ATTACK:
        if (vo->rateAttack >= ENV_MAX) { 
            startEnv = ENV_MAX;
            envStep = 0;
            vo->envState = ENV_DECAY;
        } else {
            uint64_t totalChange = (uint64_t)vo->rateAttack * steps;
            if ((uint64_t)ENV_MAX > target && (uint64_t)(ENV_MAX - target) > totalChange) {
                envStep = vo->rateAttack;
            } else {
                envStep = (ENV_MAX - target) / steps;
                vo->envState = ENV_DECAY;
            }
        }
        break;

    case ENV_DECAY:
        if (vo->rateDecay >= ENV_MAX) {
            startEnv = vo->levelSustain;
            envStep = 0;
            vo->envState = ENV_SUSTAIN;
        } else {
            uint64_t totalChange = (uint64_t)vo->rateDecay * steps;
            if (target > vo->levelSustain && (uint64_t)(target - vo->levelSustain) > totalChange) {
                envStep = -((int32_t)vo->rateDecay);
            } else {
                if (target < vo->levelSustain) {
                    envStep = ((int32_t)(vo->levelSustain - target)) / (int32_t)steps;
                } else {
                    envStep = -((int32_t)(target - vo->levelSustain) / (int32_t)steps);
                }
                vo->envState = ENV_SUSTAIN;
            }
        }
        break;
        
    case ENV_RELEASE:
        if (vo->rateRelease >= ENV_MAX) {
            startEnv = 0;
            envStep = 0;
            vo->envState = ENV_IDLE;
        } else {
            uint64_t totalChange = (uint64_t)vo->rateRelease * steps;
            if (target > totalChange) {
                envStep = -((int32_t)vo->rateRelease);
            } else {
                envStep = -((int32_t)target / (int32_t)steps);
                vo->envState = ENV_IDLE;
            }
        }
        break;
    default:
        envStep = 0;
        break;
    }

    int64_t finalEnv = (int64_t)startEnv + ((int64_t)envStep * steps);
    if (finalEnv < 0) finalEnv = 0;
    if (finalEnv > ENV_MAX) finalEnv = ENV_MAX;
    
    vo->currEnvVal = (uint32_t)finalEnv;

    if (vo->envState == ENV_IDLE) {
        vo->currEnvVal = 0;
        vo->active = false;
    }
}