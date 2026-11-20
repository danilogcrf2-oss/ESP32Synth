#pragma once
#include "ESP32Synth.h"

#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

static inline int32_t lfoClampI32(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline uint32_t lfoXorShift32(uint32_t x) {
    if (x == 0U) x = 0x12345678UL;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static inline int16_t lfoClampSym16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32767) return -32767;
    return (int16_t)v;
}

static inline void lfoRefreshTiming(ModLFO* lfo, uint16_t cHz) {
    if (cHz == 0) cHz = 100;
    lfo->delayTicksCfg = (lfo->delayMsCfg == 0) ? 0 : (((uint32_t)lfo->delayMsCfg * cHz + 999U) / 1000U);
    uint32_t fadeTicks = (lfo->fadeInMsCfg == 0) ? 0 : (((uint32_t)lfo->fadeInMsCfg * cHz + 999U) / 1000U);
    lfo->fadeStepQ15   = (fadeTicks == 0) ? 0 : (32768U / fadeTicks);
    if (fadeTicks > 0 && lfo->fadeStepQ15 == 0) lfo->fadeStepQ15 = 1;
}

void ESP32Synth::setLFO(uint8_t lfoId, LFOWave wave, uint32_t rateCentiHz, uint16_t startPhaseDeg, uint16_t delayMs, uint16_t fadeInMs, bool oneShot) {
    if (lfoId >= MAX_LFOS) return;
    ModLFO* lfo = &lfos[lfoId];

    uint32_t sr = (_sampleRate > 0) ? _sampleRate : 48000U;
    uint32_t spDeg = startPhaseDeg % 360U;
    uint32_t startPh = (uint32_t)(((uint64_t)spDeg << 32) / 360ULL);
    uint32_t rateInc = (uint32_t)(((uint64_t)rateCentiHz * 4294967296ULL) / ((uint64_t)sr * 100ULL));

    lfo->active = false;
    __sync_synchronize();

    lfo->wave        = wave;
    lfo->baseRate    = rateCentiHz;
    lfo->effRate     = rateCentiHz;
    lfo->rateInc     = rateInc;
    lfo->startPhase  = startPh;
    lfo->phase       = startPh;
    lfo->delayMsCfg  = delayMs;
    lfo->fadeInMsCfg = fadeInMs;
    lfoRefreshTiming(lfo, controlRateHz);
    lfo->delayTicksRem = lfo->delayTicksCfg;
    lfo->fadeGainQ15   = (lfo->fadeStepQ15 == 0) ? 32768U : 0U;
    lfo->effAmpQ8      = 256;
    if (lfo->duty == 0) lfo->duty = 128;
    if (lfo->rngState == 0) lfo->rngState = 0x6D2B79F5UL + ((uint32_t)lfoId * 0x9E3779B9UL);

    lfo->rngState = lfoXorShift32(lfo->rngState);
    lfo->curRand  = lfoClampSym16((int16_t)(lfo->rngState >> 16));
    lfo->rngState = lfoXorShift32(lfo->rngState);
    lfo->nextRand = lfoClampSym16((int16_t)(lfo->rngState >> 16));

    lfo->oneShot  = oneShot;
    lfo->finished = false;
    lfo->curValue = 0;

    __sync_synchronize();
    lfo->active = true;
    _activeLfoMask |= (uint32_t)(1UL << lfoId);
}

void ESP32Synth::setLFORate(uint8_t lfoId, uint32_t rateCentiHz) {
    if (lfoId >= MAX_LFOS) return;
    ModLFO* lfo = &lfos[lfoId];
    uint32_t sr = (_sampleRate > 0) ? _sampleRate : 48000U;
    lfo->baseRate = rateCentiHz;
    lfo->effRate  = rateCentiHz;
    lfo->rateInc  = (uint32_t)(((uint64_t)rateCentiHz * 4294967296ULL) / ((uint64_t)sr * 100ULL));
}

void ESP32Synth::setLFOWave(uint8_t lfoId, LFOWave wave) {
    if (lfoId < MAX_LFOS) lfos[lfoId].wave = wave;
}

void ESP32Synth::setLFOPhase(uint8_t lfoId, uint16_t phaseDeg) {
    if (lfoId >= MAX_LFOS) return;
    uint32_t ph = (uint32_t)(((uint64_t)(phaseDeg % 360U) << 32) / 360ULL);
    lfos[lfoId].startPhase = ph;
    lfos[lfoId].phase      = ph;
}

void ESP32Synth::setLFODuty(uint8_t lfoId, uint8_t duty) {
    if (lfoId < MAX_LFOS) lfos[lfoId].duty = duty;
}

void ESP32Synth::setLFOEnabled(uint8_t lfoId, bool enable) {
    if (lfoId >= MAX_LFOS) return;
    lfos[lfoId].active = enable;
    if (enable) _activeLfoMask |= (uint32_t)(1UL << lfoId);
    else {
        _activeLfoMask &= ~(uint32_t)(1UL << lfoId);
        for (int r = 0; r < MAX_LFO_ROUTES; r++) {
            if (lfoRoutes[r].active && lfoRoutes[r].lfoId == lfoId) {
                resetLFORoute(&lfoRoutes[r]);
            }
        }
        lfos[lfoId].curValue = 0;
    }
}

void ESP32Synth::setLFOCallback(uint8_t lfoId, SynthLFOCallback cb) {
    if (lfoId < MAX_LFOS) lfos[lfoId].callback = cb;
}

void ESP32Synth::retriggerLFO(uint8_t lfoId) {
    if (lfoId >= MAX_LFOS) return;
    ModLFO* lfo = &lfos[lfoId];
    lfo->phase         = lfo->startPhase;
    lfo->delayTicksRem = lfo->delayTicksCfg;
    lfo->fadeGainQ15   = (lfo->fadeStepQ15 == 0) ? 32768U : 0U;
    lfo->finished      = false;
    lfo->rngState      = lfoXorShift32(lfo->rngState);
    lfo->curRand       = lfoClampSym16((int16_t)(lfo->rngState >> 16));
    lfo->rngState      = lfoXorShift32(lfo->rngState);
    lfo->nextRand      = lfoClampSym16((int16_t)(lfo->rngState >> 16));
}

void ESP32Synth::retriggerAllLFOs() {
    for (uint8_t i = 0; i < MAX_LFOS; i++) {
        if (lfos[i].active) retriggerLFO(i);
    }
}

void ESP32Synth::removeLFO(uint8_t lfoId) {
    if (lfoId >= MAX_LFOS) return;
    clearLFORoutes(lfoId);
    lfos[lfoId].active   = false;
    lfos[lfoId].callback = nullptr;
    lfos[lfoId].curValue = 0;
    _activeLfoMask &= ~(uint32_t)(1UL << lfoId);
}

int16_t ESP32Synth::getLFOValue(uint8_t lfoId) {
    return (lfoId < MAX_LFOS) ? lfos[lfoId].curValue : 0;
}

uint16_t ESP32Synth::getLFOValueUnipolar(uint8_t lfoId) {
    if (lfoId >= MAX_LFOS) return 32768;
    int32_t v = (int32_t)lfos[lfoId].curValue + 32767;
    if (v < 0) v = 0;
    if (v > 65534) v = 65534;
    return (uint16_t)((v * 65535UL) / 65534UL);
}

uint8_t ESP32Synth::getLFOValue8Bit(uint8_t lfoId) {
    return (uint8_t)(getLFOValueUnipolar(lfoId) >> 8);
}

int32_t ESP32Synth::getLFOValueScaled(uint8_t lfoId, int32_t minVal, int32_t maxVal) {
    uint16_t uni = getLFOValueUnipolar(lfoId);
    int64_t span = (int64_t)maxVal - (int64_t)minVal;
    return minVal + (int32_t)((span * (int64_t)uni) / 65535LL);
}

bool ESP32Synth::isLFOActive(uint8_t lfoId) {
    return (lfoId < MAX_LFOS) ? lfos[lfoId].active : false;
}

int8_t ESP32Synth::allocLFORoute(uint8_t lfoId, LFOTarget target, uint16_t targetStart, uint16_t targetEnd, uint8_t subId, uint8_t paramIdx, int32_t depthOrMin, int32_t maxVal, bool rangeMode) {
    if (lfoId >= MAX_LFOS || target == LFO_TARGET_NONE) return -1;

    int8_t slot = -1;
    for (int i = 0; i < MAX_LFO_ROUTES; i++) {
        if (!lfoRoutes[i].active) { slot = (int8_t)i; break; }
    }
    if (slot < 0) return -1;

    ModLFORoute* rt = &lfoRoutes[slot];
    rt->active      = false;
    __sync_synchronize();

    rt->lfoId       = lfoId;
    rt->target      = target;
    rt->targetStart = targetStart;
    rt->targetEnd   = targetEnd;
    rt->subId       = subId;
    rt->paramIdx    = paramIdx;
    rt->rangeMode   = rangeMode;
    rt->valA        = depthOrMin;
    rt->valB        = maxVal;
    rt->lastOffset  = 0;

    __sync_synchronize();
    rt->active = true;
    if ((uint8_t)slot >= _maxActiveRoute) _maxActiveRoute = (uint8_t)slot + 1;
    return slot;
}

int8_t ESP32Synth::routeLFOToVoice(uint8_t lfoId, LFOTarget target, uint16_t voiceStart, uint16_t voiceEnd, int32_t depthOrMin, int32_t maxVal, bool rangeMode, uint8_t paramIdx) {
    if (voiceStart >= MAX_VOICES) return -1;
    if (voiceEnd >= MAX_VOICES) voiceEnd = MAX_VOICES - 1;
    if (voiceEnd < voiceStart) voiceEnd = voiceStart;
    return allocLFORoute(lfoId, target, voiceStart, voiceEnd, 0, paramIdx, depthOrMin, maxVal, rangeMode);
}

int8_t ESP32Synth::assignLFOToVoice(uint8_t lfoId, LFOTarget target, uint16_t voice, int32_t depth, uint8_t paramIdx) {
    return routeLFOToVoice(lfoId, target, voice, voice, depth, 0, false, paramIdx);
}

int8_t ESP32Synth::assignLFOToVoiceRange(uint8_t lfoId, LFOTarget target, uint16_t voiceStart, uint16_t voiceEnd, int32_t minVal, int32_t maxVal, uint8_t paramIdx) {
    return routeLFOToVoice(lfoId, target, voiceStart, voiceEnd, minVal, maxVal, true, paramIdx);
}

int8_t ESP32Synth::routeLFOToBusFX(uint8_t lfoId, uint8_t busId, uint8_t slotId, uint8_t paramId, int32_t depthOrMin, int32_t maxVal, bool rangeMode) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS || paramId >= SYNTH_FX_PARAMS) return -1;
    return allocLFORoute(lfoId, LFO_TARGET_BUS_FX_PARAM, busId, busId, slotId, paramId, depthOrMin, maxVal, rangeMode);
}

int8_t ESP32Synth::assignLFOToBusFX(uint8_t lfoId, uint8_t busId, uint8_t slotId, uint8_t paramId, int32_t minVal, int32_t maxVal) {
    return routeLFOToBusFX(lfoId, busId, slotId, paramId, minVal, maxVal, true);
}

int8_t ESP32Synth::routeLFOToBus(uint8_t lfoId, LFOTarget target, uint8_t busId, int32_t depthOrMin, int32_t maxVal, bool rangeMode) {
    if (busId >= MAX_BUSES) return -1;
    return allocLFORoute(lfoId, target, busId, busId, 0, 0, depthOrMin, maxVal, rangeMode);
}

int8_t ESP32Synth::routeLFOToMaster(uint8_t lfoId, LFOTarget target, int32_t depthOrMin, int32_t maxVal, bool rangeMode, uint8_t paramIdx) {
    return allocLFORoute(lfoId, target, 0, 0, 0, paramIdx, depthOrMin, maxVal, rangeMode);
}

int8_t ESP32Synth::routeLFOToLFO(uint8_t srcLfoId, LFOTarget target, uint8_t dstLfoId, int32_t depthOrMin, int32_t maxVal, bool rangeMode) {
    if (dstLfoId >= MAX_LFOS || srcLfoId == dstLfoId) return -1;
    return allocLFORoute(srcLfoId, target, dstLfoId, dstLfoId, 0, 0, depthOrMin, maxVal, rangeMode);
}

void ESP32Synth::setLFORouteDepth(uint8_t routeId, int32_t depthOrMin, int32_t maxVal, bool rangeMode) {
    if (routeId >= MAX_LFO_ROUTES) return;
    ModLFORoute* rt = &lfoRoutes[routeId];
    if (rt->rangeMode && !rangeMode) rt->lastOffset = 0;
    rt->valA      = depthOrMin;
    rt->valB      = maxVal;
    rt->rangeMode = rangeMode;
}

void ESP32Synth::setLFORouteActive(uint8_t routeId, bool active) {
    if (routeId >= MAX_LFO_ROUTES) return;
    if (!active && lfoRoutes[routeId].active) {
        lfoRoutes[routeId].active = false;
        __sync_synchronize();
        resetLFORoute(&lfoRoutes[routeId]);
    } else if (active) {
        lfoRoutes[routeId].lastOffset = 0;
        __sync_synchronize();
        lfoRoutes[routeId].active = true;
        if (routeId >= _maxActiveRoute) _maxActiveRoute = routeId + 1;
    }
}

void ESP32Synth::removeLFORoute(uint8_t routeId) {
    if (routeId >= MAX_LFO_ROUTES) return;
    ModLFORoute* rt = &lfoRoutes[routeId];
    rt->active = false;
    __sync_synchronize();
    resetLFORoute(rt);
    rt->target = LFO_TARGET_NONE;
}

void ESP32Synth::clearLFORoutes(uint8_t lfoId) {
    for (uint8_t r = 0; r < MAX_LFO_ROUTES; r++) {
        if (lfoRoutes[r].target != LFO_TARGET_NONE && lfoRoutes[r].lfoId == lfoId) {
            removeLFORoute(r);
        }
    }
}

void ESP32Synth::clearAllLFORoutes() {
    for (uint8_t r = 0; r < MAX_LFO_ROUTES; r++) {
        if (lfoRoutes[r].target != LFO_TARGET_NONE) {
            removeLFORoute(r);
        }
    }
    _maxActiveRoute = 0;
}

void ESP32Synth::resetLFORoute(ModLFORoute* rt) {
    if (!rt || rt->target == LFO_TARGET_NONE) return;

    switch (rt->target) {
        case LFO_TARGET_PULSE_WIDTH:
            if (!rt->rangeMode && rt->lastOffset != 0) {
                int32_t maxPW = (int32_t)((1ULL << (32U - _pwShift)) - 1ULL);
                for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                    int32_t cur = (int32_t)(voices[v].pulseWidth >> _pwShift);
                    int32_t restored = lfoClampI32(cur - rt->lastOffset, 0, maxPW);
                    uint64_t wCalc = (uint64_t)restored << _pwShift;
                    voices[v].pulseWidth = (wCalc > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)wCalc;
                }
            }
            break;

        case LFO_TARGET_VOICE_CP:
            if (!rt->rangeMode && rt->lastOffset != 0 && rt->paramIdx < SYNTH_CUSTOM_PARAMS_PER_VOICE) {
                for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                    int32_t restored = lfoClampI32((int32_t)voices[v].cp[rt->paramIdx] - rt->lastOffset, -32768, 32767);
                    voices[v].cp[rt->paramIdx] = (int16_t)restored;
                }
            }
            break;

        case LFO_TARGET_VOICE_PAN:
#if SYNTH_STEREO
            for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                updateVoicePanGains(&voices[v], voices[v].pan);
            }
#endif
            break;

        case LFO_TARGET_BUS_FX_PARAM:
            if (!rt->rangeMode && rt->lastOffset != 0 && rt->targetStart < MAX_BUSES &&
                rt->subId < MAX_FX_PER_BUS && rt->paramIdx < SYNTH_FX_PARAMS) {
                FXNode* fx = &busesConfig[rt->targetStart].fxChain[rt->subId];
                int32_t restored = lfoClampI32((int32_t)fx->ep[rt->paramIdx] - rt->lastOffset, -32768, 32767);
                fx->ep[rt->paramIdx] = (int16_t)restored;
            }
            break;

        case LFO_TARGET_BUS_MIX:
            if (!rt->rangeMode && rt->lastOffset != 0 && rt->targetStart < MAX_BUSES) {
                int32_t restored = lfoClampI32((int32_t)busesConfig[rt->targetStart].mixLevel - rt->lastOffset, 0, 255);
                busesConfig[rt->targetStart].mixLevel = (uint8_t)restored;
            }
            break;

        case LFO_TARGET_BUS_PAN:
#if SYNTH_STEREO
            if (!rt->rangeMode && rt->lastOffset != 0 && rt->targetStart < MAX_BUSES) {
                AudioBus* bus = &busesConfig[rt->targetStart];
                bus->pan = (int16_t)lfoClampI32((int32_t)bus->pan - rt->lastOffset, -32767, 32767);
                computePanLawGains(bus->pan, true, bus->panGainL, bus->panGainR);
            }
#endif
            break;

        case LFO_TARGET_BUS_WIDTH:
#if SYNTH_STEREO
            if (!rt->rangeMode && rt->lastOffset != 0 && rt->targetStart < MAX_BUSES) {
                int32_t restored = lfoClampI32((int32_t)busesConfig[rt->targetStart].stereoWidth - rt->lastOffset, 0, 512);
                busesConfig[rt->targetStart].stereoWidth = (uint16_t)restored;
            }
#endif
            break;

        case LFO_TARGET_DSP_PARAM:
            if (!rt->rangeMode && rt->lastOffset != 0 && rt->paramIdx < SYNTH_CUSTOM_PARAMS_GLOBAL) {
                dp[rt->paramIdx] = (int16_t)lfoClampI32((int32_t)dp[rt->paramIdx] - rt->lastOffset, -32768, 32767);
            }
            break;

        case LFO_TARGET_MASTER_VOL:
            if (!rt->rangeMode && rt->lastOffset != 0) {
                int32_t maxV = (int32_t)((1UL << (16U - _volShift)) - 1UL);
                int32_t cur = (int32_t)(_masterVolume >> _volShift);
                int32_t restored = lfoClampI32(cur - rt->lastOffset, 0, maxV);
                uint32_t vCalc = (uint32_t)restored << _volShift;
                _masterVolume = (vCalc > 65535U) ? 65535U : (uint16_t)vCalc;
            }
            break;

        case LFO_TARGET_MASTER_PAN:
#if SYNTH_STEREO
            if (!rt->rangeMode && rt->lastOffset != 0) {
                _masterPan = (int16_t)lfoClampI32((int32_t)_masterPan - rt->lastOffset, -32767, 32767);
                computePanLawGains(_masterPan, true, _masterPanGainL, _masterPanGainR);
            }
#endif
            break;

        case LFO_TARGET_MASTER_WIDTH:
#if SYNTH_STEREO
            if (!rt->rangeMode && rt->lastOffset != 0) {
                _masterWidth = (uint16_t)lfoClampI32((int32_t)_masterWidth - rt->lastOffset, 0, 512);
            }
#endif
            break;

        case LFO_TARGET_LFO_RATE:
            if (rt->targetStart < MAX_LFOS) {
                lfos[rt->targetStart].effRate = lfos[rt->targetStart].baseRate;
            }
            break;

        case LFO_TARGET_LFO_AMP:
            if (rt->targetStart < MAX_LFOS) {
                lfos[rt->targetStart].effAmpQ8 = 256;
            }
            break;

        default: break;
    }

    rt->lastOffset = 0;
}

void IRAM_ATTR ESP32Synth::processModularLFOs() {
    if (LIKELY(_activeLfoMask == 0)) return;

    const uint32_t sr = (_sampleRate > 0) ? _sampleRate : 48000U;

    for (uint8_t i = 0; i < MAX_LFOS; i++) {
        if (!(_activeLfoMask & (1UL << i))) continue;
        ModLFO* lfo = &lfos[i];
        if (!lfo->active) continue;

        if (lfo->delayTicksRem > 0) {
            lfo->delayTicksRem--;
            lfo->curValue = 0;
        } else {
            if (lfo->fadeGainQ15 < 32768U) {
                uint32_t fg = lfo->fadeGainQ15 + lfo->fadeStepQ15;
                lfo->fadeGainQ15 = (fg > 32768U) ? 32768U : fg;
            }

            if (!lfo->finished) {
                uint32_t inc = (lfo->effRate == lfo->baseRate)
                    ? lfo->rateInc
                    : (uint32_t)(((uint64_t)lfo->effRate * 4294967296ULL) / ((uint64_t)sr * 100ULL));

                uint32_t oldPh = lfo->phase;
                uint32_t newPh = oldPh + (inc * controlIntervalSamples);

                if (UNLIKELY(newPh < oldPh && inc > 0)) {
                    if (lfo->oneShot) {
                        newPh = 0xFFFFFFFFUL;
                        lfo->finished = true;
                    } else if (lfo->wave == LFO_SAMPLE_HOLD || lfo->wave == LFO_SMOOTH_RANDOM) {
                        lfo->curRand  = lfo->nextRand;
                        lfo->rngState = lfoXorShift32(lfo->rngState);
                        lfo->nextRand = lfoClampSym16((int16_t)(lfo->rngState >> 16));
                    }
                }
                lfo->phase = newPh;
            }

            uint32_t ph = lfo->phase;
            int32_t raw = 0;

            switch (lfo->wave) {
                case LFO_SINE: {
                    uint32_t idx = ph >> SINE_SHIFT;
                    int32_t s0 = sineLUT[idx];
                    raw = s0 + ((((sineLUT[idx + 1] - s0) * (int32_t)((ph >> 4) & 0xFFFFU))) >> 16);
                    break;
                }
                case LFO_TRIANGLE: {
                    int16_t saw = (int16_t)(ph >> 16);
                    raw = ((saw ^ (saw >> 15)) * 2) - 32767;
                    break;
                }
                case LFO_SAW_UP:
                    raw = (int32_t)((int16_t)((ph >> 16) ^ 0x8000U));
                    break;
                case LFO_SAW_DOWN:
                    raw = -(int32_t)((int16_t)((ph >> 16) ^ 0x8000U));
                    break;
                case LFO_SQUARE: {
                    uint32_t thresh = (uint32_t)lfo->duty << 24;
                    raw = (ph < thresh) ? 32767 : -32767;
                    break;
                }
                case LFO_SAMPLE_HOLD:
                    raw = lfo->curRand;
                    break;
                case LFO_SMOOTH_RANDOM: {
                    int32_t sCurve = 32767 - sineLUT[1024U + (ph >> 21)];
                    raw = (int32_t)lfo->curRand + (int32_t)(((int64_t)(lfo->nextRand - lfo->curRand) * sCurve) >> 16);
                    break;
                }
                default: break;
            }

            int32_t scaled = (int32_t)(((int64_t)raw * (int32_t)lfo->fadeGainQ15) >> 15);
            scaled = (scaled * lfo->effAmpQ8) >> 8;
            lfo->curValue = lfoClampSym16(scaled);
        }

        if (lfo->callback) {
            lfo->callback(i, lfo->curValue);
        }

        lfo->effRate  = lfo->baseRate;
        lfo->effAmpQ8 = 256;
    }

    // Pass 0 applies Range routes first, Pass 1 applies Additive routes (allows stacking Range + Additive cleanly)
    for (uint8_t pass = 0; pass < 2; pass++) {
        bool wantRange = (pass == 0);
        for (uint8_t r = 0; r < _maxActiveRoute; r++) {
            ModLFORoute* rt = &lfoRoutes[r];
            if (!rt->active || rt->rangeMode != wantRange) continue;
            if (rt->lfoId >= MAX_LFOS || !lfos[rt->lfoId].active) continue;

            int32_t lfoBi   = lfos[rt->lfoId].curValue;
            uint32_t lfoUni = (uint32_t)(((int32_t)lfoBi + 32767) * 65535UL / 65534UL);

            int32_t valMapped = 0;
            int32_t newOff    = 0;
            if (wantRange) {
                int64_t span = (int64_t)rt->valB - (int64_t)rt->valA;
                valMapped = rt->valA + (int32_t)((span * (int64_t)lfoUni) / 65535LL);
                for (uint8_t k = 0; k < _maxActiveRoute; k++) {
                    if (!lfoRoutes[k].rangeMode && lfoRoutes[k].active &&
                        lfoRoutes[k].target == rt->target &&
                        lfoRoutes[k].targetStart == rt->targetStart &&
                        lfoRoutes[k].subId == rt->subId &&
                        lfoRoutes[k].paramIdx == rt->paramIdx) {
                        lfoRoutes[k].lastOffset = 0;
                    }
                }
            } else {
                newOff = (int32_t)(((int64_t)lfoBi * (int64_t)rt->valA) / 32767LL);
            }

            switch (rt->target) {
                case LFO_TARGET_PULSE_WIDTH: {
                    int32_t maxPW = (int32_t)((1ULL << (32U - _pwShift)) - 1ULL);
                    if (wantRange) {
                        int32_t cl = lfoClampI32(valMapped, 0, maxPW);
                        uint64_t wCalc = (uint64_t)cl << _pwShift;
                        uint32_t pw32 = (wCalc > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)wCalc;
                        for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                            voices[v].pulseWidth = pw32;
                        }
                    } else {
                        int32_t prevOff = rt->lastOffset;
                        int32_t applied = newOff;
                        for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                            int32_t base = (int32_t)(voices[v].pulseWidth >> _pwShift) - prevOff;
                            int32_t cl   = lfoClampI32(base + newOff, 0, maxPW);
                            if (v == rt->targetStart && rt->targetStart == rt->targetEnd) applied = cl - base;
                            uint64_t wCalc = (uint64_t)cl << _pwShift;
                            voices[v].pulseWidth = (wCalc > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)wCalc;
                        }
                        rt->lastOffset = applied;
                    }
                    break;
                }

                case LFO_TARGET_VOICE_CP: {
                    if (rt->paramIdx >= SYNTH_CUSTOM_PARAMS_PER_VOICE) break;
                    uint8_t pIdx = rt->paramIdx;
                    if (wantRange) {
                        int16_t cl = (int16_t)lfoClampI32(valMapped, -32768, 32767);
                        for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                            voices[v].cp[pIdx] = cl;
                        }
                    } else {
                        int32_t prevOff = rt->lastOffset;
                        int32_t applied = newOff;
                        for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                            int32_t base = (int32_t)voices[v].cp[pIdx] - prevOff;
                            int32_t cl   = lfoClampI32(base + newOff, -32768, 32767);
                            if (v == rt->targetStart && rt->targetStart == rt->targetEnd) applied = cl - base;
                            voices[v].cp[pIdx] = (int16_t)cl;
                        }
                        rt->lastOffset = applied;
                    }
                    break;
                }

                case LFO_TARGET_VOICE_PITCH: {
                    int32_t deltaCentiHz = wantRange ? valMapped : newOff;
                    int32_t incDelta = (int32_t)(((int64_t)deltaCentiHz * 4294967296LL) / ((int64_t)sr * 100LL));
                    for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                        Voice* vo = &voices[v];
                        if (!vo->active) continue;
                        vo->vibOffset += incDelta;

                        if (vo->type == WAVE_SAMPLE || (vo->type == WAVE_STREAM && vo->streamTrackId >= 0)) {
                            int32_t totalVibCenti = (int32_t)(((int64_t)vo->vibOffset * sr * 100LL) >> 32);
                            int32_t modFreq = (int32_t)vo->freqVal + totalVibCenti;
                            if (modFreq < 0) modFreq = 0;

                            if (vo->type == WAVE_STREAM && vo->streamTrackId >= 0) {
                                StreamTrack* trk = &streams[vo->streamTrackId];
                                if (trk->rootFreqCentiHz > 0) {
                                    uint64_t ratio1616 = ((uint64_t)modFreq << 16) / trk->rootFreqCentiHz;
                                    vo->sampleInc1616 = (uint32_t)((ratio1616 * trk->sampleRate) / sr);
                                }
                            } else if (vo->type == WAVE_SAMPLE) {
                                if (vo->instSample != nullptr) {
                                    const SampleData* sData = nullptr;
                                    uint32_t root = 0;
                                    for (int zIdx = 0; zIdx < vo->instSample->numZones; zIdx++) {
                                        const SampleZone* z = &vo->instSample->zones[zIdx];
                                        if (vo->freqVal >= z->lowFreq && vo->freqVal <= z->highFreq) {
                                            if (z->sampleId < MAX_SAMPLES) {
                                                sData = &registeredSamples[z->sampleId];
                                                root = (z->rootOverride > 0) ? z->rootOverride : sData->rootFreqCentiHz;
                                            }
                                            break;
                                        }
                                    }
                                    if (sData && sData->data && root > 0) {
                                        uint64_t ratio1616 = ((uint64_t)modFreq << 16) / root;
                                        vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / sr);
                                    }
                                } else {
                                    const SampleData* sData = &registeredSamples[vo->curSampleId];
                                    if (sData->data && sData->rootFreqCentiHz > 0) {
                                        uint64_t ratio1616 = ((uint64_t)modFreq << 16) / sData->rootFreqCentiHz;
                                        vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / sr);
                                    }
                                }
                            }
                        }
                    }
                    break;
                }

                case LFO_TARGET_VOICE_VOL: {
                    int32_t gainQ8;
                    if (wantRange) {
                        gainQ8 = lfoClampI32(valMapped, 0, 512);
                    } else {
                        int32_t depth = lfoClampI32(rt->valA, -256, 256);
                        int32_t uni16 = (int32_t)lfoUni;
                        int32_t delta = (depth >= 0) ? ((depth * (65535 - uni16)) >> 16) : (((-depth) * uni16) >> 16);
                        gainQ8 = lfoClampI32(256 - delta, 0, 256);
                    }
                    for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                        Voice* vo = &voices[v];
                        if (!vo->active) continue;
                        int32_t g = ((int32_t)vo->trmModGain * gainQ8) >> 8;
                        vo->trmModGain = (uint16_t)lfoClampI32(g, 0, 512);
                    }
                    break;
                }

                case LFO_TARGET_VOICE_PAN: {
#if SYNTH_STEREO
                    int32_t maxPan = (1L << (_panShift - 1)) - 1;
                    if (maxPan < 1) maxPan = 1;
                    for (uint16_t v = rt->targetStart; v <= rt->targetEnd && v < MAX_VOICES; v++) {
                        Voice* vo = &voices[v];
                        if (!vo->active) continue;
                        int32_t pan16;
                        if (wantRange) {
                            int32_t pUser = lfoClampI32(valMapped, -maxPan, maxPan);
                            pan16 = (pUser >= 0) ? ((pUser * 32767) / maxPan) : -((-pUser * 32767) / maxPan);
                        } else {
                            int32_t d16 = (int32_t)(((int64_t)newOff * 32767LL) / maxPan);
                            pan16 = (int32_t)vo->pan + d16;
                        }
                        if (vo->panLfoDepth > 0) {
                            pan16 += ((int32_t)vo->panLfoDepth * sineLUT[vo->panLfoPhase >> SINE_SHIFT]) >> 15;
                        }
                        updateVoicePanGains(vo, pan16);
                    }
#endif
                    break;
                }

                case LFO_TARGET_BUS_FX_PARAM: {
                    if (rt->targetStart >= MAX_BUSES || rt->subId >= MAX_FX_PER_BUS || rt->paramIdx >= SYNTH_FX_PARAMS) break;
                    FXNode* fx = &busesConfig[rt->targetStart].fxChain[rt->subId];
                    if (!fx->active) break;

                    if (wantRange) {
                        fx->ep[rt->paramIdx] = (int16_t)lfoClampI32(valMapped, -32768, 32767);
                    } else {
                        int32_t base    = (int32_t)fx->ep[rt->paramIdx] - rt->lastOffset;
                        int32_t cl      = lfoClampI32(base + newOff, -32768, 32767);
                        fx->ep[rt->paramIdx] = (int16_t)cl;
                        rt->lastOffset  = cl - base;
                    }
                    break;
                }

                case LFO_TARGET_BUS_MIX: {
                    if (rt->targetStart >= MAX_BUSES) break;
                    AudioBus* bus = &busesConfig[rt->targetStart];
                    if (wantRange) {
                        bus->mixLevel = (uint8_t)lfoClampI32(valMapped, 0, 255);
                    } else {
                        int32_t base   = (int32_t)bus->mixLevel - rt->lastOffset;
                        int32_t cl     = lfoClampI32(base + newOff, 0, 255);
                        bus->mixLevel  = (uint8_t)cl;
                        rt->lastOffset = cl - base;
                    }
                    bus->active = true;
                    break;
                }

                case LFO_TARGET_BUS_PAN: {
#if SYNTH_STEREO
                    if (rt->targetStart >= MAX_BUSES) break;
                    AudioBus* bus = &busesConfig[rt->targetStart];
                    int32_t maxPan = (1L << (_panShift - 1)) - 1;
                    if (maxPan < 1) maxPan = 1;

                    if (wantRange) {
                        int32_t pUser = lfoClampI32(valMapped, -maxPan, maxPan);
                        bus->pan = (int16_t)((pUser >= 0) ? ((pUser * 32767) / maxPan) : -((-pUser * 32767) / maxPan));
                    } else {
                        int32_t d16  = (int32_t)(((int64_t)newOff * 32767LL) / maxPan);
                        int32_t base = (int32_t)bus->pan - rt->lastOffset;
                        int32_t cl   = lfoClampI32(base + d16, -32767, 32767);
                        bus->pan     = (int16_t)cl;
                        rt->lastOffset = cl - base;
                    }
                    computePanLawGains(bus->pan, true, bus->panGainL, bus->panGainR);
                    bus->active = true;
#endif
                    break;
                }

                case LFO_TARGET_BUS_WIDTH: {
#if SYNTH_STEREO
                    if (rt->targetStart >= MAX_BUSES) break;
                    AudioBus* bus = &busesConfig[rt->targetStart];
                    if (wantRange) {
                        bus->stereoWidth = (uint16_t)lfoClampI32(valMapped, 0, 512);
                    } else {
                        int32_t base     = (int32_t)bus->stereoWidth - rt->lastOffset;
                        int32_t cl       = lfoClampI32(base + newOff, 0, 512);
                        bus->stereoWidth = (uint16_t)cl;
                        rt->lastOffset   = cl - base;
                    }
                    bus->active = true;
#endif
                    break;
                }

                case LFO_TARGET_DSP_PARAM: {
                    if (rt->paramIdx >= SYNTH_CUSTOM_PARAMS_GLOBAL) break;
                    if (wantRange) {
                        dp[rt->paramIdx] = (int16_t)lfoClampI32(valMapped, -32768, 32767);
                    } else {
                        int32_t base   = (int32_t)dp[rt->paramIdx] - rt->lastOffset;
                        int32_t cl     = lfoClampI32(base + newOff, -32768, 32767);
                        dp[rt->paramIdx] = (int16_t)cl;
                        rt->lastOffset = cl - base;
                    }
                    break;
                }

                case LFO_TARGET_MASTER_VOL: {
                    int32_t maxV = (int32_t)((1UL << (16U - _volShift)) - 1UL);
                    int32_t cl;
                    if (wantRange) {
                        cl = lfoClampI32(valMapped, 0, maxV);
                    } else {
                        int32_t base   = (int32_t)(_masterVolume >> _volShift) - rt->lastOffset;
                        cl             = lfoClampI32(base + newOff, 0, maxV);
                        rt->lastOffset = cl - base;
                    }
                    uint32_t vCalc = (uint32_t)cl << _volShift;
                    _masterVolume = (vCalc > 65535U) ? 65535U : (uint16_t)vCalc;
                    break;
                }

                case LFO_TARGET_MASTER_PAN: {
#if SYNTH_STEREO
                    int32_t maxPan = (1L << (_panShift - 1)) - 1;
                    if (maxPan < 1) maxPan = 1;
                    if (wantRange) {
                        int32_t pUser = lfoClampI32(valMapped, -maxPan, maxPan);
                        _masterPan = (int16_t)((pUser >= 0) ? ((pUser * 32767) / maxPan) : -((-pUser * 32767) / maxPan));
                    } else {
                        int32_t d16    = (int32_t)(((int64_t)newOff * 32767LL) / maxPan);
                        int32_t base   = (int32_t)_masterPan - rt->lastOffset;
                        int32_t cl     = lfoClampI32(base + d16, -32767, 32767);
                        _masterPan     = (int16_t)cl;
                        rt->lastOffset = cl - base;
                    }
                    computePanLawGains(_masterPan, true, _masterPanGainL, _masterPanGainR);
#endif
                    break;
                }

                case LFO_TARGET_MASTER_WIDTH: {
#if SYNTH_STEREO
                    if (wantRange) {
                        _masterWidth = (uint16_t)lfoClampI32(valMapped, 0, 512);
                    } else {
                        int32_t base   = (int32_t)_masterWidth - rt->lastOffset;
                        int32_t cl     = lfoClampI32(base + newOff, 0, 512);
                        _masterWidth   = (uint16_t)cl;
                        rt->lastOffset = cl - base;
                    }
#endif
                    break;
                }

                case LFO_TARGET_LFO_RATE: {
                    if (rt->targetStart < MAX_LFOS) {
                        ModLFO* dst = &lfos[rt->targetStart];
                        if (wantRange) {
                            dst->effRate = (uint32_t)lfoClampI32(valMapped, 0, 50000);
                        } else {
                            int32_t cl = lfoClampI32((int32_t)dst->effRate + newOff, 0, 50000);
                            dst->effRate = (uint32_t)cl;
                        }
                    }
                    break;
                }

                case LFO_TARGET_LFO_AMP: {
                    if (rt->targetStart < MAX_LFOS) {
                        ModLFO* dst = &lfos[rt->targetStart];
                        if (wantRange) {
                            dst->effAmpQ8 = (int16_t)lfoClampI32(valMapped, 0, 512);
                        } else {
                            int32_t depth = lfoClampI32(rt->valA, -256, 256);
                            int32_t uni16 = (int32_t)lfoUni;
                            int32_t delta = (depth >= 0) ? ((depth * (65535 - uni16)) >> 16) : (((-depth) * uni16) >> 16);
                            dst->effAmpQ8 = (int16_t)lfoClampI32(((int32_t)dst->effAmpQ8 * (256 - delta)) >> 8, 0, 512);
                        }
                    }
                    break;
                }

                default: break;
            }
        }
    }
}