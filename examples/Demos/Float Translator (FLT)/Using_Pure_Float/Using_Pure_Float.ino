// please don't do that, it is much slower and ineficient.
#include <Arduino.h>
#include "ESP32Synth.h"

ESP32Synth synth;

// ----------------------------------------------------------------------------
// TEST 1: The Baker (Pure Float Additive Synthesis Pre-computed at Setup)
// ----------------------------------------------------------------------------
float bakedWaveFloat(float phase) {
    // phase: 0.0f to 1.0f (Standard normalized cycle)
    float rad = phase * 2.0f * (float)M_PI;
    
    float h1 = sinf(rad);
    float h2 = sinf(rad * 2.0f) * 0.4f;
    float h3 = sinf(rad * 3.0f) * 0.25f;
    float h4 = sinf(rad * 4.0f) * 0.1f;
    
    return (h1 + h2 + h3 + h4) * 0.57f;
}

const int16_t* bakedWavePtr = nullptr;

const int16_t* bakeFloatWavetable(ESP32Synth* engine, uint16_t tableId, uint32_t size) {
    int16_t* data16 = (int16_t*)heap_caps_malloc(size * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!data16) return nullptr;
    
    for (uint32_t i = 0; i < size; i++) {
        float p = (float)i / (float)size;
        float sampleFloat = bakedWaveFloat(p);
        
        // Final float to 16-bit audio conversion
        data16[i] = (int16_t)(sampleFloat * 32767.0f);
    }
    
    engine->registerWavetable(tableId, data16, size, BITS_16);
    return data16;
}

// ----------------------------------------------------------------------------
// TEST 2: Buchla 259 Style Wavefolder (Pure Native Float Math)
// ----------------------------------------------------------------------------
void nativeFloatWavefolder(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    
    // Convert 32-bit hardware integers into 0.0f - 1.0f floating-point domain
    float phase    = (float)vo->phase / 4294967296.0f;
    float phaseInc = (float)(vo->phaseInc + vo->vibOffset) / 4294967296.0f;
    float foldKnob = (float)vo->cp[0] / 100.0f;
    float drive    = 1.0f + (foldKnob * 3.0f); // 1.0f to 4.0f

    for (int i = 0; i < samples; i++) {
        // Pure floating-point trigonometric synthesis
        float source = sinf(phase * 2.0f * (float)M_PI);
        float folded = sinf(source * drive * (float)M_PI * 0.5f);
        
        // Single conversion back to integer audio domain
        int32_t audio16 = (int32_t)(folded * 32767.0f);
        
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;
        mixBuffer[i] += (audio16 * finalVol) >> 16;
        
        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;
        currentEnv += envStep;
    }
    
    // Save phase accumulator back to engine
    vo->phase = (uint32_t)(phase * 4294967296.0f);
}

// ----------------------------------------------------------------------------
// TEST 3: Custom One-Pole Low-Pass Filter (Pure Native Float Math)
// ----------------------------------------------------------------------------
void nativeFloatOnePoleLPF(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    
    float phase    = (float)vo->phase / 4294967296.0f;
    float phaseInc = (float)(vo->phaseInc + vo->vibOffset) / 4294967296.0f;
    
    // Standard floating-point cutoff filter calculation in Hertz
    float cutoffHz = 100.0f + (((float)vo->cp[0] / 100.0f) * 3900.0f); // 100Hz to 4000Hz
    float wc = 2.0f * (float)M_PI * cutoffHz / 48000.0f;
    float alpha = wc / (1.0f + wc);
    
    // Retrieve previous filter state stored as float in voice cw[0]
    float z1 = *(float*)&vo->cw[0];

    for (int i = 0; i < samples; i++) {
        float square = (phase < 0.5f) ? 1.0f : -1.0f;
        z1 += (square - z1) * alpha;
        
        // Single conversion to 16-bit audio domain
        int32_t audio16 = (int32_t)(z1 * 32767.0f);
        
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;
        mixBuffer[i] += (audio16 * finalVol) >> 16;
        
        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;
        currentEnv += envStep;
    }
    
    vo->phase = (uint32_t)(phase * 4294967296.0f);
    *(float*)&vo->cw[0] = z1; // Store filter state back
}

// ----------------------------------------------------------------------------
// TEST 4: Global Master Distortion (Pure Native Float Hyperbolic Saturation)
// ----------------------------------------------------------------------------
void nativeFloatMasterDistortion(int32_t* mixBuffer, int numSamples, int16_t* dp) {
    float drive = ((float)dp[0] / 100.0f) * 8.0f;
    if (drive <= 0.001f) return;

    for (int i = 0; i < numSamples; i++) {
        // Convert 32-bit mixer sample into floating-point normalized range [-1.0f, +1.0f]
        float sample = (float)mixBuffer[i] / 32768.0f;
        
        // Native IEEE 754 float math
        float distorted = tanhf(sample * drive);
        
        // Final single conversion back into mixBuffer
        mixBuffer[i] = (int32_t)(distorted * 32767.0f);
    }
}

// --- Presentation State Machine ---
int state = 0;
unsigned long lastStateChange = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);
    synth.begin(4, 6, 5, I2S_16BIT);
    
    bakedWavePtr = bakeFloatWavetable(&synth, 0, SINE_LUT_SIZE);
    
    synth.setMasterVolume(128); 
    synth.setEnv(0, 5, 0, 255, 300); 
    synth.setSmoothEnv(0, true);

    synth.setCustomDSP(nativeFloatMasterDistortion);
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
            Serial.println("\n[Module 1] Native Float Baker (Additive Table)");
            synth.setWavetable(0, bakedWavePtr, SINE_LUT_SIZE, BITS_16);
            synth.setWave(0, WAVE_WAVETABLE);
            synth.noteOn(0, c4, 255);
        }
        else if (state == 1) {
            Serial.println("\n[Module 2] Native Float Wavefolder (Buchla Style)");
            synth.setCustomWave(0, nativeFloatWavefolder);
            synth.noteOn(0, c3, 255);
        }
        else if (state == 2) {
            Serial.println("\n[Module 3] Native Float One-Pole LPF");
            synth.setCustomWave(0, nativeFloatOnePoleLPF);
            synth.noteOn(0, c3, 255);
        }
        else if (state == 3) {
            Serial.println("\n[Module 4] Native Float Master Saturation");
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