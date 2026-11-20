#pragma once
#include "ESP32Synth.h"

#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

struct FXFilterState {
    int64_t  s1L, s2L;
#if SYNTH_STEREO
    int64_t  s1R, s2R;
#endif
    int32_t  c_b0, c_b1, c_b2, c_a1, c_a2;
    int32_t  cachedFc;
    int32_t  cachedQ;
    int16_t  cachedMode;
    int16_t  cachedBwMode;
    uint32_t cachedSampleRate;
};

struct FXPhaserState {
    int32_t  apL[8];
    int32_t  fbL;
#if SYNTH_STEREO
    int32_t  apR[8];
    int32_t  fbR;
#endif
    uint32_t lfoPhase;
    int32_t  aL;
#if SYNTH_STEREO
    int32_t  aR;
#endif
};

struct FXModDelayState {
#if SYNTH_STEREO
    StereoSample* buf;
#else
    int16_t*      buf;
#endif
    uint32_t mask;
    uint32_t writeIdx;
    uint32_t lfoPhase;
};

struct FXDelayState {
#if SYNTH_STEREO
    StereoSample* buf;
#else
    int16_t*      buf;
#endif
    uint32_t capacity;
    uint32_t writeIdx;
    uint32_t currDelayQ8;
    int32_t  lpL;
#if SYNTH_STEREO
    int32_t  lpR;
#endif
};

struct FXReverbState {
    int16_t* slab;
    int16_t* combL[4];
    int16_t* apL[2];
    uint16_t combLenL[4], combIdxL[4];
    uint16_t apLenL[2], apIdxL[2];
    int32_t  combDampL[4];
    int32_t  dcL;
#if SYNTH_STEREO
    int16_t* combR[4];
    int16_t* apR[2];
    uint16_t combLenR[4], combIdxR[4];
    uint16_t apLenR[2], apIdxR[2];
    int32_t  combDampR[4];
    int32_t  dcR;
#endif
};

static inline int32_t fxClampInt(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int32_t fxSoftCubic(int32_t x) {
    if (x > 32768) x = 32768;
    else if (x < -32768) x = -32768;
    int32_t x2 = (x * x) >> 15;
    int32_t x3 = (x2 * x) >> 15;
    return ((3 * x) - x3) >> 1;
}

static inline void fxComputeBiquadCoeffs(FXFilterState* st, int32_t rawFc, int16_t mode, int32_t q_param, int16_t bwMode, uint32_t sampleRate) {
    int32_t maxFc = (int32_t)((sampleRate * 45ULL) / 100ULL);
    if (maxFc < 20) maxFc = 20;
    int32_t fc = fxClampInt(rawFc, 20, maxFc);

    int32_t sin_q28, cos_q28, omc_q28;
    uint32_t w0_q28 = (uint32_t)(((uint64_t)fc * 1686629713ULL) / sampleRate);

    if (w0_q28 < (1U << 26)) {
        int32_t w0_sq = (int32_t)(((int64_t)w0_q28 * w0_q28) >> 28);
        int32_t w0_cu = (int32_t)(((int64_t)w0_sq * w0_q28) >> 28);
        int32_t w0_qu = (int32_t)(((int64_t)w0_sq * w0_sq) >> 28);
        sin_q28 = (int32_t)w0_q28 - (w0_cu / 6);
        omc_q28 = (w0_sq >> 1) - (w0_qu / 24);
        cos_q28 = (1 << 28) - omc_q28;
    } else {
        uint32_t ph_fixed = (uint32_t)(((uint64_t)fc << 28) / sampleRate);
        uint32_t idx  = (ph_fixed >> 16) & SINE_LUT_MASK;
        uint32_t frac = ph_fixed & 0xFFFFU;
        int32_t s0 = sineLUT[idx];
        int32_t s1 = sineLUT[idx + 1];
        sin_q28 = (s0 + (int32_t)(((int64_t)(s1 - s0) * frac) >> 16)) << 13;

        uint32_t idx_c = (idx + (SINE_LUT_SIZE / 4)) & SINE_LUT_MASK;
        int32_t cos0 = sineLUT[idx_c];
        int32_t cos1 = sineLUT[idx_c + 1];
        cos_q28 = (cos0 + (int32_t)(((int64_t)(cos1 - cos0) * frac) >> 16)) << 13;
        omc_q28 = (1 << 28) - cos_q28;
    }

    int32_t alpha_q28;
    if (bwMode == 1) {
        int32_t bw_hz = (q_param < 1) ? 1 : q_param;
        alpha_q28 = (int32_t)(((int64_t)sin_q28 * bw_hz) / ((int64_t)fc << 1));
    } else {
        int32_t q_fixed = (q_param < 10) ? 10 : q_param;
        alpha_q28 = (int32_t)(((int64_t)sin_q28 * 50) / q_fixed);
    }
    if (alpha_q28 < 1) alpha_q28 = 1;

    int32_t k_a0 = (1 << 28) + alpha_q28;
    int32_t k_a2 = (1 << 28) - alpha_q28;
    int32_t k_b0, k_b1, k_b2;
    int64_t a1_val = -((int64_t)cos_q28 << 1);

    if (mode == 1) {
        int32_t one_p_cos = (1 << 28) + cos_q28;
        k_b0 = one_p_cos >> 1;
        k_b1 = -one_p_cos;
        k_b2 = k_b0;
    } else if (mode == 2) {
        k_b0 = alpha_q28;
        k_b1 = 0;
        k_b2 = -alpha_q28;
    } else if (mode == 3) {
        k_b0 = (1 << 28);
        k_b1 = (int32_t)a1_val;
        k_b2 = k_b0;
    } else {
        k_b0 = omc_q28 >> 1;
        k_b1 = omc_q28;
        k_b2 = k_b0;
    }

    st->c_b0 = (int32_t)(((int64_t)k_b0 << 28) / k_a0);
    st->c_b1 = (int32_t)(((int64_t)k_b1 << 28) / k_a0);
    st->c_b2 = (int32_t)(((int64_t)k_b2 << 28) / k_a0);
    st->c_a1 = (int32_t)((a1_val << 28) / k_a0);
    st->c_a2 = (int32_t)(((int64_t)k_a2 << 28) / k_a0);

    st->cachedFc         = rawFc;
    st->cachedQ          = q_param;
    st->cachedMode       = mode;
    st->cachedBwMode     = bwMode;
    st->cachedSampleRate = sampleRate;
}

static inline int32_t fxPhaserComputeCoeff(uint32_t lfoPhase, int32_t depth, uint32_t sampleRate) {
    int32_t sinVal = sineLUT[lfoPhase >> SINE_SHIFT];
    int32_t u = ((sinVal + 32768) * depth) >> 8;
    uint32_t u2 = ((uint32_t)u * (uint32_t)u) >> 16;
    int32_t maxFc = (int32_t)((sampleRate * 35ULL) / 100ULL);
    int32_t hiFc = (3600 < maxFc) ? 3600 : maxFc;
    int32_t loFc = (180 < hiFc) ? 180 : hiFc;
    int32_t fc = loFc + (int32_t)(((uint64_t)(hiFc - loFc) * u2) >> 16);

    int32_t x_q24 = (int32_t)(((uint64_t)fc * 52707179ULL) / sampleRate);
    int32_t x2 = (int32_t)(((int64_t)x_q24 * x_q24) >> 24);
    int32_t x3 = (int32_t)(((int64_t)x2 * x_q24) >> 24);
    int32_t k_q24 = x_q24 + (x3 / 3);
    if (k_q24 > 16000000) k_q24 = 16000000;
    return (int32_t)((((int64_t)((1 << 24) - k_q24)) << 24) / ((1 << 24) + k_q24));
}

#if SYNTH_STEREO

static inline void fxOverdriveStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples) {
    int32_t drive = fxClampInt(fx->ep[FX_OD_DRIVE], 0, 255);
    int32_t tone  = fxClampInt(fx->ep[FX_OD_TONE],  0, 255);
    int32_t level = fxClampInt(fx->ep[FX_OD_LEVEL], 0, 255);
    int32_t mode  = fxClampInt(fx->ep[FX_OD_MODE],  0, 2);
    int32_t mix   = fxClampInt(fx->ep[FX_OD_MIX],   0, 255);

    int32_t gain = 256 + ((drive * drive * 31) >> 8);
    if (mode == 2) gain <<= 1;
    int32_t tc = 12 + ((tone * 245) >> 8);

    int32_t lpL = fx->es[0], dcL = fx->es[1];
    int32_t lpR = fx->es[2], dcR = fx->es[3];

    for (int i = 0; i < samples; i++) {
        int32_t dryL = busL[i], dryR = busR[i];
        int32_t xL = (int32_t)(((int64_t)dryL * gain) >> 8);
        int32_t xR = (int32_t)(((int64_t)dryR * gain) >> 8);
        int32_t yL, yR;

        if (mode == 1) {
            yL = fxSoftCubic(xL);
            if (xL >= 0) yL = (yL * 210) >> 8;
            int32_t dcoL = dcL >> 9; dcL += yL - dcoL; yL -= dcoL;

            yR = fxSoftCubic(xR);
            if (xR >= 0) yR = (yR * 210) >> 8;
            int32_t dcoR = dcR >> 9; dcR += yR - dcoR; yR -= dcoR;
        } else if (mode == 2) {
            xL = fxClampInt(xL, -24576, 24576);
            xR = fxClampInt(xR, -24576, 24576);
            yL = (xL * 341) >> 8;
            yR = (xR * 341) >> 8;
        } else {
            yL = fxSoftCubic(xL);
            yR = fxSoftCubic(xR);
        }

        lpL += ((yL - lpL) * tc) >> 8;
        lpR += ((yR - lpR) * tc) >> 8;
        int32_t wetL = (lpL * level) >> 8;
        int32_t wetR = (lpR * level) >> 8;

        if (mix == 255) {
            busL[i] = wetL;
            busR[i] = wetR;
        } else {
            busL[i] = dryL + (((wetL - dryL) * mix) >> 8);
            busR[i] = dryR + (((wetR - dryR) * mix) >> 8);
        }
    }

    fx->es[0] = lpL; fx->es[1] = dcL;
    fx->es[2] = lpR; fx->es[3] = dcR;
}

static inline void fxChorusStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples, uint32_t sampleRate) {
    FXModDelayState* st = (FXModDelayState*)fx->fxData;
    if (UNLIKELY(!st || !st->buf)) return;

    int32_t rate    = fxClampInt(fx->ep[FX_CHORUS_RATE], 1, 3000);
    int32_t depth   = fxClampInt(fx->ep[FX_CHORUS_DEPTH], 0, 255);
    int32_t delayMs = fxClampInt(fx->ep[FX_CHORUS_DELAY_MS], 1, 30);
    int32_t fbQ8    = fxClampInt(fx->ep[FX_CHORUS_FEEDBACK], -200, 200);
    int32_t mix     = fxClampInt(fx->ep[FX_CHORUS_MIX], 0, 255);
    int32_t stPhase = fxClampInt(fx->ep[FX_CHORUS_STEREO_PHASE], 0, 180);

    const uint32_t mask = st->mask;
    int32_t baseFrames = fxClampInt((int32_t)(((uint64_t)delayMs * sampleRate) / 1000ULL), 2, (int32_t)mask - 4);
    int32_t maxSweepFrames = (int32_t)((sampleRate * 6ULL) / 1000ULL);
    int32_t sweepAmp = (depth * maxSweepFrames) >> 8;

    uint32_t lfoInc  = (uint32_t)(((uint64_t)rate * 4294967296ULL) / ((uint64_t)sampleRate * 100ULL));
    uint32_t lfoOffR = (uint32_t)(((uint64_t)stPhase << 32) / 360ULL);
    uint32_t lfoPh = st->lfoPhase;
    uint32_t wIdx  = st->writeIdx;
    int32_t maxDelQ8 = ((int32_t)mask - 2) << 8;
    StereoSample* buf = st->buf;

    for (int i = 0; i < samples; i++) {
        int32_t sinL = sineLUT[lfoPh >> SINE_SHIFT];
        int32_t sinR = sineLUT[(lfoPh + lfoOffR) >> SINE_SHIFT];
        int32_t delQ8L = fxClampInt((baseFrames << 8) + ((sinL * sweepAmp) >> 7), 256, maxDelQ8);
        int32_t delQ8R = fxClampInt((baseFrames << 8) + ((sinR * sweepAmp) >> 7), 256, maxDelQ8);

        uint32_t i0L = (wIdx - (uint32_t)(delQ8L >> 8)) & mask;
        uint32_t i1L = (i0L - 1U) & mask;
        int32_t s0L = buf[i0L].l;
        int32_t dL  = s0L + (((buf[i1L].l - s0L) * (delQ8L & 0xFF)) >> 8);

        uint32_t i0R = (wIdx - (uint32_t)(delQ8R >> 8)) & mask;
        uint32_t i1R = (i0R - 1U) & mask;
        int32_t s0R = buf[i0R].r;
        int32_t dR  = s0R + (((buf[i1R].r - s0R) * (delQ8R & 0xFF)) >> 8);

        int32_t dryL = busL[i], dryR = busR[i];
        buf[wIdx].l = (int16_t)hw_clamps16((dryL >> 1) + ((dL * fbQ8) >> 8));
        buf[wIdx].r = (int16_t)hw_clamps16((dryR >> 1) + ((dR * fbQ8) >> 8));

        int32_t wetL = dL << 1;
        int32_t wetR = dR << 1;
        busL[i] = dryL + (((wetL - dryL) * mix) >> 8);
        busR[i] = dryR + (((wetR - dryR) * mix) >> 8);

        wIdx = (wIdx + 1U) & mask;
        lfoPh += lfoInc;
    }

    st->writeIdx = wIdx;
    st->lfoPhase = lfoPh;
}

static inline void fxFlangerStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples, uint32_t sampleRate) {
    FXModDelayState* st = (FXModDelayState*)fx->fxData;
    if (UNLIKELY(!st || !st->buf)) return;

    int32_t rate    = fxClampInt(fx->ep[FX_FLANGER_RATE], 1, 3000);
    int32_t depth   = fxClampInt(fx->ep[FX_FLANGER_DEPTH], 0, 255);
    int32_t del10   = fxClampInt(fx->ep[FX_FLANGER_DELAY_MS10], 1, 100);
    int32_t fbQ8    = fxClampInt(fx->ep[FX_FLANGER_FEEDBACK], -230, 230);
    int32_t mix     = fxClampInt(fx->ep[FX_FLANGER_MIX], 0, 255);
    int32_t stPhase = fxClampInt(fx->ep[FX_FLANGER_STEREO_PHASE], 0, 180);

    const uint32_t mask = st->mask;
    int32_t baseQ8 = (int32_t)(((uint64_t)del10 * sampleRate * 256ULL) / 10000ULL);
    if (baseQ8 < 256) baseQ8 = 256;
    int32_t maxSweepQ8 = (int32_t)(((uint64_t)sampleRate * 5ULL * 256ULL) / 1000ULL);
    int32_t sweepRangeQ8 = (int32_t)(((int64_t)maxSweepQ8 * depth) >> 8);

    uint32_t lfoInc  = (uint32_t)(((uint64_t)rate * 4294967296ULL) / ((uint64_t)sampleRate * 100ULL));
    uint32_t lfoOffR = (uint32_t)(((uint64_t)stPhase << 32) / 360ULL);
    uint32_t lfoPh = st->lfoPhase;
    uint32_t wIdx  = st->writeIdx;
    int32_t maxDelQ8 = ((int32_t)mask - 2) << 8;
    StereoSample* buf = st->buf;

    for (int i = 0; i < samples; i++) {
        int32_t uL = sineLUT[lfoPh >> SINE_SHIFT] + 32768;
        int32_t uR = sineLUT[(lfoPh + lfoOffR) >> SINE_SHIFT] + 32768;
        int32_t delQ8L = fxClampInt(baseQ8 + (int32_t)(((int64_t)uL * sweepRangeQ8) >> 16), 256, maxDelQ8);
        int32_t delQ8R = fxClampInt(baseQ8 + (int32_t)(((int64_t)uR * sweepRangeQ8) >> 16), 256, maxDelQ8);

        uint32_t i0L = (wIdx - (uint32_t)(delQ8L >> 8)) & mask;
        uint32_t i1L = (i0L - 1U) & mask;
        int32_t s0L = buf[i0L].l;
        int32_t dL  = s0L + (((buf[i1L].l - s0L) * (delQ8L & 0xFF)) >> 8);

        uint32_t i0R = (wIdx - (uint32_t)(delQ8R >> 8)) & mask;
        uint32_t i1R = (i0R - 1U) & mask;
        int32_t s0R = buf[i0R].r;
        int32_t dR  = s0R + (((buf[i1R].r - s0R) * (delQ8R & 0xFF)) >> 8);

        int32_t dryL = busL[i], dryR = busR[i];
        buf[wIdx].l = (int16_t)hw_clamps16((dryL >> 1) + ((dL * fbQ8) >> 8));
        buf[wIdx].r = (int16_t)hw_clamps16((dryR >> 1) + ((dR * fbQ8) >> 8));

        int32_t wetL = dL << 1;
        int32_t wetR = dR << 1;
        busL[i] = dryL + (((wetL - dryL) * mix) >> 8);
        busR[i] = dryR + (((wetR - dryR) * mix) >> 8);

        wIdx = (wIdx + 1U) & mask;
        lfoPh += lfoInc;
    }

    st->writeIdx = wIdx;
    st->lfoPhase = lfoPh;
}

static inline void fxPhaserStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples, uint32_t sampleRate) {
    FXPhaserState* st = (FXPhaserState*)fx->fxData;
    if (UNLIKELY(!st)) return;

    int32_t rate    = fxClampInt(fx->ep[FX_PHASER_RATE], 1, 3000);
    int32_t depth   = fxClampInt(fx->ep[FX_PHASER_DEPTH], 0, 255);
    int32_t fbQ8    = fxClampInt(fx->ep[FX_PHASER_FEEDBACK], -220, 220);
    int32_t stages  = fxClampInt(fx->ep[FX_PHASER_STAGES], 2, 8) & ~1;
    if (stages < 2) stages = 2;
    int32_t mix     = fxClampInt(fx->ep[FX_PHASER_MIX], 0, 255);
    int32_t stPhase = fxClampInt(fx->ep[FX_PHASER_STEREO_PHASE], 0, 180);

    uint32_t lfoInc  = (uint32_t)(((uint64_t)rate * 4294967296ULL) / ((uint64_t)sampleRate * 100ULL));
    uint32_t lfoOffR = (uint32_t)(((uint64_t)stPhase << 32) / 360ULL);
    uint32_t lfoPh = st->lfoPhase;

    int32_t aL = st->aL, aR = st->aR;
    int32_t fbL = st->fbL, fbR = st->fbR;
    const int32_t clampSig = 524288 << 10;

    for (int i = 0; i < samples; i++) {
        if ((i & 15) == 0) {
            aL = fxPhaserComputeCoeff(lfoPh, depth, sampleRate);
            aR = fxPhaserComputeCoeff(lfoPh + lfoOffR, depth, sampleRate);
        }

        int32_t dryL = busL[i], dryR = busR[i];

        int32_t sigL = (dryL << 10) + (int32_t)(((int64_t)fbL * fbQ8) >> 8);
        if (UNLIKELY(sigL > clampSig)) sigL = clampSig;
        else if (UNLIKELY(sigL < -clampSig)) sigL = -clampSig;

        int32_t sigR = (dryR << 10) + (int32_t)(((int64_t)fbR * fbQ8) >> 8);
        if (UNLIKELY(sigR > clampSig)) sigR = clampSig;
        else if (UNLIKELY(sigR < -clampSig)) sigR = -clampSig;

        for (int s = 0; s < stages; s++) {
            int32_t yL = -(int32_t)(((int64_t)aL * sigL) >> 24) + st->apL[s];
            st->apL[s] = sigL + (int32_t)(((int64_t)aL * yL) >> 24);
            sigL = yL;

            int32_t yR = -(int32_t)(((int64_t)aR * sigR) >> 24) + st->apR[s];
            st->apR[s] = sigR + (int32_t)(((int64_t)aR * yR) >> 24);
            sigR = yR;
        }

        fbL = sigL;
        fbR = sigR;

        int32_t phasedL = (dryL + (sigL >> 10)) >> 1;
        int32_t phasedR = (dryR + (sigR >> 10)) >> 1;
        busL[i] = dryL + (((phasedL - dryL) * mix) >> 8);
        busR[i] = dryR + (((phasedR - dryR) * mix) >> 8);

        lfoPh += lfoInc;
    }

    st->lfoPhase = lfoPh;
    st->aL = aL; st->aR = aR;
    st->fbL = fbL; st->fbR = fbR;
}

static inline void fxDelayStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples, uint32_t sampleRate) {
    FXDelayState* st = (FXDelayState*)fx->fxData;
    if (UNLIKELY(!st || !st->buf || st->capacity < 4)) return;

    int32_t delayMs  = fxClampInt(fx->ep[FX_DELAY_TIME_MS], 1, 30000);
    int32_t fbQ8     = fxClampInt(fx->ep[FX_DELAY_FEEDBACK], 0, 245);
    int32_t damp     = fxClampInt(fx->ep[FX_DELAY_DAMP], 0, 255);
    bool    pingPong = (fx->ep[FX_DELAY_PINGPONG] != 0);
    int32_t mix      = fxClampInt(fx->ep[FX_DELAY_MIX], 0, 255);

    uint32_t cap = st->capacity;
    uint32_t targetFrames = (uint32_t)(((uint64_t)delayMs * sampleRate) / 1000ULL);
    if (targetFrames < 1U) targetFrames = 1U;
    if (targetFrames > cap - 2U) targetFrames = cap - 2U;
    uint32_t targetQ8 = targetFrames << 8;

    uint32_t currQ8 = st->currDelayQ8;
    if (UNLIKELY(currQ8 == 0)) currQ8 = targetQ8;
    if (currQ8 > ((cap - 2U) << 8)) currQ8 = (cap - 2U) << 8;

    int32_t dampCoef = 256 - ((damp * 224) >> 8);
    uint32_t wIdx = st->writeIdx;
    int32_t lpL = st->lpL, lpR = st->lpR;
    StereoSample* buf = st->buf;

    for (int i = 0; i < samples; i++) {
        if (UNLIKELY(currQ8 != targetQ8)) {
            int32_t diff = (int32_t)targetQ8 - (int32_t)currQ8;
            int32_t step = diff >> 10;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            currQ8 = (uint32_t)((int32_t)currQ8 + step);
        }

        uint32_t intDel = currQ8 >> 8;
        int32_t  frac   = (int32_t)(currQ8 & 0xFFU);
        uint32_t r0 = (wIdx >= intDel) ? (wIdx - intDel) : (wIdx + cap - intDel);
        uint32_t r1 = (r0 > 0U) ? (r0 - 1U) : (cap - 1U);

        StereoSample s0 = buf[r0];
        StereoSample s1 = buf[r1];
        int32_t dL = s0.l + (((s1.l - s0.l) * frac) >> 8);
        int32_t dR = s0.r + (((s1.r - s0.r) * frac) >> 8);

        lpL += ((dL - lpL) * dampCoef) >> 8;
        lpR += ((dR - lpR) * dampCoef) >> 8;
        int32_t fbL = (lpL * fbQ8) >> 8;
        int32_t fbR = (lpR * fbQ8) >> 8;

        int32_t dryL = busL[i], dryR = busR[i];
        if (pingPong) {
            int32_t monoIn = (dryL + dryR) >> 2;
            buf[wIdx].l = (int16_t)hw_clamps16(monoIn + fbR);
            buf[wIdx].r = (int16_t)hw_clamps16(fbL);
        } else {
            buf[wIdx].l = (int16_t)hw_clamps16((dryL >> 1) + fbL);
            buf[wIdx].r = (int16_t)hw_clamps16((dryR >> 1) + fbR);
        }

        int32_t wetL = dL << 1;
        int32_t wetR = dR << 1;
        busL[i] = dryL + (((wetL - dryL) * mix) >> 8);
        busR[i] = dryR + (((wetR - dryR) * mix) >> 8);

        if (UNLIKELY(++wIdx >= cap)) wIdx = 0;
    }

    st->writeIdx    = wIdx;
    st->currDelayQ8 = currQ8;
    st->lpL         = lpL;
    st->lpR         = lpR;
}

static inline void fxReverbStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples) {
    FXReverbState* st = (FXReverbState*)fx->fxData;
    if (UNLIKELY(!st || !st->slab)) return;

    int32_t room  = fxClampInt(fx->ep[FX_REVERB_ROOM],  0, 255);
    int32_t damp  = fxClampInt(fx->ep[FX_REVERB_DAMP],  0, 255);
    int32_t width = fxClampInt(fx->ep[FX_REVERB_WIDTH], 0, 255);
    int32_t mix   = fxClampInt(fx->ep[FX_REVERB_MIX],   0, 255);

    int32_t fbQ15  = 23593 + ((room * 7864) >> 8);
    int32_t lpCoef = 256 - ((damp * 200) >> 8);
    int32_t dcL = st->dcL;
    int32_t dcR = st->dcR;

    for (int i = 0; i < samples; i++) {
        int32_t dryL = busL[i], dryR = busR[i];
        int32_t acL = dryL - (dcL >> 9);
        dcL += acL;
        int32_t acR = dryR - (dcR >> 9);
        dcR += acR;
        int32_t inL = acL >> 1;
        int32_t inR = acR >> 1;
        int32_t accL = 0, accR = 0;

        for (int c = 0; c < 4; c++) {
            uint16_t idxL = st->combIdxL[c];
            int16_t* bufL = st->combL[c];
            int32_t cOutL = bufL[idxL];
            int32_t dStateL = st->combDampL[c];
            dStateL += (((cOutL << 8) - dStateL) * lpCoef) >> 8;
            st->combDampL[c] = dStateL;
            bufL[idxL] = (int16_t)hw_clamps16(inL + (((dStateL >> 8) * fbQ15) >> 15));
            if (UNLIKELY(++idxL >= st->combLenL[c])) idxL = 0;
            st->combIdxL[c] = idxL;
            accL += cOutL;

            uint16_t idxR = st->combIdxR[c];
            int16_t* bufR = st->combR[c];
            int32_t cOutR = bufR[idxR];
            int32_t dStateR = st->combDampR[c];
            dStateR += (((cOutR << 8) - dStateR) * lpCoef) >> 8;
            st->combDampR[c] = dStateR;
            bufR[idxR] = (int16_t)hw_clamps16(inR + (((dStateR >> 8) * fbQ15) >> 15));
            if (UNLIKELY(++idxR >= st->combLenR[c])) idxR = 0;
            st->combIdxR[c] = idxR;
            accR += cOutR;
        }

        int32_t sigL = hw_clamps16(accL >> 1);
        int32_t sigR = hw_clamps16(accR >> 1);

        for (int a = 0; a < 2; a++) {
            uint16_t idxL = st->apIdxL[a];
            int16_t* bufL = st->apL[a];
            int32_t bOutL = bufL[idxL];
            int32_t newL  = hw_clamps16(sigL + (bOutL >> 1));
            sigL = bOutL - (newL >> 1);
            bufL[idxL] = (int16_t)newL;
            if (UNLIKELY(++idxL >= st->apLenL[a])) idxL = 0;
            st->apIdxL[a] = idxL;

            uint16_t idxR = st->apIdxR[a];
            int16_t* bufR = st->apR[a];
            int32_t bOutR = bufR[idxR];
            int32_t newR  = hw_clamps16(sigR + (bOutR >> 1));
            sigR = bOutR - (newR >> 1);
            bufR[idxR] = (int16_t)newR;
            if (UNLIKELY(++idxR >= st->apLenR[a])) idxR = 0;
            st->apIdxR[a] = idxR;
        }

        if (width < 255) {
            int32_t mid  = (sigL + sigR) >> 1;
            int32_t side = ((sigL - sigR) * width) >> 9;
            sigL = mid + side;
            sigR = mid - side;
        }

        busL[i] = dryL + (((sigL - dryL) * mix) >> 8);
        busR[i] = dryR + (((sigR - dryR) * mix) >> 8);
    }
    st->dcL = dcL;
    st->dcR = dcR;
}

static inline void fxFilterStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples, uint32_t sampleRate) {
    FXFilterState* st = (FXFilterState*)fx->fxData;
    if (UNLIKELY(!st)) return;

    int32_t fc     = fx->ep[FX_FILTER_CUTOFF];
    int16_t mode   = fx->ep[FX_FILTER_MODE];
    int32_t q      = fx->ep[FX_FILTER_RESONANCE];
    int16_t bwMode = fx->ep[FX_FILTER_BW_MODE];
    int32_t mix    = fxClampInt(fx->ep[FX_FILTER_MIX], 0, 255);

    if (UNLIKELY(fc != st->cachedFc || q != st->cachedQ || mode != st->cachedMode ||
                 bwMode != st->cachedBwMode || sampleRate != st->cachedSampleRate)) {
        fxComputeBiquadCoeffs(st, fc, mode, q, bwMode, sampleRate);
    }

    int32_t c_b0 = st->c_b0, c_b1 = st->c_b1, c_b2 = st->c_b2;
    int32_t c_a1 = st->c_a1, c_a2 = st->c_a2;
    int64_t s1L = st->s1L, s2L = st->s2L;
    int64_t s1R = st->s1R, s2R = st->s2R;

    if (mix == 255) {
        for (int i = 0; i < samples; i++) {
            int32_t xL = busL[i];
            int64_t ysL = (int64_t)c_b0 * xL + s1L;
            int32_t yiL = (int32_t)(ysL >> 28), yfL = (int32_t)(ysL & 0x0FFFFFFF);
            if (UNLIKELY(yiL > 1048575)) { yiL = 1048575; yfL = 0; }
            else if (UNLIKELY(yiL < -1048575)) { yiL = -1048575; yfL = 0; }
            s1L = ((int64_t)c_b1 * xL) - (((int64_t)c_a1 * yiL) + (((int64_t)c_a1 * yfL) >> 28)) + s2L;
            s2L = ((int64_t)c_b2 * xL) - (((int64_t)c_a2 * yiL) + (((int64_t)c_a2 * yfL) >> 28));
            busL[i] = yiL;

            int32_t xR = busR[i];
            int64_t ysR = (int64_t)c_b0 * xR + s1R;
            int32_t yiR = (int32_t)(ysR >> 28), yfR = (int32_t)(ysR & 0x0FFFFFFF);
            if (UNLIKELY(yiR > 1048575)) { yiR = 1048575; yfR = 0; }
            else if (UNLIKELY(yiR < -1048575)) { yiR = -1048575; yfR = 0; }
            s1R = ((int64_t)c_b1 * xR) - (((int64_t)c_a1 * yiR) + (((int64_t)c_a1 * yfR) >> 28)) + s2R;
            s2R = ((int64_t)c_b2 * xR) - (((int64_t)c_a2 * yiR) + (((int64_t)c_a2 * yfR) >> 28));
            busR[i] = yiR;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t xL = busL[i];
            int64_t ysL = (int64_t)c_b0 * xL + s1L;
            int32_t yiL = (int32_t)(ysL >> 28), yfL = (int32_t)(ysL & 0x0FFFFFFF);
            if (UNLIKELY(yiL > 1048575)) { yiL = 1048575; yfL = 0; }
            else if (UNLIKELY(yiL < -1048575)) { yiL = -1048575; yfL = 0; }
            s1L = ((int64_t)c_b1 * xL) - (((int64_t)c_a1 * yiL) + (((int64_t)c_a1 * yfL) >> 28)) + s2L;
            s2L = ((int64_t)c_b2 * xL) - (((int64_t)c_a2 * yiL) + (((int64_t)c_a2 * yfL) >> 28));
            busL[i] = xL + (((yiL - xL) * mix) >> 8);

            int32_t xR = busR[i];
            int64_t ysR = (int64_t)c_b0 * xR + s1R;
            int32_t yiR = (int32_t)(ysR >> 28), yfR = (int32_t)(ysR & 0x0FFFFFFF);
            if (UNLIKELY(yiR > 1048575)) { yiR = 1048575; yfR = 0; }
            else if (UNLIKELY(yiR < -1048575)) { yiR = -1048575; yfR = 0; }
            s1R = ((int64_t)c_b1 * xR) - (((int64_t)c_a1 * yiR) + (((int64_t)c_a1 * yfR) >> 28)) + s2R;
            s2R = ((int64_t)c_b2 * xR) - (((int64_t)c_a2 * yiR) + (((int64_t)c_a2 * yfR) >> 28));
            busR[i] = xR + (((yiR - xR) * mix) >> 8);
        }
    }

    st->s1L = s1L; st->s2L = s2L;
    st->s1R = s1R; st->s2R = s2R;
}

static inline void fxCompressorStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples, uint32_t sampleRate) {
    int32_t threshP  = fxClampInt(fx->ep[FX_COMP_THRESHOLD], 0, 255);
    int32_t ratio    = fxClampInt(fx->ep[FX_COMP_RATIO], 1, 255);
    int32_t attMs    = fxClampInt(fx->ep[FX_COMP_ATTACK_MS], 0, 500);
    int32_t relMs    = fxClampInt(fx->ep[FX_COMP_RELEASE_MS], 1, 2000);
    int32_t makeupQ8 = 256 + (fxClampInt(fx->ep[FX_COMP_MAKEUP], 0, 255) * 3);
    int32_t mix      = fxClampInt(fx->ep[FX_COMP_MIX], 0, 255);

    int32_t thresh = (threshP == 0) ? 128 : ((threshP * threshP) >> 1);
    int32_t invRatioQ15 = (ratio >= 200) ? 0 : (32768 / ratio);
    int32_t attQ15 = (attMs <= 0) ? 32768 : fxClampInt((int32_t)(32768000ULL / ((uint64_t)attMs * sampleRate + 1000ULL)), 1, 32768);
    int32_t relQ15 = fxClampInt((int32_t)(32768000ULL / ((uint64_t)relMs * sampleRate + 1000ULL)), 1, 32768);
    int32_t env = fx->es[0];

    for (int i = 0; i < samples; i++) {
        int32_t dryL = busL[i], dryR = busR[i];
        int32_t aL = (dryL < 0) ? -dryL : dryL;
        int32_t aR = (dryR < 0) ? -dryR : dryR;
        int32_t peak = (aL > aR) ? aL : aR;

        int32_t c = (peak > env) ? attQ15 : relQ15;
        env += (int32_t)(((int64_t)(peak - env) * c) >> 15);

        int32_t gainQ15 = 32768;
        if (env > thresh) {
            int32_t outEnv = thresh + (int32_t)(((int64_t)(env - thresh) * invRatioQ15) >> 15);
            gainQ15 = (int32_t)(((int64_t)outEnv << 15) / env);
        }

        int32_t gTotal = (gainQ15 * makeupQ8) >> 8;
        int32_t wetL = (int32_t)(((int64_t)dryL * gTotal) >> 15);
        int32_t wetR = (int32_t)(((int64_t)dryR * gTotal) >> 15);

        if (mix == 255) {
            busL[i] = wetL;
            busR[i] = wetR;
        } else {
            busL[i] = dryL + (((wetL - dryL) * mix) >> 8);
            busR[i] = dryR + (((wetR - dryR) * mix) >> 8);
        }
    }

    fx->es[0] = env;
}

static inline void fxBitcrusherStereo(FXNode* fx, int32_t* busL, int32_t* busR, int samples) {
    int32_t bits = fxClampInt(fx->ep[FX_CRUSH_BITS], 1, 16);
    int32_t ds   = fxClampInt(fx->ep[FX_CRUSH_DOWNSAMPLE], 1, 64);
    int32_t mix  = fxClampInt(fx->ep[FX_CRUSH_MIX], 0, 255);

    uint32_t mask = (~0U) << (16 - bits);
    int32_t holdL = fx->es[0], holdR = fx->es[1], cnt = fx->es[2];

    for (int i = 0; i < samples; i++) {
        int32_t dryL = busL[i], dryR = busR[i];
        if (cnt <= 0) {
            cnt = ds;
            holdL = (int32_t)((uint32_t)dryL & mask);
            holdR = (int32_t)((uint32_t)dryR & mask);
        }
        cnt--;

        if (mix == 255) {
            busL[i] = holdL;
            busR[i] = holdR;
        } else {
            busL[i] = dryL + (((holdL - dryL) * mix) >> 8);
            busR[i] = dryR + (((holdR - dryR) * mix) >> 8);
        }
    }

    fx->es[0] = holdL; fx->es[1] = holdR; fx->es[2] = cnt;
}

static inline void processBusFXNodeStereo(FXNode* fx, int32_t* busL, int32_t* busR, int32_t* voiceScratch, int samples, uint32_t sampleRate) {
    switch (fx->type) {
        case FX_CUSTOM:
            if (fx->customStereoFXFunc) {
                fx->customStereoFXFunc(busL, busR, samples, fx->ep, fx->es);
            } else if (fx->customFXFunc) {
                for (int i = 0; i < samples; i++) voiceScratch[i] = (busL[i] + busR[i]) >> 1;
                fx->customFXFunc(voiceScratch, samples, fx->ep, fx->es);
                memcpy(busL, voiceScratch, samples * sizeof(int32_t));
                memcpy(busR, voiceScratch, samples * sizeof(int32_t));
            }
            break;
        case FX_OVERDRIVE:  fxOverdriveStereo(fx, busL, busR, samples); break;
        case FX_CHORUS:     fxChorusStereo(fx, busL, busR, samples, sampleRate); break;
        case FX_FLANGER:    fxFlangerStereo(fx, busL, busR, samples, sampleRate); break;
        case FX_PHASER:     fxPhaserStereo(fx, busL, busR, samples, sampleRate); break;
        case FX_DELAY:      fxDelayStereo(fx, busL, busR, samples, sampleRate); break;
        case FX_REVERB:     fxReverbStereo(fx, busL, busR, samples); break;
        case FX_FILTER:     fxFilterStereo(fx, busL, busR, samples, sampleRate); break;
        case FX_COMPRESSOR: fxCompressorStereo(fx, busL, busR, samples, sampleRate); break;
        case FX_BITCRUSHER: fxBitcrusherStereo(fx, busL, busR, samples); break;
        default: break;
    }
}

#else

static inline void fxOverdriveMono(FXNode* fx, int32_t* bus, int samples) {
    int32_t drive = fxClampInt(fx->ep[FX_OD_DRIVE], 0, 255);
    int32_t tone  = fxClampInt(fx->ep[FX_OD_TONE],  0, 255);
    int32_t level = fxClampInt(fx->ep[FX_OD_LEVEL], 0, 255);
    int32_t mode  = fxClampInt(fx->ep[FX_OD_MODE],  0, 2);
    int32_t mix   = fxClampInt(fx->ep[FX_OD_MIX],   0, 255);

    int32_t gain = 256 + ((drive * drive * 31) >> 8);
    if (mode == 2) gain <<= 1;
    int32_t tc = 12 + ((tone * 245) >> 8);
    int32_t lp = fx->es[0], dc = fx->es[1];

    for (int i = 0; i < samples; i++) {
        int32_t dry = bus[i];
        int32_t x = (int32_t)(((int64_t)dry * gain) >> 8);
        int32_t y;

        if (mode == 1) {
            y = fxSoftCubic(x);
            if (x >= 0) y = (y * 210) >> 8;
            int32_t dco = dc >> 9; dc += y - dco; y -= dco;
        } else if (mode == 2) {
            x = fxClampInt(x, -24576, 24576);
            y = (x * 341) >> 8;
        } else {
            y = fxSoftCubic(x);
        }

        lp += ((y - lp) * tc) >> 8;
        int32_t wet = (lp * level) >> 8;
        bus[i] = (mix == 255) ? wet : (dry + (((wet - dry) * mix) >> 8));
    }

    fx->es[0] = lp; fx->es[1] = dc;
}

static inline void fxChorusMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    FXModDelayState* st = (FXModDelayState*)fx->fxData;
    if (UNLIKELY(!st || !st->buf)) return;

    int32_t rate    = fxClampInt(fx->ep[FX_CHORUS_RATE], 1, 3000);
    int32_t depth   = fxClampInt(fx->ep[FX_CHORUS_DEPTH], 0, 255);
    int32_t delayMs = fxClampInt(fx->ep[FX_CHORUS_DELAY_MS], 1, 30);
    int32_t fbQ8    = fxClampInt(fx->ep[FX_CHORUS_FEEDBACK], -200, 200);
    int32_t mix     = fxClampInt(fx->ep[FX_CHORUS_MIX], 0, 255);

    const uint32_t mask = st->mask;
    int32_t baseFrames = fxClampInt((int32_t)(((uint64_t)delayMs * sampleRate) / 1000ULL), 2, (int32_t)mask - 4);
    int32_t maxSweepFrames = (int32_t)((sampleRate * 6ULL) / 1000ULL);
    int32_t sweepAmp = (depth * maxSweepFrames) >> 8;

    uint32_t lfoInc = (uint32_t)(((uint64_t)rate * 4294967296ULL) / ((uint64_t)sampleRate * 100ULL));
    uint32_t lfoPh = st->lfoPhase;
    uint32_t wIdx  = st->writeIdx;
    int32_t maxDelQ8 = ((int32_t)mask - 2) << 8;
    int16_t* buf = st->buf;

    for (int i = 0; i < samples; i++) {
        int32_t sinVal = sineLUT[lfoPh >> SINE_SHIFT];
        int32_t delQ8  = fxClampInt((baseFrames << 8) + ((sinVal * sweepAmp) >> 7), 256, maxDelQ8);

        uint32_t i0 = (wIdx - (uint32_t)(delQ8 >> 8)) & mask;
        uint32_t i1 = (i0 - 1U) & mask;
        int32_t s0 = buf[i0];
        int32_t d  = s0 + (((buf[i1] - s0) * (delQ8 & 0xFF)) >> 8);

        int32_t dry = bus[i];
        buf[wIdx] = (int16_t)hw_clamps16((dry >> 1) + ((d * fbQ8) >> 8));
        int32_t wet = d << 1;
        bus[i] = dry + (((wet - dry) * mix) >> 8);

        wIdx = (wIdx + 1U) & mask;
        lfoPh += lfoInc;
    }

    st->writeIdx = wIdx;
    st->lfoPhase = lfoPh;
}

static inline void fxFlangerMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    FXModDelayState* st = (FXModDelayState*)fx->fxData;
    if (UNLIKELY(!st || !st->buf)) return;

    int32_t rate  = fxClampInt(fx->ep[FX_FLANGER_RATE], 1, 3000);
    int32_t depth = fxClampInt(fx->ep[FX_FLANGER_DEPTH], 0, 255);
    int32_t del10 = fxClampInt(fx->ep[FX_FLANGER_DELAY_MS10], 1, 100);
    int32_t fbQ8  = fxClampInt(fx->ep[FX_FLANGER_FEEDBACK], -230, 230);
    int32_t mix   = fxClampInt(fx->ep[FX_FLANGER_MIX], 0, 255);

    const uint32_t mask = st->mask;
    int32_t baseQ8 = (int32_t)(((uint64_t)del10 * sampleRate * 256ULL) / 10000ULL);
    if (baseQ8 < 256) baseQ8 = 256;
    int32_t maxSweepQ8 = (int32_t)(((uint64_t)sampleRate * 5ULL * 256ULL) / 1000ULL);
    int32_t sweepRangeQ8 = (int32_t)(((int64_t)maxSweepQ8 * depth) >> 8);

    uint32_t lfoInc = (uint32_t)(((uint64_t)rate * 4294967296ULL) / ((uint64_t)sampleRate * 100ULL));
    uint32_t lfoPh = st->lfoPhase;
    uint32_t wIdx  = st->writeIdx;
    int32_t maxDelQ8 = ((int32_t)mask - 2) << 8;
    int16_t* buf = st->buf;

    for (int i = 0; i < samples; i++) {
        int32_t u = sineLUT[lfoPh >> SINE_SHIFT] + 32768;
        int32_t delQ8 = fxClampInt(baseQ8 + (int32_t)(((int64_t)u * sweepRangeQ8) >> 16), 256, maxDelQ8);

        uint32_t i0 = (wIdx - (uint32_t)(delQ8 >> 8)) & mask;
        uint32_t i1 = (i0 - 1U) & mask;
        int32_t s0 = buf[i0];
        int32_t d  = s0 + (((buf[i1] - s0) * (delQ8 & 0xFF)) >> 8);

        int32_t dry = bus[i];
        buf[wIdx] = (int16_t)hw_clamps16((dry >> 1) + ((d * fbQ8) >> 8));
        int32_t wet = d << 1;
        bus[i] = dry + (((wet - dry) * mix) >> 8);

        wIdx = (wIdx + 1U) & mask;
        lfoPh += lfoInc;
    }

    st->writeIdx = wIdx;
    st->lfoPhase = lfoPh;
}

static inline void fxPhaserMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    FXPhaserState* st = (FXPhaserState*)fx->fxData;
    if (UNLIKELY(!st)) return;

    int32_t rate   = fxClampInt(fx->ep[FX_PHASER_RATE], 1, 3000);
    int32_t depth  = fxClampInt(fx->ep[FX_PHASER_DEPTH], 0, 255);
    int32_t fbQ8   = fxClampInt(fx->ep[FX_PHASER_FEEDBACK], -220, 220);
    int32_t stages = fxClampInt(fx->ep[FX_PHASER_STAGES], 2, 8) & ~1;
    if (stages < 2) stages = 2;
    int32_t mix    = fxClampInt(fx->ep[FX_PHASER_MIX], 0, 255);

    uint32_t lfoInc = (uint32_t)(((uint64_t)rate * 4294967296ULL) / ((uint64_t)sampleRate * 100ULL));
    uint32_t lfoPh = st->lfoPhase;
    int32_t aL = st->aL, fbL = st->fbL;
    const int32_t clampSig = 524288 << 10;

    for (int i = 0; i < samples; i++) {
        if ((i & 15) == 0) {
            aL = fxPhaserComputeCoeff(lfoPh, depth, sampleRate);
        }

        int32_t dry = bus[i];
        int32_t sig = (dry << 10) + (int32_t)(((int64_t)fbL * fbQ8) >> 8);
        if (UNLIKELY(sig > clampSig)) sig = clampSig;
        else if (UNLIKELY(sig < -clampSig)) sig = -clampSig;

        for (int s = 0; s < stages; s++) {
            int32_t y = -(int32_t)(((int64_t)aL * sig) >> 24) + st->apL[s];
            st->apL[s] = sig + (int32_t)(((int64_t)aL * y) >> 24);
            sig = y;
        }

        fbL = sig;
        int32_t phased = (dry + (sig >> 10)) >> 1;
        bus[i] = dry + (((phased - dry) * mix) >> 8);
        lfoPh += lfoInc;
    }

    st->lfoPhase = lfoPh;
    st->aL = aL;
    st->fbL = fbL;
}

static inline void fxDelayMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    FXDelayState* st = (FXDelayState*)fx->fxData;
    if (UNLIKELY(!st || !st->buf || st->capacity < 4)) return;

    int32_t delayMs = fxClampInt(fx->ep[FX_DELAY_TIME_MS], 1, 30000);
    int32_t fbQ8    = fxClampInt(fx->ep[FX_DELAY_FEEDBACK], 0, 245);
    int32_t damp    = fxClampInt(fx->ep[FX_DELAY_DAMP], 0, 255);
    int32_t mix     = fxClampInt(fx->ep[FX_DELAY_MIX], 0, 255);

    uint32_t cap = st->capacity;
    uint32_t targetFrames = (uint32_t)(((uint64_t)delayMs * sampleRate) / 1000ULL);
    if (targetFrames < 1U) targetFrames = 1U;
    if (targetFrames > cap - 2U) targetFrames = cap - 2U;
    uint32_t targetQ8 = targetFrames << 8;

    uint32_t currQ8 = st->currDelayQ8;
    if (UNLIKELY(currQ8 == 0)) currQ8 = targetQ8;
    if (currQ8 > ((cap - 2U) << 8)) currQ8 = (cap - 2U) << 8;

    int32_t dampCoef = 256 - ((damp * 224) >> 8);
    uint32_t wIdx = st->writeIdx;
    int32_t lpL = st->lpL;
    int16_t* buf = st->buf;

    for (int i = 0; i < samples; i++) {
        if (UNLIKELY(currQ8 != targetQ8)) {
            int32_t diff = (int32_t)targetQ8 - (int32_t)currQ8;
            int32_t step = diff >> 10;
            if (step == 0) step = (diff > 0) ? 1 : -1;
            currQ8 = (uint32_t)((int32_t)currQ8 + step);
        }

        uint32_t intDel = currQ8 >> 8;
        int32_t  frac   = (int32_t)(currQ8 & 0xFFU);
        uint32_t r0 = (wIdx >= intDel) ? (wIdx - intDel) : (wIdx + cap - intDel);
        uint32_t r1 = (r0 > 0U) ? (r0 - 1U) : (cap - 1U);

        int32_t s0 = buf[r0], s1 = buf[r1];
        int32_t d  = s0 + (((s1 - s0) * frac) >> 8);
        lpL += ((d - lpL) * dampCoef) >> 8;

        int32_t dry = bus[i];
        buf[wIdx] = (int16_t)hw_clamps16((dry >> 1) + ((lpL * fbQ8) >> 8));
        int32_t wet = d << 1;
        bus[i] = dry + (((wet - dry) * mix) >> 8);

        if (UNLIKELY(++wIdx >= cap)) wIdx = 0;
    }

    st->writeIdx    = wIdx;
    st->currDelayQ8 = currQ8;
    st->lpL         = lpL;
}

static inline void fxReverbMono(FXNode* fx, int32_t* bus, int samples) {
    FXReverbState* st = (FXReverbState*)fx->fxData;
    if (UNLIKELY(!st || !st->slab)) return;

    int32_t room = fxClampInt(fx->ep[FX_REVERB_ROOM], 0, 255);
    int32_t damp = fxClampInt(fx->ep[FX_REVERB_DAMP], 0, 255);
    int32_t mix  = fxClampInt(fx->ep[FX_REVERB_MIX],  0, 255);

    int32_t fbQ15  = 23593 + ((room * 7864) >> 8);
    int32_t lpCoef = 256 - ((damp * 200) >> 8);
    int32_t dcL = st->dcL;

    for (int i = 0; i < samples; i++) {
        int32_t dry = bus[i];
        int32_t ac = dry - (dcL >> 9);
        dcL += ac;
        int32_t inScaled = ac >> 1;
        int32_t acc = 0;

        for (int c = 0; c < 4; c++) {
            uint16_t idx = st->combIdxL[c];
            int16_t* buf = st->combL[c];
            int32_t cOut = buf[idx];
            int32_t dState = st->combDampL[c];
            dState += (((cOut << 8) - dState) * lpCoef) >> 8;
            st->combDampL[c] = dState;
            buf[idx] = (int16_t)hw_clamps16(inScaled + (((dState >> 8) * fbQ15) >> 15));
            if (UNLIKELY(++idx >= st->combLenL[c])) idx = 0;
            st->combIdxL[c] = idx;
            acc += cOut;
        }

        int32_t sig = hw_clamps16(acc >> 1);
        for (int a = 0; a < 2; a++) {
            uint16_t idx = st->apIdxL[a];
            int16_t* buf = st->apL[a];
            int32_t bOut = buf[idx];
            int32_t newV = hw_clamps16(sig + (bOut >> 1));
            sig = bOut - (newV >> 1);
            buf[idx] = (int16_t)newV;
            if (UNLIKELY(++idx >= st->apLenL[a])) idx = 0;
            st->apIdxL[a] = idx;
        }

        bus[i] = dry + (((sig - dry) * mix) >> 8);
    }
    st->dcL = dcL;
}

static inline void fxFilterMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    FXFilterState* st = (FXFilterState*)fx->fxData;
    if (UNLIKELY(!st)) return;

    int32_t fc     = fx->ep[FX_FILTER_CUTOFF];
    int16_t mode   = fx->ep[FX_FILTER_MODE];
    int32_t q      = fx->ep[FX_FILTER_RESONANCE];
    int16_t bwMode = fx->ep[FX_FILTER_BW_MODE];
    int32_t mix    = fxClampInt(fx->ep[FX_FILTER_MIX], 0, 255);

    if (UNLIKELY(fc != st->cachedFc || q != st->cachedQ || mode != st->cachedMode ||
                 bwMode != st->cachedBwMode || sampleRate != st->cachedSampleRate)) {
        fxComputeBiquadCoeffs(st, fc, mode, q, bwMode, sampleRate);
    }

    int32_t c_b0 = st->c_b0, c_b1 = st->c_b1, c_b2 = st->c_b2;
    int32_t c_a1 = st->c_a1, c_a2 = st->c_a2;
    int64_t s1 = st->s1L, s2 = st->s2L;

    if (mix == 255) {
        for (int i = 0; i < samples; i++) {
            int32_t x = bus[i];
            int64_t ys = (int64_t)c_b0 * x + s1;
            int32_t yi = (int32_t)(ys >> 28), yf = (int32_t)(ys & 0x0FFFFFFF);
            if (UNLIKELY(yi > 1048575)) { yi = 1048575; yf = 0; }
            else if (UNLIKELY(yi < -1048575)) { yi = -1048575; yf = 0; }
            s1 = ((int64_t)c_b1 * x) - (((int64_t)c_a1 * yi) + (((int64_t)c_a1 * yf) >> 28)) + s2;
            s2 = ((int64_t)c_b2 * x) - (((int64_t)c_a2 * yi) + (((int64_t)c_a2 * yf) >> 28));
            bus[i] = yi;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t x = bus[i];
            int64_t ys = (int64_t)c_b0 * x + s1;
            int32_t yi = (int32_t)(ys >> 28), yf = (int32_t)(ys & 0x0FFFFFFF);
            if (UNLIKELY(yi > 1048575)) { yi = 1048575; yf = 0; }
            else if (UNLIKELY(yi < -1048575)) { yi = -1048575; yf = 0; }
            s1 = ((int64_t)c_b1 * x) - (((int64_t)c_a1 * yi) + (((int64_t)c_a1 * yf) >> 28)) + s2;
            s2 = ((int64_t)c_b2 * x) - (((int64_t)c_a2 * yi) + (((int64_t)c_a2 * yf) >> 28));
            bus[i] = x + (((yi - x) * mix) >> 8);
        }
    }

    st->s1L = s1; st->s2L = s2;
}

static inline void fxCompressorMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    int32_t threshP  = fxClampInt(fx->ep[FX_COMP_THRESHOLD], 0, 255);
    int32_t ratio    = fxClampInt(fx->ep[FX_COMP_RATIO], 1, 255);
    int32_t attMs    = fxClampInt(fx->ep[FX_COMP_ATTACK_MS], 0, 500);
    int32_t relMs    = fxClampInt(fx->ep[FX_COMP_RELEASE_MS], 1, 2000);
    int32_t makeupQ8 = 256 + (fxClampInt(fx->ep[FX_COMP_MAKEUP], 0, 255) * 3);
    int32_t mix      = fxClampInt(fx->ep[FX_COMP_MIX], 0, 255);

    int32_t thresh = (threshP == 0) ? 128 : ((threshP * threshP) >> 1);
    int32_t invRatioQ15 = (ratio >= 200) ? 0 : (32768 / ratio);
    int32_t attQ15 = (attMs <= 0) ? 32768 : fxClampInt((int32_t)(32768000ULL / ((uint64_t)attMs * sampleRate + 1000ULL)), 1, 32768);
    int32_t relQ15 = fxClampInt((int32_t)(32768000ULL / ((uint64_t)relMs * sampleRate + 1000ULL)), 1, 32768);
    int32_t env = fx->es[0];

    for (int i = 0; i < samples; i++) {
        int32_t dry = bus[i];
        int32_t peak = (dry < 0) ? -dry : dry;
        int32_t c = (peak > env) ? attQ15 : relQ15;
        env += (int32_t)(((int64_t)(peak - env) * c) >> 15);

        int32_t gainQ15 = 32768;
        if (env > thresh) {
            int32_t outEnv = thresh + (int32_t)(((int64_t)(env - thresh) * invRatioQ15) >> 15);
            gainQ15 = (int32_t)(((int64_t)outEnv << 15) / env);
        }

        int32_t gTotal = (gainQ15 * makeupQ8) >> 8;
        int32_t wet = (int32_t)(((int64_t)dry * gTotal) >> 15);
        bus[i] = (mix == 255) ? wet : (dry + (((wet - dry) * mix) >> 8));
    }

    fx->es[0] = env;
}

static inline void fxBitcrusherMono(FXNode* fx, int32_t* bus, int samples) {
    int32_t bits = fxClampInt(fx->ep[FX_CRUSH_BITS], 1, 16);
    int32_t ds   = fxClampInt(fx->ep[FX_CRUSH_DOWNSAMPLE], 1, 64);
    int32_t mix  = fxClampInt(fx->ep[FX_CRUSH_MIX], 0, 255);

    uint32_t mask = (~0U) << (16 - bits);
    int32_t hold = fx->es[0], cnt = fx->es[1];

    for (int i = 0; i < samples; i++) {
        int32_t dry = bus[i];
        if (cnt <= 0) {
            cnt = ds;
            hold = (int32_t)((uint32_t)dry & mask);
        }
        cnt--;
        bus[i] = (mix == 255) ? hold : (dry + (((hold - dry) * mix) >> 8));
    }

    fx->es[0] = hold; fx->es[1] = cnt;
}

static inline void processBusFXNodeMono(FXNode* fx, int32_t* bus, int samples, uint32_t sampleRate) {
    switch (fx->type) {
        case FX_CUSTOM:
            if (fx->customFXFunc) fx->customFXFunc(bus, samples, fx->ep, fx->es);
            break;
        case FX_OVERDRIVE:  fxOverdriveMono(fx, bus, samples); break;
        case FX_CHORUS:     fxChorusMono(fx, bus, samples, sampleRate); break;
        case FX_FLANGER:    fxFlangerMono(fx, bus, samples, sampleRate); break;
        case FX_PHASER:     fxPhaserMono(fx, bus, samples, sampleRate); break;
        case FX_DELAY:      fxDelayMono(fx, bus, samples, sampleRate); break;
        case FX_REVERB:     fxReverbMono(fx, bus, samples); break;
        case FX_FILTER:     fxFilterMono(fx, bus, samples, sampleRate); break;
        case FX_COMPRESSOR: fxCompressorMono(fx, bus, samples, sampleRate); break;
        case FX_BITCRUSHER: fxBitcrusherMono(fx, bus, samples); break;
        default: break;
    }
}

#endif

void ESP32Synth::releaseFXNode(uint8_t busId, uint8_t slotId) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];

    busesConfig[busId].fxMask &= (uint8_t)(~(1U << slotId));
    fx->active = false;
    __sync_synchronize();

    if (_running && audioTaskHandle != NULL && xTaskGetCurrentTaskHandle() != audioTaskHandle) {
        uint32_t seq = _renderSeq;
        if (seq & 1U) {
            uint32_t spins = 0;
            while (_running && _renderSeq == seq && spins < 50) {
                vTaskDelay(pdMS_TO_TICKS(1));
                spins++;
            }
        }
    }

    if (fx->fxData != nullptr) {
        if (fx->type == FX_CHORUS || fx->type == FX_FLANGER) {
            FXModDelayState* st = (FXModDelayState*)fx->fxData;
            if (st->buf) heap_caps_free(st->buf);
            heap_caps_free(st);
        } else if (fx->type == FX_DELAY) {
            FXDelayState* st = (FXDelayState*)fx->fxData;
            if (st->buf) heap_caps_free(st->buf);
            heap_caps_free(st);
        } else if (fx->type == FX_REVERB) {
            FXReverbState* st = (FXReverbState*)fx->fxData;
            if (st->slab) heap_caps_free(st->slab);
            heap_caps_free(st);
        } else {
            heap_caps_free(fx->fxData);
        }
        fx->fxData = nullptr;
    }
    fx->type = FX_NONE;
    fx->customFXFunc = nullptr;
#if SYNTH_STEREO
    fx->customStereoFXFunc = nullptr;
#endif
}

void ESP32Synth::clearAllBusFXBuffers() {
    for (uint8_t b = 0; b < MAX_BUSES; b++) {
        for (uint8_t f = 0; f < MAX_FX_PER_BUS; f++) {
            FXNode* fx = &busesConfig[b].fxChain[f];
            memset(fx->es, 0, sizeof(fx->es));
            if (!fx->fxData) continue;

            if (fx->type == FX_CHORUS || fx->type == FX_FLANGER) {
                FXModDelayState* st = (FXModDelayState*)fx->fxData;
                if (st->buf) {
#if SYNTH_STEREO
                    memset(st->buf, 0, (st->mask + 1U) * sizeof(StereoSample));
#else
                    memset(st->buf, 0, (st->mask + 1U) * sizeof(int16_t));
#endif
                }
                st->writeIdx = 0;
                st->lfoPhase = 0;
            } else if (fx->type == FX_DELAY) {
                FXDelayState* st = (FXDelayState*)fx->fxData;
                if (st->buf && st->capacity > 0) {
#if SYNTH_STEREO
                    memset(st->buf, 0, st->capacity * sizeof(StereoSample));
#else
                    memset(st->buf, 0, st->capacity * sizeof(int16_t));
#endif
                }
                st->writeIdx = 0;
                st->currDelayQ8 = 0;
                st->lpL = 0;
#if SYNTH_STEREO
                st->lpR = 0;
#endif
            } else if (fx->type == FX_REVERB) {
                FXReverbState* st = (FXReverbState*)fx->fxData;
                uint32_t total = 0;
                for (int c = 0; c < 4; c++) { total += st->combLenL[c]; st->combIdxL[c] = 0; st->combDampL[c] = 0; }
                for (int a = 0; a < 2; a++) { total += st->apLenL[a]; st->apIdxL[a] = 0; }
                st->dcL = 0;
#if SYNTH_STEREO
                for (int c = 0; c < 4; c++) { total += st->combLenR[c]; st->combIdxR[c] = 0; st->combDampR[c] = 0; }
                for (int a = 0; a < 2; a++) { total += st->apLenR[a]; st->apIdxR[a] = 0; }
                st->dcR = 0;
#endif
                if (st->slab && total > 0) memset(st->slab, 0, total * sizeof(int16_t));
            } else if (fx->type == FX_FILTER) {
                FXFilterState* st = (FXFilterState*)fx->fxData;
                st->s1L = st->s2L = 0;
#if SYNTH_STEREO
                st->s1R = st->s2R = 0;
#endif
            } else if (fx->type == FX_PHASER) {
                FXPhaserState* st = (FXPhaserState*)fx->fxData;
                memset(st->apL, 0, sizeof(st->apL));
                st->fbL = 0;
#if SYNTH_STEREO
                memset(st->apR, 0, sizeof(st->apR));
                st->fbR = 0;
#endif
                st->lfoPhase = 0;
            }
        }
    }
}

void ESP32Synth::freeAllBusFX() {
    for (uint8_t b = 0; b < MAX_BUSES; b++) {
        clearBusFX(b);
    }
}

bool ESP32Synth::initBuiltInFXNode(uint8_t busId, uint8_t slotId, FXType type, uint32_t extraMs) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return false;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];

    releaseFXNode(busId, slotId);

    memset(fx->ep, 0, sizeof(fx->ep));
    memset(fx->es, 0, sizeof(fx->es));

    if (type == FX_OVERDRIVE) {
        fx->ep[FX_OD_DRIVE] = 128;
        fx->ep[FX_OD_TONE]  = 160;
        fx->ep[FX_OD_LEVEL] = 180;
        fx->ep[FX_OD_MODE]  = 0;
        fx->ep[FX_OD_MIX]   = 255;
    } else if (type == FX_CHORUS || type == FX_FLANGER) {
        uint32_t frames = (type == FX_CHORUS) ? 2048U : 1024U;
        FXModDelayState* st = (FXModDelayState*)heap_caps_calloc(1, sizeof(FXModDelayState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!st) return false;
#if SYNTH_STEREO
        st->buf = (StereoSample*)heap_caps_calloc(frames, sizeof(StereoSample), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
        st->buf = (int16_t*)heap_caps_calloc(frames, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
        if (!st->buf) { heap_caps_free(st); return false; }
        st->mask = frames - 1U;
        fx->fxData = st;

        if (type == FX_CHORUS) {
            fx->ep[FX_CHORUS_RATE]         = 80;
            fx->ep[FX_CHORUS_DEPTH]        = 120;
            fx->ep[FX_CHORUS_DELAY_MS]     = 12;
            fx->ep[FX_CHORUS_FEEDBACK]     = 20;
            fx->ep[FX_CHORUS_MIX]          = 128;
            fx->ep[FX_CHORUS_STEREO_PHASE] = 180;
        } else {
            fx->ep[FX_FLANGER_RATE]         = 35;
            fx->ep[FX_FLANGER_DEPTH]        = 180;
            fx->ep[FX_FLANGER_DELAY_MS10]   = 15;
            fx->ep[FX_FLANGER_FEEDBACK]     = 140;
            fx->ep[FX_FLANGER_MIX]          = 128;
            fx->ep[FX_FLANGER_STEREO_PHASE] = 90;
        }
    } else if (type == FX_PHASER) {
        FXPhaserState* st = (FXPhaserState*)heap_caps_calloc(1, sizeof(FXPhaserState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!st) return false;
        st->aL = 14000000;
#if SYNTH_STEREO
        st->aR = 14000000;
#endif
        fx->fxData = st;
        fx->ep[FX_PHASER_RATE]         = 50;
        fx->ep[FX_PHASER_DEPTH]        = 180;
        fx->ep[FX_PHASER_FEEDBACK]     = 120;
        fx->ep[FX_PHASER_STAGES]       = 4;
        fx->ep[FX_PHASER_MIX]          = 128;
        fx->ep[FX_PHASER_STEREO_PHASE] = 90;
    } else if (type == FX_DELAY) {
        uint32_t targetMs = (extraMs > (uint32_t)SYNTH_DELAY_MAX_MS) ? extraMs : (uint32_t)SYNTH_DELAY_MAX_MS;
        if (targetMs < 20U) targetMs = 20U;
        uint32_t sr = (_sampleRate > 0) ? _sampleRate : 48000U;
        uint32_t capFrames = (uint32_t)(((uint64_t)targetMs * sr) / 1000ULL) + 4U;

        FXDelayState* st = (FXDelayState*)heap_caps_calloc(1, sizeof(FXDelayState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!st) return false;

#if SYNTH_STEREO
        size_t elemSize = sizeof(StereoSample);
        st->buf = (StereoSample*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
        size_t elemSize = sizeof(int16_t);
        st->buf = (int16_t*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
        if (!st->buf) {
            #ifdef MALLOC_CAP_SPIRAM
            #if SYNTH_STEREO
            st->buf = (StereoSample*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            #else
            st->buf = (int16_t*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            #endif
            #endif
        }
        if (!st->buf && extraMs > 0 && extraMs < targetMs) {
            capFrames = (uint32_t)(((uint64_t)extraMs * sr) / 1000ULL) + 4U;
#if SYNTH_STEREO
            st->buf = (StereoSample*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
            st->buf = (int16_t*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
        }
        if (!st->buf) {
            size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            uint32_t fallbackFrames = (uint32_t)((avail * 3U) / (4U * elemSize));
            if (fallbackFrames >= 64U) {
                capFrames = fallbackFrames;
#if SYNTH_STEREO
                st->buf = (StereoSample*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
                st->buf = (int16_t*)heap_caps_calloc(capFrames, elemSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
            }
        }
        if (!st->buf) { heap_caps_free(st); return false; }

        st->capacity = capFrames;
        fx->fxData = st;

        fx->ep[FX_DELAY_TIME_MS]  = (int16_t)((extraMs > 0) ? extraMs : 250);
        fx->ep[FX_DELAY_FEEDBACK] = 120;
        fx->ep[FX_DELAY_DAMP]     = 100;
        fx->ep[FX_DELAY_PINGPONG] = 1;
        fx->ep[FX_DELAY_MIX]      = 110;
    } else if (type == FX_REVERB) {
        static const uint16_t baseCombL[4] = {1217, 1361, 1493, 1621};
        static const uint16_t baseCombR[4] = {1240, 1384, 1516, 1644};
        static const uint16_t baseApL[2]   = {389, 521};
        static const uint16_t baseApR[2]   = {412, 544};
        uint32_t sr = (_sampleRate > 0) ? _sampleRate : 48000U;

        FXReverbState* st = (FXReverbState*)heap_caps_calloc(1, sizeof(FXReverbState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!st) return false;

        uint32_t total = 0;
        for (int c = 0; c < 4; c++) {
            uint32_t l = ((uint32_t)baseCombL[c] * sr) / 48000U;
            st->combLenL[c] = (uint16_t)((l < 16U) ? 16U : l);
            total += st->combLenL[c];
        }
        for (int a = 0; a < 2; a++) {
            uint32_t l = ((uint32_t)baseApL[a] * sr) / 48000U;
            st->apLenL[a] = (uint16_t)((l < 8U) ? 8U : l);
            total += st->apLenL[a];
        }
#if SYNTH_STEREO
        for (int c = 0; c < 4; c++) {
            uint32_t l = ((uint32_t)baseCombR[c] * sr) / 48000U;
            st->combLenR[c] = (uint16_t)((l < 16U) ? 16U : l);
            total += st->combLenR[c];
        }
        for (int a = 0; a < 2; a++) {
            uint32_t l = ((uint32_t)baseApR[a] * sr) / 48000U;
            st->apLenR[a] = (uint16_t)((l < 8U) ? 8U : l);
            total += st->apLenR[a];
        }
#else
        (void)baseCombR; (void)baseApR;
#endif

        st->slab = (int16_t*)heap_caps_calloc(total, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!st->slab) { heap_caps_free(st); return false; }

        int16_t* ptr = st->slab;
        for (int c = 0; c < 4; c++) { st->combL[c] = ptr; ptr += st->combLenL[c]; }
        for (int a = 0; a < 2; a++) { st->apL[a]   = ptr; ptr += st->apLenL[a]; }
#if SYNTH_STEREO
        for (int c = 0; c < 4; c++) { st->combR[c] = ptr; ptr += st->combLenR[c]; }
        for (int a = 0; a < 2; a++) { st->apR[a]   = ptr; ptr += st->apLenR[a]; }
#endif
        fx->fxData = st;

        fx->ep[FX_REVERB_ROOM]  = 160;
        fx->ep[FX_REVERB_DAMP]  = 110;
        fx->ep[FX_REVERB_WIDTH] = 255;
        fx->ep[FX_REVERB_MIX]   = 90;
    } else if (type == FX_FILTER) {
        FXFilterState* st = (FXFilterState*)heap_caps_calloc(1, sizeof(FXFilterState), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!st) return false;
        fx->fxData = st;
        fx->ep[FX_FILTER_CUTOFF]    = 2000;
        fx->ep[FX_FILTER_MODE]      = 0;
        fx->ep[FX_FILTER_RESONANCE] = 70;
        fx->ep[FX_FILTER_BW_MODE]   = 0;
        fx->ep[FX_FILTER_MIX]       = 255;
        fxComputeBiquadCoeffs(st, 2000, 0, 70, 0, (_sampleRate > 0) ? _sampleRate : 48000U);
    } else if (type == FX_COMPRESSOR) {
        fx->ep[FX_COMP_THRESHOLD]  = 140;
        fx->ep[FX_COMP_RATIO]      = 4;
        fx->ep[FX_COMP_ATTACK_MS]  = 5;
        fx->ep[FX_COMP_RELEASE_MS] = 80;
        fx->ep[FX_COMP_MAKEUP]     = 32;
        fx->ep[FX_COMP_MIX]        = 255;
    } else if (type == FX_BITCRUSHER) {
        fx->ep[FX_CRUSH_BITS]       = 8;
        fx->ep[FX_CRUSH_DOWNSAMPLE] = 4;
        fx->ep[FX_CRUSH_MIX]        = 255;
    } else {
        return false;
    }

    fx->type = type;
    __sync_synchronize();
    fx->active = true;
    busesConfig[busId].fxMask |= (uint8_t)(1U << slotId);
    busesConfig[busId].active = true;
    return true;
}

void ESP32Synth::removeBusFX(uint8_t busId, uint8_t slotId) {
    releaseFXNode(busId, slotId);
}

void ESP32Synth::clearBusFX(uint8_t busId) {
    if (busId >= MAX_BUSES) return;
    for (uint8_t f = 0; f < MAX_FX_PER_BUS; f++) {
        releaseFXNode(busId, f);
    }
}

void ESP32Synth::setBusOverdrive(uint8_t busId, uint8_t slotId, uint8_t drive, uint8_t tone, uint8_t level, uint8_t mode, uint8_t mix) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_OVERDRIVE && !initBuiltInFXNode(busId, slotId, FX_OVERDRIVE)) return;
    fx->ep[FX_OD_DRIVE] = drive;
    fx->ep[FX_OD_TONE]  = tone;
    fx->ep[FX_OD_LEVEL] = level;
    fx->ep[FX_OD_MODE]  = mode;
    fx->ep[FX_OD_MIX]   = mix;
}

void ESP32Synth::setBusChorus(uint8_t busId, uint8_t slotId, uint16_t rateCentiHz, uint8_t depth, uint8_t delayMs, int16_t feedback, uint8_t mix, uint8_t stereoPhase) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_CHORUS && !initBuiltInFXNode(busId, slotId, FX_CHORUS)) return;
    fx->ep[FX_CHORUS_RATE]         = (int16_t)((rateCentiHz > 3000) ? 3000 : rateCentiHz);
    fx->ep[FX_CHORUS_DEPTH]        = depth;
    fx->ep[FX_CHORUS_DELAY_MS]     = delayMs;
    fx->ep[FX_CHORUS_FEEDBACK]     = feedback;
    fx->ep[FX_CHORUS_MIX]          = mix;
    fx->ep[FX_CHORUS_STEREO_PHASE] = stereoPhase;
}

void ESP32Synth::setBusFlanger(uint8_t busId, uint8_t slotId, uint16_t rateCentiHz, uint8_t depth, uint8_t delayTenthsMs, int16_t feedback, uint8_t mix, uint8_t stereoPhase) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_FLANGER && !initBuiltInFXNode(busId, slotId, FX_FLANGER)) return;
    fx->ep[FX_FLANGER_RATE]         = (int16_t)((rateCentiHz > 3000) ? 3000 : rateCentiHz);
    fx->ep[FX_FLANGER_DEPTH]        = depth;
    fx->ep[FX_FLANGER_DELAY_MS10]   = delayTenthsMs;
    fx->ep[FX_FLANGER_FEEDBACK]     = feedback;
    fx->ep[FX_FLANGER_MIX]          = mix;
    fx->ep[FX_FLANGER_STEREO_PHASE] = stereoPhase;
}

void ESP32Synth::setBusPhaser(uint8_t busId, uint8_t slotId, uint16_t rateCentiHz, uint8_t depth, int16_t feedback, uint8_t stages, uint8_t mix, uint8_t stereoPhase) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_PHASER && !initBuiltInFXNode(busId, slotId, FX_PHASER)) return;
    fx->ep[FX_PHASER_RATE]         = (int16_t)((rateCentiHz > 3000) ? 3000 : rateCentiHz);
    fx->ep[FX_PHASER_DEPTH]        = depth;
    fx->ep[FX_PHASER_FEEDBACK]     = feedback;
    fx->ep[FX_PHASER_STAGES]       = stages;
    fx->ep[FX_PHASER_MIX]          = mix;
    fx->ep[FX_PHASER_STEREO_PHASE] = stereoPhase;
}

void ESP32Synth::setBusDelay(uint8_t busId, uint8_t slotId, uint16_t delayMs, uint8_t feedback, uint8_t damp, bool pingPong, uint8_t mix) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    uint32_t sr = (_sampleRate > 0) ? _sampleRate : 48000U;
    uint32_t reqFrames = (uint32_t)(((uint64_t)delayMs * sr) / 1000ULL) + 2U;

    bool needAlloc = (fx->type != FX_DELAY);
    if (!needAlloc && fx->fxData) {
        FXDelayState* st = (FXDelayState*)fx->fxData;
        if (reqFrames > st->capacity) needAlloc = true;
    }
    if (needAlloc && !initBuiltInFXNode(busId, slotId, FX_DELAY, delayMs)) return;

    fx->ep[FX_DELAY_TIME_MS]  = (int16_t)((delayMs > 30000) ? 30000 : delayMs);
    fx->ep[FX_DELAY_FEEDBACK] = feedback;
    fx->ep[FX_DELAY_DAMP]     = damp;
    fx->ep[FX_DELAY_PINGPONG] = pingPong ? 1 : 0;
    fx->ep[FX_DELAY_MIX]      = mix;
}

void ESP32Synth::setBusReverb(uint8_t busId, uint8_t slotId, uint8_t roomSize, uint8_t damping, uint8_t width, uint8_t mix) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_REVERB && !initBuiltInFXNode(busId, slotId, FX_REVERB)) return;
    fx->ep[FX_REVERB_ROOM]  = roomSize;
    fx->ep[FX_REVERB_DAMP]  = damping;
    fx->ep[FX_REVERB_WIDTH] = width;
    fx->ep[FX_REVERB_MIX]   = mix;
}

void ESP32Synth::setBusFilter(uint8_t busId, uint8_t slotId, uint16_t cutoffHz, uint16_t resonance, uint8_t mode, uint8_t mix) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_FILTER && !initBuiltInFXNode(busId, slotId, FX_FILTER)) return;
    fx->ep[FX_FILTER_CUTOFF]    = (int16_t)((cutoffHz > 20000) ? 20000 : cutoffHz);
    fx->ep[FX_FILTER_MODE]      = mode;
    fx->ep[FX_FILTER_RESONANCE] = (int16_t)((resonance > 30000) ? 30000 : resonance);
    fx->ep[FX_FILTER_BW_MODE]   = 0;
    fx->ep[FX_FILTER_MIX]       = mix;
}

void ESP32Synth::setBusCompressor(uint8_t busId, uint8_t slotId, uint8_t threshold, uint8_t ratio, uint16_t attackMs, uint16_t releaseMs, uint8_t makeup, uint8_t mix) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_COMPRESSOR && !initBuiltInFXNode(busId, slotId, FX_COMPRESSOR)) return;
    fx->ep[FX_COMP_THRESHOLD]  = threshold;
    fx->ep[FX_COMP_RATIO]      = ratio;
    fx->ep[FX_COMP_ATTACK_MS]  = (int16_t)((attackMs > 500) ? 500 : attackMs);
    fx->ep[FX_COMP_RELEASE_MS] = (int16_t)((releaseMs > 2000) ? 2000 : releaseMs);
    fx->ep[FX_COMP_MAKEUP]     = makeup;
    fx->ep[FX_COMP_MIX]        = mix;
}

void ESP32Synth::setBusBitcrush(uint8_t busId, uint8_t slotId, uint8_t bits, uint8_t downsample, uint8_t mix) {
    if (busId >= MAX_BUSES || slotId >= MAX_FX_PER_BUS) return;
    FXNode* fx = &busesConfig[busId].fxChain[slotId];
    if (fx->type != FX_BITCRUSHER && !initBuiltInFXNode(busId, slotId, FX_BITCRUSHER)) return;
    fx->ep[FX_CRUSH_BITS]       = bits;
    fx->ep[FX_CRUSH_DOWNSAMPLE] = downsample;
    fx->ep[FX_CRUSH_MIX]        = mix;
}