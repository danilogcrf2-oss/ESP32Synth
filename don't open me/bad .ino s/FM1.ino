// ====================================================================================
// ESP32Synth - 4-Operator FM Synthesis & Custom DSP Ping-Pong Delay
// Highly Optimized Integer-Math Audio Engine
// ====================================================================================

#include <Arduino.h>
#include <ESP32Synth.h>

ESP32Synth synth;

// Access the highly optimized Sine Look-Up Table from ESP32Synth Core
extern int16_t sineLUT[];

// ====================================================================================
// 4-OPERATOR FM SYNTHESIS (CUSTOM WAVE ENGINE)
// ====================================================================================

// Structure for 4-Op FM Patches
struct FMInst { 
    uint16_t r1, r2, r3, r4;  // Frequency Multipliers (Fixed-Point: 256 = 1.0x)
    int32_t  m1, m2, m3;      // Modulation Indices (How much one Op affects the next)
    int32_t  dec;             // Modulator Envelope Decay Speed
};

// Instrument Definitions
const FMInst insts[6] = {
    {256, 256, 256, 256,  0,   0,   0,   0},     // 0: Init (Safe default)
    {256, 128, 256, 256,  0,   0,   120, 200},   // 1: FM Bass (Punchy, 2-Op style)
    {256, 512, 768, 1024, 0,   30,  80,  15},    // 2: FM Flute (Harmonic, slow env)
    {256, 256, 258, 256,  0,   80,  120, 400},   // 3: FM Guitar (Plucky, detuned)
    {256, 512, 1024,2048, 40,  60,  80,  250},   // 4: FM Piano (Complex attack)
    {256, 362, 696, 804,  150, 180, 220, 100}    // 5: FM Bells (Inharmonic ratios)
};

// The core callback for our 4-Operator FM engine
void IRAM_ATTR customFM(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    // Initialization stage (ESP32Synth clears cw[0-5] on noteOn for safety)
    if (vo->cw[5] == 0) { 
        vo->cw[0] = vo->startPhase; // Hack: We pass the Instrument ID via startPhase!
        vo->cw[5] = 65536;          // Set Modulator Envelope to Max (Fixed-Point 1.0)
        vo->cw[1] = 0; vo->cw[2] = 0; vo->cw[3] = 0; vo->cw[4] = 0; // Clear phases
    }

    uint8_t id = vo->cw[0];
    if (id > 5) id = 0;
    const FMInst* inst = &insts[id];

    // Load states into fast local registers
    uint32_t p1 = vo->cw[1], p2 = vo->cw[2], p3 = vo->cw[3], p4 = vo->cw[4];
    int32_t modEnv = vo->cw[5];

    // Pre-calculate increments based on the root frequency
    uint32_t i1 = (vo->phaseInc * inst->r1) >> 8;
    uint32_t i2 = (vo->phaseInc * inst->r2) >> 8;
    uint32_t i3 = (vo->phaseInc * inst->r3) >> 8;
    uint32_t i4 = (vo->phaseInc * inst->r4) >> 8;

    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;

    for (int i = 0; i < samples; i++) {
        p1 += i1; p2 += i2; p3 += i3; p4 += i4;

        // Cascade FM Algorithm (Op1 -> Op2 -> Op3 -> Op4)
        // Bitwise logic heavily optimized for Xtensa CPU pipeline
        int32_t o1 = sineLUT[(p1 >> 20) & 0xFFF];
        int32_t mod1 = (o1 * inst->m1 * (modEnv >> 8)) >> 15;

        int32_t o2 = sineLUT[((p2 >> 20) + mod1) & 0xFFF];
        int32_t mod2 = (o2 * inst->m2 * (modEnv >> 8)) >> 15;

        int32_t o3 = sineLUT[((p3 >> 20) + mod2) & 0xFFF];
        int32_t mod3 = (o3 * inst->m3 * (modEnv >> 8)) >> 15;

        int32_t out = sineLUT[((p4 >> 20) + mod3) & 0xFFF];

        // Process Modulator Envelope
        if (modEnv > 0) {
            modEnv -= inst->dec;
            if (modEnv < 0) modEnv = 0;
        }

        // Apply Main Master ADSR Envelope
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;

        mixBuffer[i] += (out * finalVol) >> 16;
        currentEnv += envStep;
    }

    // Save states back to the struct
    vo->cw[1] = p1; vo->cw[2] = p2; vo->cw[3] = p3; vo->cw[4] = p4;
    vo->cw[5] = modEnv;
    vo->phase = p1; 
}

// Helper function to safely trigger a custom FM note
void playFMNote(uint16_t voice, uint8_t instId, uint32_t note, uint16_t vol) {
    synth.setStartPhase(voice, instId); // Store the ID to be picked up by customFM
    synth.setCustomWave(voice, customFM);
    synth.noteOn(voice, note, vol);
}

// ====================================================================================
// ANALOG ECHO (CUSTOM DSP ENGINE)
// ====================================================================================

#define DELAY_SAMPLES 12000 // Approx 250ms at 48kHz (Requires ~48KB RAM)
int32_t delayBuf[DELAY_SAMPLES];
uint32_t delayIdx = 0;

void IRAM_ATTR customEchoDSP(int32_t* mixBuffer, int samples) {
    static int32_t lastDly = 0; // State for our 1-tap Lowpass filter
    
    for(int i = 0; i < samples; i++) {
        int32_t in = mixBuffer[i];
        int32_t dly = delayBuf[delayIdx];

        // IIR Lowpass filter on the delay line to simulate analog tape decay
        int32_t filtered = (dly + lastDly) >> 1;
        lastDly = filtered;

        // Mix 50% wet into the main bus
        mixBuffer[i] = in + (filtered >> 1);

        // Feedback calculation (153/256 = ~60% feedback)
        delayBuf[delayIdx] = in + ((filtered * 153) >> 8); 

        delayIdx++;
        if (delayIdx >= DELAY_SAMPLES) delayIdx = 0;
    }
}

// ====================================================================================
// SEQUENCER & SETUP
// ====================================================================================

uint32_t lastTick = 0;
uint32_t currentStep = 0;
const uint32_t TICK_MS = 125; // 120 BPM, 16th notes

void setup() {
    Serial.begin(115200);

    // Setup I2S output (Matches PCM5102A hardware pins)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    synth.begin(4, 6, 5, I2S_32BIT); // ESP32-S3: BCLK=4, WS=6, DIN=5
#else
    synth.begin(4, 15, 2, I2S_32BIT); // ESP32: BCLK=4, WS=15, DIN=2
#endif

    synth.setMasterVolume(100); // 0-255 scale
    
    // Register the DSP Effect
    synth.setCustomDSP(customEchoDSP);

    Serial.println("ESP32Synth: 4-Op FM & Analog Echo Engine Active.");
}

void loop() {
    if (millis() - lastTick >= TICK_MS) {
        lastTick = millis();
        int step = currentStep % 32;

        // --- TRACK 1: DRUMS (Voices 0 & 1) ---
        // Using native SINE and NOISE for maximal efficiency!
        if (step % 8 == 0) { 
            // Kick (Fast pitch drop trick)
            synth.setWave(0, WAVE_SINE); 
            synth.setEnv(0, 0, 150, 0, 0); 
            synth.noteOn(0, c2, 255); 
            synth.slideFreqTo(0, c1, 80); 
        }
        if (step % 8 == 4) { 
            // Snare
            synth.setWave(1, WAVE_NOISE); 
            synth.setEnv(1, 0, 100, 0, 0); 
            synth.noteOn(1, c4, 180); 
        }
        if (step % 2 == 1) { 
            // Hihat
            synth.setWave(1, WAVE_NOISE); 
            synth.setEnv(1, 0, 30, 0, 0); 
            synth.noteOn(1, c6, 70); 
        }

        // --- TRACK 2: FM BASS (Voice 2) ---
        const uint32_t bassLine[32] = {a2,0,0,a2, 0,0,g2,0, f2,0,0,f2, 0,0,e2,0, c2,0,0,c2, 0,0,b1,0, g1,0,0,g1, 0,0,0,0};
        if (bassLine[step]) {
            synth.setEnv(2, 5, 200, 0, 50);
            playFMNote(2, 1, bassLine[step], 255); // Inst 1: Bass
        }

        // --- TRACK 3: FM PIANO CHORDS (Voices 3, 4, 5) ---
        int chordIdx = (step / 8) % 4;
        const uint32_t chords[4][3] = {
            {a3, c4, e4}, // Am
            {f3, a3, c4}, // F
            {c3, e3, g3}, // C
            {g2, b2, d3}  // G
        };
        if (step % 8 == 0 || step % 8 == 3 || step % 8 == 6) {
            synth.setEnv(3, 10, 300, 0, 200);
            synth.setEnv(4, 10, 300, 0, 200);
            synth.setEnv(5, 10, 300, 0, 200);
            
            playFMNote(3, 4, chords[chordIdx][0], 100); // Inst 4: Piano
            playFMNote(4, 4, chords[chordIdx][1], 100);
            playFMNote(5, 4, chords[chordIdx][2], 100);
        }

        // --- TRACK 4: FM MELODY (Voice 6) ---
        const uint32_t melody[32] = {a4,0,e5,0, c5,0,0,0, f4,0,c5,0, a4,0,0,0, g4,0,e5,0, c5,0,0,0, d4,0,g4,0, b4,0,d5,0};
        if (melody[step]) {
            // Alternate between Flute (Inst 2) and Bells (Inst 5)
            uint8_t inst = (step < 16) ? 2 : 5;
            synth.setEnv(6, 20, 500, 50, 500);
            playFMNote(6, inst, melody[step], 150);
        }

        currentStep++;
    }
}