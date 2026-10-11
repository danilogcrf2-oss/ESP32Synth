#include <Arduino.h>
#include <ESP32Synth.h>
#include <ESP32SynthNotes.h>
#include <ESP32Synth_Patches.hpp>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define I2S_PIN_BCK   4
    #define I2S_PIN_WS    6
    #define I2S_PIN_DATA  5
#else
    #define I2S_PIN_BCK   4
    #define I2S_PIN_WS    15
    #define I2S_PIN_DATA  2
#endif

ESP32Synth synth;

// Melody sequence (C minor blues / funk riff)
const uint32_t riffNotes[] = { c3, ds3, f3, fs3, g3, as3, c4 };
const int riffLen = sizeof(riffNotes) / sizeof(riffNotes[0]);

void setup() {
    Serial.begin(115200);

    synth.begin(I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DATA, I2S_32BIT);
    synth.setMasterVolume(210);

    // Setup Dual-Oscillator Mono Lead (Voice 0 & Voice 1 detuned)
    for (int v = 0; v < 2; v++) {
        // High-fidelity PolyBLEP oscillator (zero aliasing)
        synth.setCustomWave(v, ESP32Patches::AA_Saw);
        synth.setEnv(v, 15, 60, 200, 120);
        synth.setVoiceBus(v, 1); // Route voices into FX Bus 1
    }

    // Attach Resonant Low-Pass Filter to Bus 1, Slot 0
    synth.setBusFX(1, 0, FX_CUSTOM, ESP32Patches::FX_BiquadFilter);
    synth.setBusFXParam(1, 0, 0, 1600); // ep[0] = Cutoff: 1600 Hz
    synth.setBusFXParam(1, 0, 1, 0);    // ep[1] = Mode: 0 (LPF)
    synth.setBusFXParam(1, 0, 2, 280);  // ep[2] = Q: 2.8 (Warm analog resonance)
    synth.setBusFXParam(1, 0, 3, 0);    // ep[3] = Unit: Q Mode
    synth.setBusFXParam(1, 0, 4, 255);  // ep[4] = Mix: 100% Wet
    synth.setBusMix(1, 255);            // Full level mix to Master

    Serial.println("ESP32Synth: Moog Lead Patch Ready.");
}

void loop() {
    for (int i = 0; i < riffLen; i++) {
        uint32_t baseFreq = riffNotes[i];

        // Trigger detuned oscillators for that fat analog sound
        synth.noteOn(0, baseFreq, 90);
        synth.noteOn(1, baseFreq + 25, 90); // +25 centiHz (+0.25 Hz detune)

        // Dynamic Filter Sweep: Opens the filter on higher notes
        int16_t dynamicCutoff = 800 + (i * 350);
        synth.setBusFXParam(1, 0, 0, dynamicCutoff);

        delay(180);

        synth.noteOff(0);
        synth.noteOff(1);
        delay(40);
    }
    delay(400);
}