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

void setup() {
    Serial.begin(115200);

    synth.begin(I2S_PIN_BCK, I2S_PIN_WS, I2S_PIN_DATA, I2S_32BIT);
    synth.setMasterVolume(200);

    // Voice 0: Square Lead with internal hardware arpeggiator
    synth.setWave(0, WAVE_PULSE);
    synth.setPulseWidth(0, 128); // 50% square wave (0-255 scale)
    synth.setEnv(0, 10, 40, 200, 80);
    synth.setArpeggio(0, 110, c4, ds4, g4, as4, c5, as4, g4, ds4); // 8-step pattern at 110ms

    // Voice 1: Deep Triangle Bass
    synth.setWave(1, WAVE_TRIANGLE);
    synth.setSmoothEnv(1, true); // Keep envelope level across pitch glides
    synth.setEnv(1, 15, 80, 220, 100);

    // Trigger ongoing Arp note
    synth.noteOn(0, c4, 70);

    Serial.println("ESP32Synth: Arp + Portamento Bass Active.");
}

void loop() {
    // Dynamic bassline with portamento slides
    synth.noteOn(1, c2, 110);
    delay(440);

    synth.slideFreqTo(1, ds2, 120); // Glide to Eb2 in 120ms
    delay(440);

    synth.slideFreqTo(1, f2, 120); // Glide to F2
    delay(440);

    synth.slideFreqTo(1, as1, 150); // Drop to Bb1
    delay(440);

    synth.slideFreqTo(1, c2, 90);
    delay(440);

    synth.noteOff(1);
    delay(200);

    Serial.printf("DSP Load: %.2f%% | Voices: Active\n", synth.getCPULoad());
}