#pragma once
#include "ESP32Synth.h"

#define FORCE_INLINE __attribute__((always_inline)) inline

// GCC branch predictor hints: extreme pipeline optimization for Xtensa
#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// The LoopMode switch is pulled out of the hot path.
// MEGA OPTIMIZATION: Pre-shifted constants to avoid 64-bit shifting inside the tight loop!
#define ADVANCE_SAMPLE_POS \
    if (dir) { \
        pos += inc; \
        if (UNLIKELY(pos >= lEnd16)) { \
            uint64_t over = pos - lEnd16; \
            if (vo->sampleLoopMode == LOOP_FORWARD) { \
                pos = lStart16 + over; \
            } \
            else if (vo->sampleLoopMode == LOOP_PINGPONG) { \
                dir = false; \
                pos = (lEnd16 > over + 1) ? (lEnd16 - 1 - over) : lStart16; \
            } \
            else { vo->sampleFinished = true; break; } \
        } \
    } else { \
        bool underflow = (pos < inc); \
        pos -= inc; \
        if (UNLIKELY(underflow || pos < lStart16)) { \
            uint64_t under = lStart16 - pos; \
            if (vo->sampleLoopMode == LOOP_PINGPONG) { \
                dir = true; \
                pos = lStart16 + under; \
            } \
            else if (vo->sampleLoopMode == LOOP_REVERSE) { \
                pos = (lEnd16 > under) ? (lEnd16 - under) : lStart16; \
            } \
            else { vo->sampleFinished = true; break; } \
        } \
    }

// Render: PCM Sample
static FORCE_INLINE IRAM_ATTR void renderBlockSample(Voice* __restrict__ vo, int32_t* __restrict__ mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->sampleFinished) return;
    const SampleData* sData = &registeredSamples[vo->curSampleId];
    if (!sData->data) return;

    const uint32_t len    = sData->length;
    uint64_t       pos    = vo->samplePos1616;
    const uint32_t inc    = vo->sampleInc1616;
    const uint32_t lStart = vo->sampleLoopStart;
    const uint32_t lEnd   = (vo->sampleLoopEnd > 0 && vo->sampleLoopEnd <= len) ? vo->sampleLoopEnd : len;
    const uint64_t lStart16 = (uint64_t)lStart << 16;
    const uint64_t lEnd16   = (uint64_t)lEnd << 16;
    int32_t        currentEnv = startEnv;
    int32_t        volBase    = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    bool           dir        = vo->sampleDirection;

    if (envStep == 0) {
        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
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
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i]    += (data[(uint32_t)(pos >> 16)] * finalVol) >> 16;
                    currentEnv      += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i]    += ((((int16_t)data[(uint32_t)(pos >> 16)] - 128) << 8) * finalVol) >> 16;
                    currentEnv      += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    uint32_t idx     = (uint32_t)(pos >> 16);
                    mixBuffer[i]    += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    currentEnv      += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
        }
    }
    vo->samplePos1616  = pos;
    vo->sampleDirection = dir;
}

// Render: Wavetable
static FORCE_INLINE IRAM_ATTR void renderBlockWavetable(Voice* __restrict__ vo, int32_t* __restrict__ mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (!vo->wtData) return;

    int32_t        currentEnv = startEnv;
    int32_t        volBase    = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t       ph         = vo->phase;
    uint32_t       inc        = vo->phaseInc + vo->vibOffset;
    const uint32_t size       = vo->wtSize;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    v4u32 vInc     = {0, inc, inc * 2, inc * 3};
    v4u32 vIncStep = {inc * 4, inc * 4, inc * 4, inc * 4};
    v4i32 vEnvStep = {0, envStep, envStep * 2, envStep * 3};
    v4i32 vEnvStep4 = {envStep * 4, envStep * 4, envStep * 4, envStep * 4};
    v4i32 vEnv     = {currentEnv, currentEnv, currentEnv, currentEnv};
    vEnv += vEnvStep;
    v4u32 vPh = {ph, ph, ph, ph};
    vPh += vInc;
#endif

    if (envStep == 0) {
        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
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
                // OPTIMIZATION: Pure 32-bit replacement for 64-bit operations.
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
                    vEnvShifted      &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol   = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4u32 vIdx        = ((vPh >> 16) * size) >> 16;
                    v4i32 vals        = { data[vIdx[0]], data[vIdx[1]], data[vIdx[2]], data[vIdx[3]] };
                    *(v4i32*)&mixBuffer[i] += (vals * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    mixBuffer[i]    += (data[((ph >> 16) * size) >> 16] * finalVol) >> 16;
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
                    vEnvShifted      &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol   = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4u32 vIdx        = ((vPh >> 16) * size) >> 16;
                    v4i32 vals        = { (int32_t)data[vIdx[0]], (int32_t)data[vIdx[1]], (int32_t)data[vIdx[2]], (int32_t)data[vIdx[3]] };
                    vals = (vals - 128) << 8;
                    *(v4i32*)&mixBuffer[i] += (vals * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    mixBuffer[i]    += ((((int16_t)data[((ph >> 16) * size) >> 16] - 128) << 8) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx     = ((ph >> 16) * size) >> 16;
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    mixBuffer[i]    += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            }
        }
    }
    vo->phase = ph;
}

// Render: Basic Oscillators (Saw, Sine, Pulse, Triangle)
static FORCE_INLINE IRAM_ATTR void renderBlockBasic(Voice* __restrict__ vo, int32_t* __restrict__ mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t        currentEnv = startEnv;
    int32_t        volBase    = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t       ph         = vo->phase;
    uint32_t       inc        = vo->phaseInc + vo->vibOffset;
    const WaveType type       = vo->type;
    const uint32_t pw         = vo->pulseWidth;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    v4u32 vInc      = {0, inc, inc * 2, inc * 3};
    v4u32 vIncStep  = {inc * 4, inc * 4, inc * 4, inc * 4};
    v4i32 vEnvStep  = {0, envStep, envStep * 2, envStep * 3};
    v4i32 vEnvStep4 = {envStep * 4, envStep * 4, envStep * 4, envStep * 4};
    v4i32 vEnv      = {currentEnv, currentEnv, currentEnv, currentEnv};
    vEnv += vEnvStep;
    v4u32 vPh = {ph, ph, ph, ph};
    vPh += vInc;
#endif

    if (envStep == 0) {
        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc * samples; return; }

        switch (type) {
            case WAVE_SAW:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    v4i32 vVolVec = {finalVol, finalVol, finalVol, finalVol};
                    for (int i = 0; i < samples; i += 4) {
                        // EXTREME OPTIMIZATION: Pure Arithmetic Shift! Cuts 2 instructions per 4 samples.
                        v4i32 shifted = (v4i32)vPh >> 16; 
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
#if SYNTH_INTERPOLATE_SINE
                        uint32_t i0 = vPh[0] >> SINE_SHIFT;
                        uint32_t i1 = vPh[1] >> SINE_SHIFT;
                        uint32_t i2 = vPh[2] >> SINE_SHIFT;
                        uint32_t i3 = vPh[3] >> SINE_SHIFT;

                        int32_t s0 = sineLUT[i0] + ((((sineLUT[i0 + 1] - sineLUT[i0]) * (int32_t)((vPh[0] >> 4) & 0xFFFF))) >> 16);
                        int32_t s1 = sineLUT[i1] + ((((sineLUT[i1 + 1] - sineLUT[i1]) * (int32_t)((vPh[1] >> 4) & 0xFFFF))) >> 16);
                        int32_t s2 = sineLUT[i2] + ((((sineLUT[i2 + 1] - sineLUT[i2]) * (int32_t)((vPh[2] >> 4) & 0xFFFF))) >> 16);
                        int32_t s3 = sineLUT[i3] + ((((sineLUT[i3 + 1] - sineLUT[i3]) * (int32_t)((vPh[3] >> 4) & 0xFFFF))) >> 16);

                        v4i32 sines = { s0, s1, s2, s3 };
#else
                        v4i32 sines = { sineLUT[vPh[0] >> SINE_SHIFT], sineLUT[vPh[1] >> SINE_SHIFT], sineLUT[vPh[2] >> SINE_SHIFT], sineLUT[vPh[3] >> SINE_SHIFT] };
#endif
                        *(v4i32*)&mixBuffer[i] += (sines * vVolVec) >> 16;
                        vPh += vIncStep;
                    }
                    ph += inc * samples;
                }
#else
                for (int i = 0; i < samples; i++) {
#if SYNTH_INTERPOLATE_SINE
                    uint32_t idx = ph >> SINE_SHIFT;
                    int32_t s0 = sineLUT[idx];
                    int32_t interp = s0 + ((((sineLUT[idx + 1] - s0) * (int32_t)((ph >> 4) & 0xFFFF))) >> 16);
                    mixBuffer[i] += (interp * finalVol) >> 16;
#else
                    mixBuffer[i] += (sineLUT[ph >> SINE_SHIFT] * finalVol) >> 16;
#endif
                    ph += inc;
                }
#endif
                break;
            case WAVE_PULSE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                {
                    int32_t sVolPos  = (finalVol * 32767) >> 16;
                    v4u32 vPw        = {pw, pw, pw, pw};
                    v4i32 vVolNeg    = {-sVolPos, -sVolPos, -sVolPos, -sVolPos};
                    v4i32 vVolPos2   = {sVolPos * 2, sVolPos * 2, sVolPos * 2, sVolPos * 2};

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
                        // EXTREME OPTIMIZATION: Phase generated via pure arithmetic extension
                        v4i32 saw     = (v4i32)vPh >> 16;
                        v4i32 sawMask = saw >> 31;
                        v4i32 tri     = (((saw ^ sawMask) * 2) - 32767);
                        *(v4i32*)&mixBuffer[i] += (tri * vVolVec) >> 16;
                        vPh += vIncStep;
                    }
                    ph += inc * samples;
                }
#else
                for (int i = 0; i < samples; i++) {
                    int16_t saw  = (int16_t)(ph >> 16);
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
                    vEnvShifted      &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol   = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4i32 shifted     = (v4i32)vPh >> 16; // EXTREME OPTIMIZATION
                    *(v4i32*)&mixBuffer[i] += (shifted * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i]    += ((int16_t)(ph >> 16) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            case WAVE_SINE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted      &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol   = (vEnvShifted * (int32_t)volBase) >> 14;
#if SYNTH_INTERPOLATE_SINE
                    uint32_t i0 = vPh[0] >> SINE_SHIFT;
                    uint32_t i1 = vPh[1] >> SINE_SHIFT;
                    uint32_t i2 = vPh[2] >> SINE_SHIFT;
                    uint32_t i3 = vPh[3] >> SINE_SHIFT;

                    int32_t s0 = sineLUT[i0] + ((((sineLUT[i0 + 1] - sineLUT[i0]) * (int32_t)((vPh[0] >> 4) & 0xFFFF))) >> 16);
                    int32_t s1 = sineLUT[i1] + ((((sineLUT[i1 + 1] - sineLUT[i1]) * (int32_t)((vPh[1] >> 4) & 0xFFFF))) >> 16);
                    int32_t s2 = sineLUT[i2] + ((((sineLUT[i2 + 1] - sineLUT[i2]) * (int32_t)((vPh[2] >> 4) & 0xFFFF))) >> 16);
                    int32_t s3 = sineLUT[i3] + ((((sineLUT[i3 + 1] - sineLUT[i3]) * (int32_t)((vPh[3] >> 4) & 0xFFFF))) >> 16);

                    v4i32 sines = { s0, s1, s2, s3 };
#else
                    v4i32 sines       = { sineLUT[vPh[0] >> SINE_SHIFT], sineLUT[vPh[1] >> SINE_SHIFT], sineLUT[vPh[2] >> SINE_SHIFT], sineLUT[vPh[3] >> SINE_SHIFT] };
#endif
                    *(v4i32*)&mixBuffer[i] += (sines * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
#if SYNTH_INTERPOLATE_SINE
                    uint32_t idx = ph >> SINE_SHIFT;
                    int32_t s0 = sineLUT[idx];
                    int32_t interp = s0 + ((((sineLUT[idx + 1] - s0) * (int32_t)((ph >> 4) & 0xFFFF))) >> 16);
                    mixBuffer[i]    += (interp * finalVol) >> 16;
#else
                    mixBuffer[i]    += (sineLUT[ph >> SINE_SHIFT] * finalVol) >> 16;
#endif
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
                        vEnvShifted      &= ~(vEnvShifted >> 31);
                        v4i32 vFinalVol   = (vEnvShifted * (int32_t)volBase) >> 14;

                        v4i32 diff   = (v4i32)((vPh >> 1) - (vPw >> 1));
                        v4i32 mask   = diff >> 31;
                        v4i32 vVolPos = (vFinalVol * 32767) >> 16;

                        *(v4i32*)&mixBuffer[i] += (-vVolPos) + (mask & (vVolPos * 2));

                        vPh += vIncStep; vEnv += vEnvStep4;
                    }
                    ph += inc * samples; currentEnv += envStep * samples;
                }
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    mixBuffer[i]    += (((ph < pw) ? 32767 : -32767) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
#endif
                break;
            case WAVE_TRIANGLE:
#if defined(CONFIG_IDF_TARGET_ESP32S3)
                for (int i = 0; i < samples; i += 4) {
                    v4i32 vEnvShifted = vEnv >> 14;
                    vEnvShifted      &= ~(vEnvShifted >> 31);
                    v4i32 vFinalVol   = (vEnvShifted * (int32_t)volBase) >> 14;
                    v4i32 saw         = (v4i32)vPh >> 16; // EXTREME OPTIMIZATION
                    v4i32 sawMask     = saw >> 31;
                    v4i32 tri         = (((saw ^ sawMask) * 2) - 32767);
                    *(v4i32*)&mixBuffer[i] += (tri * vFinalVol) >> 16;
                    vPh += vIncStep; vEnv += vEnvStep4;
                }
                ph += inc * samples; currentEnv += envStep * samples;
#else
                for (int i = 0; i < samples; i++) {
                    int32_t envSafe  = currentEnv >> 14;
                    envSafe         &= ~(envSafe >> 31);
                    int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
                    int16_t saw      = (int16_t)(ph >> 16);
                    mixBuffer[i]    += ((int16_t)(((saw ^ (saw >> 15)) * 2) - 32767) * finalVol) >> 16;
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
static FORCE_INLINE IRAM_ATTR void renderBlockNoise(Voice* __restrict__ vo, int32_t* __restrict__ mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t  currentEnv   = startEnv;
    int32_t  volBase      = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t rng          = vo->rngState;
    uint32_t ph           = vo->phase;
    uint32_t inc          = (vo->phaseInc + vo->vibOffset) << 4;
    int16_t  currentSample = vo->noiseSample;

    if (envStep == 0) {
        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
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
            int32_t envSafe  = currentEnv >> 14;
            envSafe         &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            mixBuffer[i]    += (currentSample * finalVol) >> 16;
            currentEnv      += envStep;
        }
    }
    vo->rngState    = rng;
    vo->phase       = ph;
    vo->noiseSample = currentSample;
}

// Render: Stream from RAM Buffer
static FORCE_INLINE IRAM_ATTR void renderBlockStream(Voice* __restrict__ vo, StreamTrack* __restrict__ streamsArr, int32_t* __restrict__ mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->streamTrackId < 0 || vo->streamTrackId >= MAX_STREAMS) return;
    StreamTrack* trk = &streamsArr[vo->streamTrackId];
    if (trk->seeking) return;
    if (!trk->playing && trk->head == trk->tail) {
        vo->active = false;
        return;
    }
    if (!trk->playing) return;

    int32_t  currentEnv = startEnv;
    int32_t  volBase    = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t inc        = vo->sampleInc1616;
    uint32_t accum      = vo->streamFracAccum;
    uint16_t tail       = trk->tail;
    uint16_t head       = trk->head;

    if (envStep == 0) {
        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        for (int i = 0; i < samples; i++) {
            accum += inc;
            uint32_t stepsToConsume = accum >> 16;
            accum &= 0xFFFF;

            if (stepsToConsume > 0) {
                uint16_t available = (STREAM_BUF_SAMPLES + head - tail) & STREAM_BUF_MASK;
                if (stepsToConsume > available) stepsToConsume = available;
                tail             = (tail + stepsToConsume) & STREAM_BUF_MASK;
                trk->samplesPlayed += stepsToConsume;
            }

            int16_t val1   = trk->buffer[tail];
            int16_t val2   = trk->buffer[(tail + 1) & STREAM_BUF_MASK];
            int32_t interp = val1 + (((val2 - val1) * (int32_t)(accum >> 1)) >> 15);
            mixBuffer[i]  += (interp * finalVol) >> 16;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            accum += inc;
            uint32_t stepsToConsume = accum >> 16;
            accum &= 0xFFFF;

            if (stepsToConsume > 0) {
                uint16_t available = (STREAM_BUF_SAMPLES + head - tail) & STREAM_BUF_MASK;
                if (stepsToConsume > available) stepsToConsume = available;
                tail             = (tail + stepsToConsume) & STREAM_BUF_MASK;
                trk->samplesPlayed += stepsToConsume;
            }

            int16_t val1   = trk->buffer[tail];
            int16_t val2   = trk->buffer[(tail + 1) & STREAM_BUF_MASK];
            int32_t interp = val1 + (((val2 - val1) * (int32_t)(accum >> 1)) >> 15);

            int32_t envSafe  = currentEnv >> 14;
            envSafe         &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            mixBuffer[i]    += (interp * finalVol) >> 16;
            currentEnv      += envStep;
        }
    }
    vo->streamFracAccum = accum;
    trk->tail           = tail;
}

// Classic ADSR Envelope Logic (Optimized for Sub-Block Precision)
static FORCE_INLINE IRAM_ATTR int updateAdsrBlock(Voice* vo, int maxSamples, int32_t& startEnv, int32_t& envStep) {
    if (vo->inst) {
        startEnv       = ENV_MAX;
        vo->currEnvVal = ENV_MAX;
        envStep        = 0;
        return maxSamples;
    }

    startEnv = vo->currEnvVal;

    if (vo->envState == ENV_IDLE || vo->envState == ENV_SUSTAIN) {
        envStep = 0;
        return maxSamples;
    }

    uint32_t targetEnv = 0;
    uint32_t rate = 0;
    EnvState nextState = ENV_IDLE;

    if (vo->envState == ENV_ATTACK) {
        rate = vo->rateAttack;
        targetEnv = ENV_MAX;
        nextState = ENV_DECAY;
    } else if (vo->envState == ENV_DECAY) {
        rate = vo->rateDecay;
        targetEnv = vo->levelSustain;
        nextState = ENV_SUSTAIN;
    } else if (vo->envState == ENV_RELEASE) {
        rate = vo->rateRelease;
        targetEnv = 0;
        nextState = ENV_IDLE;
    }

    if (rate >= ENV_MAX) {
        startEnv       = targetEnv;
        envStep        = 0;
        vo->envState   = nextState;
        vo->currEnvVal = targetEnv;
        if (nextState == ENV_IDLE) vo->active = false;
        return maxSamples;
    }

    // Calcula exatamente quantas amostras faltam para esse estágio terminar
    uint32_t absDiff = (startEnv > targetEnv) ? (startEnv - targetEnv) : (targetEnv - startEnv);
    if (UNLIKELY(rate == 0)) rate = 1;
    uint32_t samplesNeeded = absDiff / rate;
    if (absDiff % rate != 0) samplesNeeded++; // Arredonda para cima

    int samplesToProcess = maxSamples;
    bool stageFinished = false;

    if (samplesNeeded <= (uint32_t)maxSamples) {
        samplesToProcess = (int)samplesNeeded;
        stageFinished = true;
    }

    // SIMD SHIELDING: Força o número de amostras a ser um múltiplo de 4 
    // para NUNCA corromper ou escrever fora da memória nos loops do renderBlock!
    samplesToProcess = (samplesToProcess + 3) & ~3;
    if (samplesToProcess > maxSamples) samplesToProcess = maxSamples;
    if (samplesToProcess == 0) samplesToProcess = 4;

    if (stageFinished) {
        envStep = (samplesToProcess > 0) ? (int32_t)(((int64_t)targetEnv - startEnv) / samplesToProcess) : 0;
        vo->currEnvVal = targetEnv;
        vo->envState   = nextState;
        if (nextState == ENV_IDLE) vo->active = false;
    } else {
        envStep = (vo->envState == ENV_ATTACK) ? (int32_t)rate : -((int32_t)rate);
        int64_t finalEnv = (int64_t)startEnv + ((int64_t)envStep * samplesToProcess);
        if (finalEnv < 0) finalEnv = 0;
        if (finalEnv > ENV_MAX) finalEnv = ENV_MAX;
        vo->currEnvVal = (uint32_t)finalEnv;
    }

    return samplesToProcess; // Retorna quantos samples renderizar antes de reavaliar o envelope
}