/*
  ====================================================================================
  ESP32Synth - Modular CFX & DSP Chiptune Showcase (2-Minute Track)
  Signal Flow & Effects Architecture:
  ----------------------------------------------------------------------------------
  1. BUS ROUTING (4 Hardware Buses):
     - Bus 0 (Master Dry):
         * Voice 0: Sub Bass (Triangle)
         * Voice 3: Punch Kick (Pitch-drop Bresenham)
         * Voice 4: Percussion (LFSR Noise Hats & Snare)
     - Bus 1 (Lead Acid Bus):
         * Voice 1: Saw Lead running through CFX_BiquadFilter (LPF, Q=3.8)
     - Bus 2 (Atmosphere & Echo Bus):
         * Voice 2: Arp Pluck (12.5% Pulse)
         * Voices 5, 6, 7: Chord Pads (DSP_BiquadOsc Pulse with rising PW & Cutoff)
         * FX Slot 0: CFX_TapeDelay (Dotted-16th echo)
     - Bus 3 (Riser FX Bus):
         * Voice 8: Pure White Noise Custom Wave (CW)
         * FX Slot 0: FX_BiquadFilter in Band-Pass Mode (BPF) with 280Hz medium BW

  2. HARDWARE:
     - PCM5102A I2S DAC in 32-bit mode (I2S_32BIT).
     - Classic ESP32: BCK=4, WS=15, DATA=2 | ESP32-S3: BCK=4, WS=6, DATA=5
  ====================================================================================
*/

#include <Arduino.h>
#include "ESP32Synth.h"
#include "ESP32Synth_Patches.hpp"

// Hardware pin layout for PCM5102A DAC
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define PIN_BCK  4
    #define PIN_WS   6
    #define PIN_DATA 5
#else
    #define PIN_BCK  4
    #define PIN_WS   15
    #define PIN_DATA 2
#endif

ESP32Synth synth;

#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// ====================================================================================
// 1. CUSTOM WAVE (CW): Pure White Noise Generator for Riser
// ====================================================================================
void CW_PureWhiteNoise(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t rng = vo->rngState;

    for (int i = 0; i < samples; i++) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;

        // 32-bit fast Linear Congruential Generator
        rng = (rng * 1664525UL) + 1013904223UL;
        int32_t noiseSample = (int32_t)(rng >> 16) - 32768;

        mixBuffer[i] += (noiseSample * finalVol) >> 16;
        currentEnv += envStep;
    }
    vo->rngState = rng;
}

// ====================================================================================
// 2. CUSTOM CFX: Integer Tape Delay / Echo (Bus 2 Slot 0)
// ====================================================================================
#define DELAY_LINE_SIZE 16384
#define DELAY_LINE_MASK (DELAY_LINE_SIZE - 1)

static int32_t* delayLineBuf = nullptr;
static uint32_t delayHead = 0;

void CFX_TapeDelay(int32_t* busBuffer, int samples, int16_t* ep, int32_t* es) {
    if (UNLIKELY(!delayLineBuf)) {
        delayLineBuf = (int32_t*)heap_caps_calloc(DELAY_LINE_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!delayLineBuf) return;
    }

    int32_t delayTime = ep[0]; // Samples (8640 = ~180ms at 48kHz)
    int32_t feedback  = ep[1]; // 0 to 255
    int32_t wetMix    = ep[2]; // 0 to 255

    for (int i = 0; i < samples; i++) {
        int32_t drySample = busBuffer[i];
        uint32_t readPos = (delayHead - delayTime) & DELAY_LINE_MASK;
        int32_t delayedSample = delayLineBuf[readPos];

        delayLineBuf[delayHead] = drySample + ((delayedSample * feedback) >> 8);
        delayHead = (delayHead + 1) & DELAY_LINE_MASK;

        busBuffer[i] = drySample + ((delayedSample * wetMix) >> 8);
    }
}

// ====================================================================================
// 3. TRACKER SEQUENCER PATTERN TABLES (Stored in Flash ROM)
// ====================================================================================
static const uint32_t bassPatterns[5][16] = {
    // 0: Cm
    { c2, 0, c2, 0, c2, 0, ds2, 0, c2, 0, c2, 0, as1, 0, g1, 0 },
    // 1: Ab
    { gs1, 0, gs1, 0, gs1, 0, c2, 0, gs1, 0, gs1, 0, ds2, 0, c2, 0 },
    // 2: Bb
    { as1, 0, as1, 0, as1, 0, d2, 0, as1, 0, as1, 0, f2, 0, d2, 0 },
    // 3: Gm
    { g1, 0, g1, 0, g1, 0, as1, 0, g1, 0, g1, 0, d2, 0, g1, 0 },
    // 4: Mute
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static const uint32_t arpPatterns[5][16] = {
    // 0: Cm Arp
    { c4, ds4, g4, c5, ds4, g4, c5, ds5, c4, ds4, g4, c5, ds4, g4, c5, g4 },
    // 1: Ab Arp
    { gs3, c4, ds4, gs4, c4, ds4, gs4, c5, gs3, c4, ds4, gs4, c4, ds4, gs4, ds4 },
    // 2: Bb Arp
    { as3, d4, f4, as4, d4, f4, as4, d5, as3, d4, f4, as4, d4, f4, as4, f4 },
    // 3: Gm Arp
    { g3, as3, d4, g4, as3, d4, g4, as4, g3, as3, d4, g4, as3, d4, g4, d4 },
    // 4: Mute
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static const uint32_t leadPatterns[6][16] = {
    // 0: Mute
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    // 1: Theme A - Part 1
    { c4, 0, ds4, 0, g4, 0, f4, ds4, f4, 0, ds4, 0, c4, 0, as3, 0 },
    // 2: Theme A - Part 2
    { c4, 0, ds4, 0, g4, 0, as4, 0, c5, 0, as4, 0, g4, 0, f4, ds4 },
    // 3: Theme B - Chorus Hook 1
    { g4, 0, c5, 0, as4, 0, g4, 0, f4, 0, ds4, f4, g4, 0, 0, 0 },
    // 4: Theme B - Chorus Hook 2
    { as4, 0, g4, 0, f4, 0, ds4, 0, f4, 0, ds4, 0, c4, 0, 0, 0 },
    // 5: Breakdown Stabs
    { c4, 0, 0, 0, ds4, 0, 0, 0, g4, 0, 0, 0, as4, 0, 0, 0 }
};

// Pad Chord Table (Triads: Voices 5, 6, 7)
static const uint32_t padChords[4][3] = {
    { c3,  ds3, g3  }, // 0: Cm
    { gs2, c3,  ds3 }, // 1: Ab
    { as2, d3,  f3  }, // 2: Bb
    { g2,  as2, d3  }  // 3: Gm
};

// Drums: 0=None, 1=Closed Hat, 2=Open Hat, 3=Snare, 4=Crash
static const uint8_t drumPatterns[6][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0 },
    { 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1 },
    { 1, 1, 2, 1, 3, 1, 2, 1, 1, 1, 2, 1, 3, 1, 2, 1 },
    { 4, 1, 2, 1, 3, 1, 2, 1, 1, 1, 2, 1, 3, 1, 2, 1 },
    { 4, 1, 2, 1, 3, 1, 2, 1, 3, 0, 3, 0, 3, 3, 3, 3 }
};

// Kick: 0=Mute, 1=Four-on-the-floor, 2=Syncopated driving kick
static const uint8_t kickPatterns[3][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0 }
};

struct SongStep {
    uint8_t kick;
    uint8_t drum;
    uint8_t bass;
    uint8_t lead;
    uint8_t arp;
};

// 64-Bar Arrangement Order Table (122.88 Seconds = 2m 03s)
static const SongStep songOrder[64] = {
    // Bars 0-7: Intro (Hats + Arp, Kick enters at bar 4)
    { 0, 1, 4, 0, 0 }, { 0, 1, 4, 0, 1 }, { 0, 1, 4, 0, 2 }, { 0, 1, 4, 0, 3 },
    { 1, 2, 4, 0, 0 }, { 1, 2, 4, 0, 1 }, { 1, 2, 4, 0, 2 }, { 1, 5, 4, 0, 3 },

    // Bars 8-15: Groove enters (Bass + Drums full + Arp + Pulse Pads enter)
    { 1, 3, 0, 0, 0 }, { 1, 3, 1, 0, 1 }, { 1, 3, 2, 0, 2 }, { 1, 3, 3, 0, 3 },
    { 1, 4, 0, 0, 0 }, { 1, 4, 1, 0, 1 }, { 1, 4, 2, 0, 2 }, { 1, 5, 3, 0, 3 },

    // Bars 16-31: Verse 1 / Theme A (Lead arrives with Biquad Filter squelch)
    { 1, 3, 0, 1, 0 }, { 1, 3, 1, 2, 1 }, { 1, 3, 2, 1, 2 }, { 1, 3, 3, 2, 3 },
    { 1, 4, 0, 1, 0 }, { 1, 4, 1, 2, 1 }, { 1, 4, 2, 1, 2 }, { 1, 5, 3, 2, 3 },
    { 1, 3, 0, 1, 0 }, { 1, 3, 1, 2, 1 }, { 1, 3, 2, 1, 2 }, { 1, 3, 3, 2, 3 },
    { 1, 4, 0, 1, 0 }, { 1, 4, 1, 2, 1 }, { 1, 4, 2, 1, 2 }, { 1, 5, 3, 2, 3 },

    // Bars 32-47: Chorus / Theme B (High energy, syncopated kick, opening pads)
    { 2, 4, 0, 3, 0 }, { 2, 4, 1, 4, 1 }, { 2, 4, 2, 3, 2 }, { 2, 4, 3, 4, 3 },
    { 2, 4, 0, 3, 0 }, { 2, 4, 1, 4, 1 }, { 2, 4, 2, 3, 2 }, { 2, 5, 3, 4, 3 },
    { 2, 4, 0, 3, 0 }, { 2, 4, 1, 4, 1 }, { 2, 4, 2, 3, 2 }, { 2, 4, 3, 4, 3 },
    { 2, 4, 0, 3, 0 }, { 2, 4, 1, 4, 1 }, { 2, 4, 2, 3, 2 }, { 1, 5, 3, 4, 3 },

    // Bars 48-55: Breakdown (Drums mute, White Noise CW Riser in Bus 3 sweeps up)
    { 0, 0, 4, 5, 0 }, { 0, 0, 4, 5, 1 }, { 0, 0, 4, 5, 2 }, { 0, 0, 4, 5, 3 },
    { 1, 1, 4, 5, 0 }, { 1, 2, 4, 5, 1 }, { 1, 2, 4, 5, 2 }, { 1, 5, 4, 5, 3 },

    // Bars 56-63: Drop Climax & Outro resolution
    { 2, 4, 0, 3, 0 }, { 2, 4, 1, 4, 1 }, { 2, 4, 2, 3, 2 }, { 2, 4, 3, 4, 3 },
    { 1, 3, 0, 1, 0 }, { 1, 3, 1, 2, 1 }, { 0, 2, 4, 0, 2 }, { 0, 1, 4, 0, 4 }
};

// ====================================================================================
// TIMING & STATE VARIABLES
// ====================================================================================
const uint32_t STEP_TIME_MS = 120; // 125 BPM (16th notes)
const uint32_t GATE_TIME_MS = 85;  // Note-off gate length

uint32_t lastStepTime = 0;
uint32_t lastFilterDecayTime = 0;
uint32_t leadGateOffTime = 0;

uint8_t  currentStep = 0;
uint8_t  currentBar  = 0;
int8_t   lastAnnouncedSection = -1;

bool leadNoteActive = false;
bool songFinished   = false;

int32_t filterCutoffCurrent = 350;
int32_t filterCutoffBase    = 350;
int32_t filterCutoffPeak    = 3800;

// ====================================================================================
// SOUND GENERATORS
// ====================================================================================
void triggerKick(uint8_t kickType) {
    if (kickType > 0) {
        synth.noteOn(3, 14000, 130);
        synth.slideFreq(3, 14000, 3200, 45); // Snappy pitch-drop
    }
}

void triggerDrum(uint8_t drumType) {
    switch (drumType) {
        case 1: // Closed Hi-Hat
            synth.setEnv(4, 1, 28, 0, 10);
            synth.noteOn(4, 85000, 75);
            break;
        case 2: // Open Hi-Hat
            synth.setEnv(4, 2, 110, 0, 30);
            synth.noteOn(4, 80000, 95);
            break;
        case 3: // Snare Drum
            synth.setEnv(4, 2, 75, 0, 25);
            synth.noteOn(4, 38000, 115);
            break;
        case 4: // Crash Cymbal
            synth.setEnv(4, 2, 220, 0, 50);
            synth.noteOn(4, 75000, 120);
            break;
        default:
            break;
    }
}

void triggerPads(uint8_t chordIdx) {
    if (chordIdx >= 4) return;
    synth.noteOn(5, padChords[chordIdx][0], 45);
    synth.noteOn(6, padChords[chordIdx][1], 45);
    synth.noteOn(7, padChords[chordIdx][2], 45);
}

// ====================================================================================
// SYNCHRONIZED SERIAL ANNOUNCER
// ====================================================================================
void announceSection(uint8_t bar) {
    int8_t currentSection = -1;
    if (bar == 0)       currentSection = 0;
    else if (bar == 4)  currentSection = 1;
    else if (bar == 8)  currentSection = 2;
    else if (bar == 16) currentSection = 3;
    else if (bar == 32) currentSection = 4;
    else if (bar == 48) currentSection = 5;
    else if (bar == 56) currentSection = 6;
    else if (bar == 62) currentSection = 7;

    if (currentSection != -1 && currentSection != lastAnnouncedSection) {
        lastAnnouncedSection = currentSection;
        Serial.println();
        Serial.print(F("[Bar "));
        if (bar < 10) Serial.print('0');
        Serial.print(bar);
        Serial.print(F("/64] "));

        switch (currentSection) {
            case 0:
                Serial.println(F("SECTION: INTRO"));
                Serial.println(F("  -> Bus 2 (CFX Delay): Dotted-16th active on Arp."));
                Serial.println(F("  -> Bus 0: Off-beat noise hi-hats."));
                break;
            case 1:
                Serial.println(F("SECTION: INTRO BUILD"));
                Serial.println(F("  -> Bus 0: Analog-style Kick drum enters (140Hz -> 32Hz)."));
                break;
            case 2:
                Serial.println(F("SECTION: GROOVE IN + PULSE CHORD PADS"));
                Serial.println(F("  -> Voices 5, 6, 7: Pulse Pad Chords engage via DSP_BiquadOsc (LPF Q=1.8)."));
                Serial.println(F("  -> Automation: Pulse Width & Cutoff modulate upwards across the track."));
                break;
            case 3:
                Serial.println(F("SECTION: VERSE 1 - THEME A"));
                Serial.println(F("  -> Bus 1 (CFX Biquad Filter): Resonant LPF (Q=3.8) acid squelch."));
                break;
            case 4:
                Serial.println(F("SECTION: CHORUS - THEME B"));
                Serial.println(F("  -> High-energy climax! Accent steps push filter to 5000Hz."));
                break;
            case 5:
                Serial.println(F("SECTION: BREAKDOWN & RISER"));
                Serial.println(F("  -> Voice 8: Pure White Noise CW engaged on Bus 3!"));
                Serial.println(F("  -> Bus 3 (CFX Biquad BPF): Band-Pass Filter (BW=280Hz) sweeping 250Hz -> 7500Hz."));
                Serial.println(F("  -> Bus 2 (CFX Delay): Feedback boosted to 68% for deep wash."));
                break;
            case 6:
                Serial.println(F("SECTION: FINAL OUTRO DROP"));
                Serial.println(F("  -> Riser Noise silenced immediately on drop! Groove returns at peak energy."));
                break;
            case 7:
                Serial.println(F("SECTION: OUTRO RESOLUTION"));
                Serial.println(F("  -> Instruments wind down. Final notes ring out into the CFX Tape Delay."));
                break;
        }
    }
}

void setup() {
    Serial.begin(115200);

    Serial.println(F("=================================================================="));
    Serial.println(F("  ESP32Synth - Modular CFX & DSP Chiptune Showcase"));
    Serial.println(F("  Engine: 100% Integer DSP | Dual-Core 240MHz | Zero Floats in Audio"));
    Serial.println(F("=================================================================="));

    // 1. Initialize audio engine in 32-bit I2S
    synth.begin(PIN_BCK, PIN_WS, PIN_DATA, I2S_32BIT);
    synth.setMasterVolume(140); // Mix headroom protection for 9 simultaneous voices

    // 2. CFX Setup: Bus 1 (Resonant Acid Biquad Filter)
    synth.setBusFX(1, 0, FX_CUSTOM, ESP32Patches::FX_BiquadFilter);
    synth.setBusFXParam(1, 0, 0, 400); // ep[0]: Cutoff (Hz)
    synth.setBusFXParam(1, 0, 1, 0);   // ep[1]: Mode (0 = LPF)
    synth.setBusFXParam(1, 0, 2, 380); // ep[2]: Q = 3.8
    synth.setBusFXParam(1, 0, 3, 0);   // ep[3]: Unit mode (Q)
    synth.setBusFXParam(1, 0, 4, 255); // ep[4]: 100% wet
    synth.setBusMix(1, 190);

    // 3. CFX Setup: Bus 2 (Dotted-16th Tape Echo Delay)
    synth.setBusFX(2, 0, FX_CUSTOM, CFX_TapeDelay);
    synth.setBusFXParam(2, 0, 0, 8640); // ep[0]: 180ms delay at 48kHz
    synth.setBusFXParam(2, 0, 1, 140);  // ep[1]: Feedback (55%)
    synth.setBusFXParam(2, 0, 2, 130);  // ep[2]: Wet mix
    synth.setBusMix(2, 165);

    // 4. CFX Setup: Bus 3 (Noise Riser Band-Pass Filter)
    synth.setBusFX(3, 0, FX_CUSTOM, ESP32Patches::FX_BiquadFilter);
    synth.setBusFXParam(3, 0, 0, 250); // ep[0]: Start center freq = 250 Hz
    synth.setBusFXParam(3, 0, 1, 2);   // ep[1]: Filter Mode = 2 (BPF)
    synth.setBusFXParam(3, 0, 2, 280); // ep[2]: Bandwidth = 280 Hz (medium BW)
    synth.setBusFXParam(3, 0, 3, 1);   // ep[3]: Unit Mode = 1 (Bandwidth in Hz mode)
    synth.setBusFXParam(3, 0, 4, 255); // ep[4]: 100% wet
    synth.setBusMix(3, 175);

    // 5. Voice Allocation
    // Voice 0: Bass -> Bus 0 (Direct Master)
    synth.setVoiceBus(0, 0);
    synth.setWave(0, WAVE_TRIANGLE);
    synth.setEnv(0, 4, 85, 0, 40);

    // Voice 1: Lead -> Bus 1 (Biquad Filter)
    synth.setVoiceBus(1, 1);
    synth.setWave(1, WAVE_SAW);
    synth.setEnv(1, 6, 95, 20, 35);

    // Voice 2: Arp Pluck -> Bus 2 (Delay Bus)
    synth.setVoiceBus(2, 2);
    synth.setWave(2, WAVE_PULSE);
    synth.setPulseWidth(2, 32); // 12.5% NES pulse
    synth.setEnv(2, 3, 65, 0, 45);

    // Voice 3: Kick Drum -> Bus 0 (Direct Master)
    synth.setVoiceBus(3, 0);
    synth.setWave(3, WAVE_TRIANGLE);
    synth.setEnv(3, 2, 70, 0, 30);

    // Voice 4: Percussion Noise -> Bus 0 (Direct Master)
    synth.setVoiceBus(4, 0);
    synth.setWave(4, WAVE_NOISE);

    // Voices 5, 6, 7: Pulse Chord Pads via DSP_BiquadOsc -> Bus 2 (Delay Bus)
    for (int v = 5; v <= 7; v++) {
        synth.setVoiceBus(v, 2);
        synth.setCustomWave(v, ESP32Patches::DSP_BiquadOsc);
        synth.setCustomParam(v, 0, 1);   // cp[0]: 1 = Pulse
        synth.setCustomParam(v, 1, 450); // cp[1]: Initial Cutoff = 450 Hz
        synth.setCustomParam(v, 2, 180); // cp[2]: Resonance Q = 1.8
        synth.setCustomParam(v, 3, 0);   // cp[3]: Mode = LPF
        synth.setPulseWidth(v, 35);      // Initial narrow PW
        synth.setEnv(v, 35, 0, 60, 120);// Slow attack & smooth release
    }

    // Voice 8: Pure White Noise CW Riser -> Bus 3 (Band-Pass Filter Bus)
    synth.setVoiceBus(8, 3);
    synth.setCustomWave(8, CW_PureWhiteNoise);
    synth.setEnv(8, 10, 0, 255, 60);

    lastStepTime = millis();
    lastFilterDecayTime = millis();
}

void loop() {
    uint32_t now = millis();

    // ------------------------------------------------------------------------
    // LEAD GATE TIMER (Prevents Stuck Notes)
    // ------------------------------------------------------------------------
    if (leadNoteActive && now >= leadGateOffTime) {
        synth.noteOff(1);
        leadNoteActive = false;
    }

    // ------------------------------------------------------------------------
    // FILTER ENVELOPE DECAY (Simulates Analog VCF)
    // ------------------------------------------------------------------------
    if (now - lastFilterDecayTime >= 6) {
        lastFilterDecayTime = now;
        
        if (currentBar < 48 || currentBar >= 56) {
            if (filterCutoffCurrent > filterCutoffBase) {
                filterCutoffCurrent -= 110;
                if (filterCutoffCurrent < filterCutoffBase) filterCutoffCurrent = filterCutoffBase;
                synth.setBusFXParam(1, 0, 0, (int16_t)filterCutoffCurrent);
            }
        }
    }

    // Stop all sequencer execution once the song is complete
    if (songFinished) {
        return;
    }

    // ------------------------------------------------------------------------
    // TRACKER STEP SEQUENCER
    // ------------------------------------------------------------------------
    if (now - lastStepTime >= STEP_TIME_MS) {
        lastStepTime = now;

        // Synchronized Serial announcement at bar boundaries
        if (currentStep == 0) {
            announceSection(currentBar);

            // Trigger Pad Chords on downbeats from Bar 8 onwards
            if (currentBar >= 8 && currentBar < 63) {
                uint8_t chordIdx = currentBar % 4;
                triggerPads(chordIdx);
            }

            // Continuous Pad Evolution: Modulate PW and LP Cutoff upwards
            uint32_t padPW = 35 + ((currentBar * 145) >> 6); // 35 -> 177
            int16_t padCutoff = 450 + (int16_t)((currentBar * 1920) >> 6); // 450Hz -> 2370Hz
            for (int v = 5; v <= 7; v++) {
                synth.setPulseWidth(v, padPW);
                synth.setCustomParam(v, 1, padCutoff);
            }
        }

        const SongStep* stepData = &songOrder[currentBar];

        // 1. Trigger Kick Drum
        uint8_t kTrig = kickPatterns[stepData->kick][currentStep];
        triggerKick(kTrig);

        // 2. Trigger Hi-Hats / Snare
        uint8_t dTrig = drumPatterns[stepData->drum][currentStep];
        triggerDrum(dTrig);

        // 3. Trigger Bass Note
        uint32_t bNote = bassPatterns[stepData->bass][currentStep];
        if (bNote > 0) {
            synth.noteOn(0, bNote, 95);
        }

        // 4. Trigger Lead Melody (Bus 1 Filter)
        uint32_t lNote = leadPatterns[stepData->lead][currentStep];
        if (lNote > 0) {
            if (currentStep == 0 || currentStep == 8) {
                filterCutoffCurrent = filterCutoffPeak + 1200; // Accent bite
                synth.noteOn(1, lNote, 115);
            } else {
                filterCutoffCurrent = filterCutoffPeak;
                synth.noteOn(1, lNote, 95);
            }
            synth.setBusFXParam(1, 0, 0, (int16_t)filterCutoffCurrent);

            leadGateOffTime = now + GATE_TIME_MS;
            leadNoteActive  = true;
        } else {
            if (leadNoteActive) {
                synth.noteOff(1);
                leadNoteActive = false;
            }
        }

        // 5. Trigger Arpeggio Pluck (Bus 2 Delay)
        uint32_t aNote = arpPatterns[stepData->arp][currentStep];
        if (aNote > 0) {
            synth.noteOn(2, aNote, 70);
        }

        // 6. Dynamic Breakdown & CW White Noise Riser (Bars 48 to 55)
        if (currentBar >= 48 && currentBar < 56) {
            int stepIdx = (currentBar - 48) * 16 + currentStep; // 0 to 127

            // Smoothly sweep Lead Cutoff on Bus 1
            int32_t sweepCutoff = 200 + (stepIdx * 31);
            synth.setBusFXParam(1, 0, 0, (int16_t)sweepCutoff);
            synth.setBusFXParam(2, 0, 1, 175); // Delay wash

            // Riser CW White Noise on Bus 3: Sweep BPF Center Freq (250Hz -> ~7500Hz) & Ramp Volume
            int32_t riserFreq = 250 + (stepIdx * 57);
            uint8_t riserVol  = 15 + ((stepIdx * 95) >> 7);
            synth.setBusFXParam(3, 0, 0, (int16_t)riserFreq);

            if (currentBar == 48 && currentStep == 0) {
                synth.noteOn(8, 1000, riserVol);
            } else {
                synth.setVolume(8, riserVol);
            }
        } else if (currentBar == 56 && currentStep == 0) {
            // Drop Hits: Instant kill of riser noise & restore delay feedback
            synth.noteOff(8);
            synth.setBusFXParam(2, 0, 1, 140);
        }

        // Advance 16th step and bar counter
        currentStep++;
        if (currentStep >= 16) {
            currentStep = 0;
            currentBar++;

            // Finish after exactly 64 bars (2 minutes and 3 seconds)
            if (currentBar >= 64) {
                songFinished = true;
                for (int v = 0; v <= 8; v++) {
                    synth.noteOff(v);
                }

                Serial.println();
                Serial.println(F("=================================================================="));
                Serial.println(F("  [SONG FINISHED] - 64 Bars (02:03) Completed."));
                Serial.println(F("  Playback stopped. Delay tail dissipating cleanly."));
                Serial.println(F("=================================================================="));
            }
        }
    }
}