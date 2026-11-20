#include <Arduino.h>
#include <ESP32Synth.h>
#include <ESP32SynthNotes.h>

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

const uint32_t progression[4][4] = {
    { c4, ds4, g4, as4 }, // Cm7
    { gs3, c4, ds4, g4 }, // Abmaj7
    { as3, d4, f4, as4 }, // Bb
    { g3, as3, d4, f4 }   // Gm7
};

void setup() {
    Serial.begin(115200);

    // Initialize I2S in 32-bit mode
    if (!synth.begin(I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DATA, I2S_32BIT)) {
        Serial.println("Failed to start synth engine!");
        while (1) { delay(1000); }
    }

    // Conservative master level to guarantee clean sum of 4 voices
    synth.setMasterVolume(190);

    // Setup warm polyphonic pad voices
    for (int v = 0; v < 4; v++) {
        synth.setWave(v, WAVE_SAW);
        synth.setEnv(v, 60, 100, 180, 250); // A=60ms, D=100ms, S=180/255, R=250ms
    }

    Serial.println("ESP32Synth: Polyphonic Engine Started.");
}

void loop() {
    for (int chord = 0; chord < 4; chord++) {
        // Trigger 4-voice chord (Voice volume ~60 leaves safe dynamic range)
        for (int v = 0; v < 4; v++) {
            synth.noteOn(v, progression[chord][v], 60);
        }

        delay(1200);

        // Release voices for natural ADSR fade
        for (int v = 0; v < 4; v++) {
            synth.noteOff(v);
        }

        delay(300);
    }
}