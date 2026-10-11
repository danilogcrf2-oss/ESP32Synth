#include "ESP32Synth.h"

// Hardware Pin Selection (ESP32-S3 default: 4, 6, 5)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    const int PIN_BCK  = 4;
    const int PIN_WS   = 6;
    const int PIN_DATA = 5;
#else
    const int PIN_BCK  = 4;
    const int PIN_WS   = 15;
    const int PIN_DATA = 2;
#endif

ESP32Synth synth;

// Voice Allocations
const uint16_t V_BASS    = 0;
const uint16_t V_PAD0    = 1;
const uint16_t V_PAD1    = 2;
const uint16_t V_PAD2    = 3;
const uint16_t V_PAD3    = 4;
const uint16_t V_PLUCK0  = 5;
const uint16_t V_PLUCK1  = 6;
const uint16_t V_LEAD    = 7;
const uint16_t V_BELL    = 8;
const uint16_t V_AIR     = 9;
const uint16_t V_HARP    = 10;
const uint16_t V_DROP    = 11;

// ====================================================================================
// DSP FX ENGINES (Pure 32-bit Fixed Point, Zero Floats, No Macro Collisions)
// ====================================================================================

// Bus 1: 3/4 Time Multi-Tap Raindrop Delay (128KB Buffer, Uses ESP32-S3 PSRAM)
#define RAIN_DELAY_SIZE 65536
#define RAIN_DELAY_MASK 0xFFFF

void FX_RaindropDelay(int32_t* busBuffer, int samples, int16_t* ep, int32_t* es) {
    static int16_t* rainBuf = nullptr;
    static uint16_t rainWriteIdx = 0;
    static int32_t  rainLpFilter = 0;

    if (rainBuf == nullptr) {
        rainBuf = (int16_t*)heap_caps_calloc(RAIN_DELAY_SIZE, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!rainBuf) {
            rainBuf = (int16_t*)heap_caps_calloc(RAIN_DELAY_SIZE, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (!rainBuf) return;
    }

    const uint16_t tap1Offset = 10320; // 1 step  (~215ms)
    const uint16_t tap2Offset = 15480; // 1.5 steps (~322ms)
    const uint16_t tap3Offset = 30960; // 3 steps (~645ms, 3/8 waltz bounce)
    const uint16_t tap4Offset = 51600; // 5 steps (~1075ms)

    for (int i = 0; i < samples; i++) {
        int32_t inSample = busBuffer[i];

        int32_t tapOut1 = rainBuf[(rainWriteIdx - tap1Offset) & RAIN_DELAY_MASK];
        int32_t tapOut2 = rainBuf[(rainWriteIdx - tap2Offset) & RAIN_DELAY_MASK];
        int32_t tapOut3 = rainBuf[(rainWriteIdx - tap3Offset) & RAIN_DELAY_MASK];
        int32_t tapOut4 = rainBuf[(rainWriteIdx - tap4Offset) & RAIN_DELAY_MASK];

        int32_t rainCluster = ((tapOut1 * 75) + (tapOut2 * 65) + (tapOut3 * 80) + (tapOut4 * 50)) >> 8;

        rainLpFilter += ((rainCluster - rainLpFilter) * 60) >> 8;

        int32_t fbSample = inSample + ((rainLpFilter * 115) >> 8);
        if (fbSample > 32767) fbSample = 32767;
        else if (fbSample < -32768) fbSample = -32768;

        rainBuf[rainWriteIdx] = (int16_t)fbSample;
        rainWriteIdx++;

        busBuffer[i] = inSample + rainCluster;
    }
}

// Bus 2: Lush Ensemble Chorus
#define CHORUS_BUF_SIZE 2048
#define CHORUS_BUF_MASK (CHORUS_BUF_SIZE - 1)

void FX_LushEnsembleChorus(int32_t* busBuffer, int samples, int16_t* ep, int32_t* es) {
    static int16_t* chorusBuf = nullptr;
    static uint16_t chorusWriteIdx = 0;
    static uint32_t chorusLfo1 = 0;
    static uint32_t chorusLfo2 = 1073741824UL;

    if (chorusBuf == nullptr) {
        chorusBuf = (int16_t*)heap_caps_calloc(CHORUS_BUF_SIZE, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!chorusBuf) return;
    }

    const uint32_t lfoInc1 = 35791; // ~0.4 Hz
    const uint32_t lfoInc2 = 53687; // ~0.6 Hz

    for (int i = 0; i < samples; i++) {
        int32_t drySample = busBuffer[i];
        int32_t clamped = (drySample > 32767) ? 32767 : (drySample < -32768 ? -32768 : drySample);
        chorusBuf[chorusWriteIdx] = (int16_t)clamped;

        int32_t mod1 = (sineLUT[chorusLfo1 >> SINE_SHIFT] * 200) >> 15;
        uint16_t readIdx1 = (chorusWriteIdx - (650 + mod1)) & CHORUS_BUF_MASK;
        int32_t chorusTap1 = chorusBuf[readIdx1];

        int32_t mod2 = (sineLUT[chorusLfo2 >> SINE_SHIFT] * 240) >> 15;
        uint16_t readIdx2 = (chorusWriteIdx - (850 + mod2)) & CHORUS_BUF_MASK;
        int32_t chorusTap2 = chorusBuf[readIdx2];

        busBuffer[i] = drySample + (((chorusTap1 + chorusTap2) * 110) >> 8);

        chorusWriteIdx = (chorusWriteIdx + 1) & CHORUS_BUF_MASK;
        chorusLfo1 += lfoInc1;
        chorusLfo2 += lfoInc2;
    }
}

// Master Bus 0: 6-Comb Expansive Reverb + Tape Saturator (Zero Note Collision)
void DSP_AtmosphericSpaceReverb(int32_t* mixBuffer, int numSamples, int16_t* dp) {
    #define REV_CMB1 1116
    #define REV_CMB2 1188
    #define REV_CMB3 1277
    #define REV_CMB4 1356
    #define REV_CMB5 1422
    #define REV_CMB6 1514
    #define REV_AP1  225
    #define REV_AP2  556

    static int16_t* comb1_buf = nullptr; static int16_t* comb2_buf = nullptr;
    static int16_t* comb3_buf = nullptr; static int16_t* comb4_buf = nullptr;
    static int16_t* comb5_buf = nullptr; static int16_t* comb6_buf = nullptr;
    static int16_t* allpass1_buf = nullptr; static int16_t* allpass2_buf = nullptr;

    static uint16_t comb1_idx = 0, comb2_idx = 0, comb3_idx = 0, comb4_idx = 0, comb5_idx = 0, comb6_idx = 0;
    static uint16_t allpass1_idx = 0, allpass2_idx = 0;
    static int32_t comb1_damp = 0, comb2_damp = 0, comb3_damp = 0, comb4_damp = 0, comb5_damp = 0, comb6_damp = 0;

    if (comb1_buf == nullptr) {
        comb1_buf    = (int16_t*)heap_caps_calloc(REV_CMB1, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        comb2_buf    = (int16_t*)heap_caps_calloc(REV_CMB2, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        comb3_buf    = (int16_t*)heap_caps_calloc(REV_CMB3, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        comb4_buf    = (int16_t*)heap_caps_calloc(REV_CMB4, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        comb5_buf    = (int16_t*)heap_caps_calloc(REV_CMB5, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        comb6_buf    = (int16_t*)heap_caps_calloc(REV_CMB6, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        allpass1_buf = (int16_t*)heap_caps_calloc(REV_AP1,  sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        allpass2_buf = (int16_t*)heap_caps_calloc(REV_AP2,  sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!comb1_buf || !comb2_buf || !comb3_buf || !comb4_buf || !comb5_buf || !comb6_buf || !allpass1_buf || !allpass2_buf) return;
    }

    for (int i = 0; i < numSamples; i++) {
        int32_t drySample = mixBuffer[i];
        int32_t inRev = drySample >> 2;

        int32_t combOut1 = comb1_buf[comb1_idx]; comb1_damp += ((combOut1 - comb1_damp) * 64) >> 8;
        comb1_buf[comb1_idx] = (int16_t)(inRev + ((comb1_damp * 215) >> 8));
        if (++comb1_idx >= REV_CMB1) comb1_idx = 0;

        int32_t combOut2 = comb2_buf[comb2_idx]; comb2_damp += ((combOut2 - comb2_damp) * 64) >> 8;
        comb2_buf[comb2_idx] = (int16_t)(inRev + ((comb2_damp * 212) >> 8));
        if (++comb2_idx >= REV_CMB2) comb2_idx = 0;

        int32_t combOut3 = comb3_buf[comb3_idx]; comb3_damp += ((combOut3 - comb3_damp) * 64) >> 8;
        comb3_buf[comb3_idx] = (int16_t)(inRev + ((comb3_damp * 208) >> 8));
        if (++comb3_idx >= REV_CMB3) comb3_idx = 0;

        int32_t combOut4 = comb4_buf[comb4_idx]; comb4_damp += ((combOut4 - comb4_damp) * 64) >> 8;
        comb4_buf[comb4_idx] = (int16_t)(inRev + ((comb4_damp * 204) >> 8));
        if (++comb4_idx >= REV_CMB4) comb4_idx = 0;

        int32_t combOut5 = comb5_buf[comb5_idx]; comb5_damp += ((combOut5 - comb5_damp) * 64) >> 8;
        comb5_buf[comb5_idx] = (int16_t)(inRev + ((comb5_damp * 200) >> 8));
        if (++comb5_idx >= REV_CMB5) comb5_idx = 0;

        int32_t combOut6 = comb6_buf[comb6_idx]; comb6_damp += ((combOut6 - comb6_damp) * 64) >> 8;
        comb6_buf[comb6_idx] = (int16_t)(inRev + ((comb6_damp * 196) >> 8));
        if (++comb6_idx >= REV_CMB6) comb6_idx = 0;

        int32_t combMix = (combOut1 + combOut2 + combOut3 + combOut4 + combOut5 + combOut6) / 6;

        int32_t allpassOut1 = allpass1_buf[allpass1_idx];
        int32_t allpassVal1 = combMix + ((allpassOut1 * 128) >> 8);
        allpass1_buf[allpass1_idx] = (int16_t)allpassVal1;
        int32_t allpassSig1 = allpassOut1 - ((allpassVal1 * 128) >> 8);
        if (++allpass1_idx >= REV_AP1) allpass1_idx = 0;

        int32_t allpassOut2 = allpass2_buf[allpass2_idx];
        int32_t allpassVal2 = allpassSig1 + ((allpassOut2 * 128) >> 8);
        allpass2_buf[allpass2_idx] = (int16_t)allpassVal2;
        int32_t wet = allpassOut2 - ((allpassVal2 * 128) >> 8);
        if (++allpass2_idx >= REV_AP2) allpass2_idx = 0;

        int32_t outSample = drySample + ((wet * 105) >> 8);

        if (outSample > 24000) {
            outSample = 24000 + ((outSample - 24000) >> 1);
        } else if (outSample < -24000) {
            outSample = -24000 + ((outSample + 24000) >> 1);
        }

        mixBuffer[i] = outSample;
    }
}

// ====================================================================================
// MUSICAL COMPOSITION DATA (3/4 Time Signature, 12 Steps per Bar)
// ====================================================================================

const uint32_t CHORD_BASS[6] = {
    d2, b1, g1, a1, e2, fs2
};

const uint32_t CHORD_PADS[6][4] = {
    { fs3, a3,  cs4, e4  }, // 0: Dmaj9
    { d3,  fs3, a3,  cs4 }, // 1: Bm9
    { b2,  d3,  fs3, a3  }, // 2: Gmaj7
    { cs3, e3,  a3,  b3  }, // 3: Asus4
    { g3,  b3,  d4,  fs4 }, // 4: Em9
    { a3,  cs4, d4,  fs4 }  // 5: Dmaj9/F#
};

const uint32_t RAIN_MOTIFS[4][6] = {
    { fs4, a4, cs5, e5, a5, cs6 },
    { d4,  fs4, a4, cs5, fs5, a5 },
    { b3,  d4, fs4, a4,  d5, fs5 },
    { e4,  a4, b4,  cs5, e5, a5  }
};

const uint32_t THEME_1[12][12] = {
    { fs4, 0, 0, 0, a4,  0, 0, 0, cs5, 0, 0, 0 },
    { b4,  0, 0, 0, a4,  0, 0, 0, fs4, 0, 0, 0 },
    { g4,  0, 0, 0, a4,  0, 0, 0, d5,  0, 0, 0 },
    { cs5, 0, 0, 0, b4,  0, 0, 0, a4,  0, 0, 0 },
    { fs4, 0, 0, 0, cs5, 0, 0, 0, e5,  0, 0, 0 },
    { d5,  0, 0, 0, cs5, 0, 0, 0, b4,  0, 0, 0 },
    { a4,  0, 0, 0, b4,  0, 0, 0, fs4, 0, 0, 0 },
    { e4,  0, 0, 0, fs4, 0, 0, 0, d4,  0, 0, 0 },
    { a4,  0, 0, 0, cs5, 0, 0, 0, fs5, 0, 0, 0 },
    { e5,  0, 0, 0, d5,  0, 0, 0, cs5, 0, 0, 0 },
    { b4,  0, 0, 0, d5,  0, 0, 0, a4,  0, 0, 0 },
    { fs4, 0, 0, 0, e4,  0, 0, 0, d4,  0, 0, 0 }
};

const uint32_t THEME_CLIMAX[12][12] = {
    { fs5, 0, 0, 0, a5,  0, 0, 0, cs6, 0, 0, 0 },
    { b5,  0, 0, 0, a5,  0, 0, 0, fs5, 0, 0, 0 },
    { g5,  0, 0, 0, a5,  0, 0, 0, d6,  0, 0, 0 },
    { cs6, 0, 0, 0, b5,  0, 0, 0, a5,  0, 0, 0 },
    { fs5, 0, 0, 0, cs6, 0, 0, 0, e6,  0, 0, 0 },
    { d6,  0, 0, 0, cs6, 0, 0, 0, b5,  0, 0, 0 },
    { a5,  0, 0, 0, b5,  0, 0, 0, fs5, 0, 0, 0 },
    { e5,  0, 0, 0, fs5, 0, 0, 0, d5,  0, 0, 0 },
    { a5,  0, 0, 0, cs6, 0, 0, 0, fs6, 0, 0, 0 },
    { e6,  0, 0, 0, d6,  0, 0, 0, cs6, 0, 0, 0 },
    { b5,  0, 0, 0, d6,  0, 0, 0, a5,  0, 0, 0 },
    { fs5, 0, 0, 0, e5,  0, 0, 0, d5,  0, 0, 0 }
};

const uint32_t THEME_RESOLUTION[6][12] = {
    { b4, 0, 0, 0, a4,  0, 0, 0, fs4, 0, 0, 0 },
    { e4, 0, 0, 0, fs4, 0, 0, 0, d4,  0, 0, 0 },
    { g4, 0, 0, 0, fs4, 0, 0, 0, e4,  0, 0, 0 },
    { a4, 0, 0, 0, fs4, 0, 0, 0, d4,  0, 0, 0 },
    { e4, 0, 0, 0, cs4, 0, 0, 0, d4,  0, 0, 0 },
    { d4, 0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0 }
};

const uint32_t STEP_DURATION_MS = 215;
uint32_t lastStepTime = 0;
uint16_t currentStep = 0;

uint8_t getChordForBar(uint8_t bar) {
    if (bar < 12) return bar % 4;
    if (bar < 24) { const uint8_t chordSeqA[12] = {0, 1, 2, 3, 4, 2, 1, 3, 0, 1, 2, 3}; return chordSeqA[bar - 12]; }
    if (bar < 30) { const uint8_t chordSeqB[6]  = {1, 2, 4, 3, 1, 0}; return chordSeqB[bar - 24]; }
    if (bar < 42) { const uint8_t chordSeqC[12] = {0, 1, 2, 3, 5, 2, 4, 3, 0, 1, 2, 3}; return chordSeqC[bar - 30]; }
    if (bar < 48) { const uint8_t chordSeqD[6]  = {2, 3, 1, 0, 2, 0}; return chordSeqD[bar - 42]; }
    return 0;
}

void setup() {
    synth.begin(PIN_BCK, PIN_WS, PIN_DATA, I2S_32BIT);
    synth.setMasterVolume(155);

    synth.setBusFX(1, 0, FX_CUSTOM, FX_RaindropDelay);
    synth.setBusMix(1, 220);

    synth.setBusFX(2, 0, FX_CUSTOM, FX_LushEnsembleChorus);
    synth.setBusMix(2, 230);

    synth.setCustomDSP(DSP_AtmosphericSpaceReverb);

    synth.setVoiceBus(V_BASS,   0);
    synth.setVoiceBus(V_LEAD,   0);

    synth.setVoiceBus(V_PLUCK0, 1);
    synth.setVoiceBus(V_PLUCK1, 1);
    synth.setVoiceBus(V_BELL,   1);
    synth.setVoiceBus(V_HARP,   1);
    synth.setVoiceBus(V_DROP,   1);

    synth.setVoiceBus(V_PAD0,   2);
    synth.setVoiceBus(V_PAD1,   2);
    synth.setVoiceBus(V_PAD2,   2);
    synth.setVoiceBus(V_PAD3,   2);
    synth.setVoiceBus(V_AIR,    2);

    synth.setWave(V_BASS, WAVE_SINE);
    synth.setEnv(V_BASS, 60, 300, 200, 450);

    for (uint16_t v = V_PAD0; v <= V_PAD3; v++) {
        synth.setWave(v, WAVE_TRIANGLE);
        synth.setEnv(v, 700, 400, 210, 1600);
        synth.setSmoothEnv(v, true);
    }

    synth.setWave(V_PLUCK0, WAVE_SINE);
    synth.setWave(V_PLUCK1, WAVE_SINE);
    synth.setEnv(V_PLUCK0, 5, 240, 15, 380);
    synth.setEnv(V_PLUCK1, 5, 240, 15, 380);

    synth.setWave(V_LEAD, WAVE_SINE);
    synth.setEnv(V_LEAD, 100, 250, 220, 500);
    synth.setSmoothEnv(V_LEAD, true);
    synth.setVibrato(V_LEAD, 440, 15);

    synth.setWave(V_BELL, WAVE_SINE);
    synth.setEnv(V_BELL, 4, 420, 10, 650);

    synth.setWave(V_AIR, WAVE_TRIANGLE);
    synth.setEnv(V_AIR, 900, 500, 190, 1800);
    synth.setSmoothEnv(V_AIR, true);

    synth.setWave(V_HARP, WAVE_TRIANGLE);
    synth.setEnv(V_HARP, 12, 180, 25, 300);

    synth.setWave(V_DROP, WAVE_SINE);
    synth.setEnv(V_DROP, 1, 35, 0, 35);
}

void loop() {
    uint32_t now = millis();
    if (now - lastStepTime < STEP_DURATION_MS) {
        return;
    }
    lastStepTime = now;

    uint8_t bar = currentStep / 12;
    uint8_t stepInBar = currentStep % 12;
    uint8_t chordIdx = getChordForBar(bar);

    // Beat 1 (Step 0): Chord Pad Swell & Sub Bass Foundation
    if (stepInBar == 0) {
        if (bar < 53) {
            synth.noteOn(V_PAD0, CHORD_PADS[chordIdx][0], 22);
            synth.noteOn(V_PAD1, CHORD_PADS[chordIdx][1], 22);
            synth.noteOn(V_PAD2, CHORD_PADS[chordIdx][2], 20);
            synth.noteOn(V_PAD3, CHORD_PADS[chordIdx][3], 18);
        }

        if (bar >= 6 && bar < 52) {
            synth.noteOn(V_BASS, CHORD_BASS[chordIdx], 50);
        }

        if (bar >= 30 && bar < 42) {
            synth.noteOn(V_AIR, CHORD_PADS[chordIdx][3] * 2, 18);
        }
    }

    // Sub Bass Note-Off (Beat 3 release)
    if (stepInBar == 10 && synth.isVoiceActive(V_BASS)) {
        synth.noteOff(V_BASS);
    }

    // Air Pad Deactivation
    if (bar == 42 && stepInBar == 0 && synth.isVoiceActive(V_AIR)) {
        synth.noteOff(V_AIR);
    }

    // Outro Pad Release (Compass 53, Step 6)
    if (bar == 53 && stepInBar == 6) {
        synth.noteOff(V_PAD0);
        synth.noteOff(V_PAD1);
        synth.noteOff(V_PAD2);
        synth.noteOff(V_PAD3);
    }

    // Raindrop Pluck Waltzing Cascades (Active from Bar 6 to 47)
    if (bar >= 6 && bar < 48) {
        uint8_t arpIdx = chordIdx % 4;
        if (stepInBar == 0 || stepInBar == 4 || stepInBar == 8) {
            uint8_t motifStep = (stepInBar / 2) % 6;
            uint32_t pluckNote = RAIN_MOTIFS[arpIdx][motifStep];
            uint16_t targetPluck = (stepInBar == 4) ? V_PLUCK1 : V_PLUCK0;
            synth.noteOn(targetPluck, pluckNote, 32);
        }
    } else if (bar == 48 && stepInBar == 0) {
        synth.noteOff(V_PLUCK0);
        synth.noteOff(V_PLUCK1);
        synth.noteOff(V_BELL);
        synth.noteOff(V_DROP);
    }

    // Celestial Bell Droplets
    if ((bar >= 12 && bar < 24) || (bar >= 30 && bar < 42)) {
        if (stepInBar == 5) {
            synth.noteOn(V_BELL, fs5, 24);
        } else if (stepInBar == 10) {
            synth.noteOn(V_BELL, cs6, 22);
        }
    }

    // Raindrop Harp Stream
    if ((bar >= 24 && bar < 30) || (bar >= 32 && bar < 40)) {
        if (stepInBar == 2 || stepInBar == 6 || stepInBar == 10) {
            uint32_t harpNote = CHORD_PADS[chordIdx][(stepInBar / 3) % 4] * 2;
            synth.noteOn(V_HARP, harpNote, 28);
        }
    }
    if ((bar == 30 || bar == 41) && stepInBar == 0 && synth.isVoiceActive(V_HARP)) {
        synth.noteOff(V_HARP);
    }

    // Organic Micro Raindrop Blips
    if (bar >= 4 && bar < 48) {
        if ((stepInBar == 3 || stepInBar == 7 || stepInBar == 9) && ((currentStep * 7) % 5 == 0)) {
            uint32_t dropFreq = (stepInBar == 3) ? a5 : (stepInBar == 7 ? d6 : fs6);
            synth.noteOn(V_DROP, dropFreq, 18);
        }
    }

    // Lead Flute Melody Engine
    uint32_t leadFreq = 0;

    if (bar >= 12 && bar < 24) {
        leadFreq = THEME_1[bar - 12][stepInBar];
        if (leadFreq > 0) synth.noteOn(V_LEAD, leadFreq, 46);
        if (bar == 23 && stepInBar == 11) synth.noteOff(V_LEAD);
    } 
    else if (bar >= 30 && bar < 42) {
        leadFreq = THEME_CLIMAX[bar - 30][stepInBar];
        if (leadFreq > 0) {
            if (synth.isVoiceActive(V_LEAD)) {
                synth.slideFreqTo(V_LEAD, leadFreq, 85);
            } else {
                synth.noteOn(V_LEAD, leadFreq, 48);
            }
        }
        if (bar == 41 && stepInBar == 11) synth.noteOff(V_LEAD);
    } 
    else if (bar >= 42 && bar < 48) {
        leadFreq = THEME_RESOLUTION[bar - 42][stepInBar];
        if (leadFreq > 0) synth.noteOn(V_LEAD, leadFreq, 42);
        if (bar == 47 && stepInBar == 11) synth.noteOff(V_LEAD);
    } 
    else {
        if (synth.isVoiceActive(V_LEAD)) synth.noteOff(V_LEAD);
    }

    // Global Safety Sweep on Final Step (Seamless Loop Restart)
    if (currentStep == 647) {
        for (uint16_t v = 0; v <= V_DROP; v++) {
            if (synth.isVoiceActive(v)) {
                synth.noteOff(v);
            }
        }
    }

    currentStep = (currentStep + 1) % 648;
}