#include <Arduino.h>
#include "ESP32Synth.h"
#include "ESP32Synth_FLT.hpp"

ESP32Synth synth;

// ----------------------------------------------------------------------------
// TEST 1: The Baker (Additive Synthesis Pre-compiled at Setup)
// ----------------------------------------------------------------------------
fip bakedWave(fip phase) {
    fip h1 = fip::sin(phase);
    fip h2 = fip::sin(phase * fip(2.0f)) * fip(0.4f);
    fip h3 = fip::sin(phase * fip(3.0f)) * fip(0.25f);
    fip h4 = fip::sin(phase * fip(4.0f)) * fip(0.1f);
    
    return (h1 + h2 + h3 + h4) * fip(0.57f); 
}
const int16_t* bakedWavePtr = nullptr;

// ----------------------------------------------------------------------------
// TEST 2: Buchla 259 Style Wavefolder (100% Fixed-Point, Continuous Sine Source)
// ----------------------------------------------------------------------------
void customWavefolder(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    
    fip phase = fip::fromPhase(vo->phase);
    fip inc   = fip::fromPhase(vo->phaseInc + vo->vibOffset);
    fip foldAmount = fip::fromParam(vo->cp[0], (int16_t)100); 
    
    fip drive = fip::lerp(fip(1.0f), fip(4.0f), foldAmount); 

    for (int i = 0; i < samples; i++) {
        fip source = fip::sin(phase); 
        fip folded = fip::sin(source * drive * fip(0.25f)); 
        
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;
        
        mixBuffer[i] += (folded.toAudio16() * finalVol) >> 16;
        
        phase += inc; currentEnv += envStep;
    }
    vo->phase = (uint32_t)(phase.val << 8); 
}

// ----------------------------------------------------------------------------
// TEST 3: Custom One-Pole Low-Pass Filter (Zero Runtime Floats)
// ----------------------------------------------------------------------------
void customOnePoleLPF(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    
    fip phase = fip::fromPhase(vo->phase);
    fip inc   = fip::fromPhase(vo->phaseInc + vo->vibOffset);
    
    // Normalized cutoff ratios: 100Hz/48kHz = 0.00208333f | 4000Hz/48kHz = 0.08333333f
    fip paramCutoff = fip(0.00208333f) + (fip::fromParam(vo->cp[0], (int16_t)100) * fip(0.08333333f));
    fip wc = fip::two_pi() * paramCutoff; 
    fip alpha = wc / (fip(1.0f) + wc);
    
    fip z1 = fip((int32_t)vo->cw[0], true);

    for (int i = 0; i < samples; i++) {
        fip square = (fip::frac(phase) < fip(0.5f)) ? fip(1.0f) : fip(-1.0f);
        z1 += (square - z1) * alpha; 
        
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;
        
        mixBuffer[i] += (z1.toAudio16() * finalVol) >> 16;
        
        phase += inc; currentEnv += envStep;
    }
    
    vo->phase = (uint32_t)(phase.val << 8);
    vo->cw[0] = (uint32_t)z1.val; 
}

// ----------------------------------------------------------------------------
// TEST 4: Global Master Distortion (Fast Bitwise Fixed-Point Fuzz)
// ----------------------------------------------------------------------------
void customMasterDistortion(int32_t* mixBuffer, int numSamples, int16_t* dp) {
    fip drive = fip::fromParam(dp[0], (int16_t)100) * fip(8.0f); 
    if (drive.val == 0) return; 

    for (int i = 0; i < numSamples; i++) {
        int64_t scaled = ((int64_t)mixBuffer[i]) << 9;
        if (scaled > 2147483647LL) scaled = 2147483647LL;
        if (scaled < -2147483648LL) scaled = -2147483648LL;
        
        fip sample((int32_t)scaled, true); 
        fip distorted = fip::fast_tanh(sample * drive);
        
        mixBuffer[i] = distorted.toAudio16(); 
    }
}

// --- Presentation State Machine ---
int state = 0;
unsigned long lastStateChange = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n--- [FLT] FIP MATHEMATICAL VALIDATION ---");
    fip testMul = fip(2.5f) * fip(2.0f);
    fip testSqrt = fip::sqrt(fip(81.0f));
    
    Serial.printf("Multiplication Test (2.5 * 2.0): %.2f (Expected: 5.00)\n", testMul.toFloat());
    Serial.printf("Square Root Test sqrt(81.0): %.2f (Expected: 9.00)\n", testSqrt.toFloat());

    synth.begin(4, 6, 5, I2S_16BIT);
    
    bakedWavePtr = FLT_Baker::bakeWavetable(&synth, 0, bakedWave, SINE_LUT_SIZE);
    
    synth.setMasterVolume(128); 
    synth.setEnv(0, 5, 0, 255, 300); 
    synth.setSmoothEnv(0, true);

    synth.setCustomDSP(customMasterDistortion);
    synth.setDSPParam(0, 0); 
}

void loop() {
    unsigned long now = millis();

    if (now - lastStateChange > 4000) {
        lastStateChange = now;
        synth.noteOff(0);
        delay(300); 
        
        synth.setDSPParam(0, 0); 
        
        if (state == 0) {
            Serial.println("\n[Module 1] FLT Baker (Baked Additive Wavetable)");
            synth.setWavetable(0, bakedWavePtr, SINE_LUT_SIZE, BITS_16);
            synth.setWave(0, WAVE_WAVETABLE);
            synth.noteOn(0, c4, 255);
        }
        else if (state == 1) {
            Serial.println("\n[Module 2] Custom Wavefolder (Buchla 259 Style)");
            synth.setCustomWave(0, customWavefolder);
            synth.noteOn(0, c3, 255);
        }
        else if (state == 2) {
            Serial.println("\n[Module 3] Custom One-Pole Low-Pass Filter");
            synth.setCustomWave(0, customOnePoleLPF);
            synth.noteOn(0, c3, 255);
        }
        else if (state == 3) {
            Serial.println("\n[Module 4] Global Master DSP (Dynamic Saturation)");
            synth.setWave(0, WAVE_SINE);
            synth.noteOn(0, c2, 255);
        }
        
        state = (state + 1) % 4;
    }

    int currentPhase = (state == 0) ? 3 : (state - 1); 
    
    if (currentPhase == 1 || currentPhase == 2) { 
        int knob = (abs((int)(now % 2000) - 1000) * 100) / 1000;
        synth.setCustomParam(0, 0, knob); 
    } 
    else if (currentPhase == 3) {
        int knob = (abs((int)(now % 4000) - 2000) * 100) / 2000; 
        synth.setDSPParam(0, knob); 
    }

    static unsigned long lastPrint = 0;
    if (now - lastPrint > 1000) {
        lastPrint = now;
        Serial.printf("DSP CPU Load: %.2f%%\n", synth.getCPULoad());
    }
}