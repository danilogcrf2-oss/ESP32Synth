#include <ESP32Synth.h>
#include <ESP32Synth_Patches.hpp>

ESP32Synth synth;

const uint32_t GUITAR_CHORDS[4][4] = {
    { e2, b2, e3, g3 },
    { c2, g2, c3, e3 },
    { d2, a2, d3, fs3 },
    { b2, fs3, b3, d4 }
};

uint32_t lastStrumMs = 0;
uint32_t lastModeMs  = 0;
uint32_t lastLogMs   = 0;
uint8_t  chordIdx    = 0;
uint8_t  stepIdx     = 0;
bool     useFlanger  = false;

// Custom stereo warm cabinet 1-pole LPF written directly in the sketch
void stereoCabinetFX(int32_t* busL, int32_t* busR, int samples, int16_t* ep, int32_t* es) {
    int32_t lpL  = es[0];
    int32_t lpR  = es[1];
    int32_t coef = ep[0];
    if (coef < 16) coef = 16;
    if (coef > 256) coef = 256;

    for (int i = 0; i < samples; i++) {
        lpL += ((busL[i] - lpL) * coef) >> 8;
        lpR += ((busR[i] - lpR) * coef) >> 8;
        busL[i] = lpL;
        busR[i] = lpR;
    }
    es[0] = lpL;
    es[1] = lpR;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!synth.begin(4, 6, 5, I2S_32BIT)) {
        Serial.println("Failed to initialize ESP32Synth!");
        while (true) delay(1000);
    }

    synth.setControlRateHz(250);
    synth.setMasterVolume(135);

    // Bus 1 (Voices 0..3): Stereo EKS Guitar into a full 4-slot Pedalboard
    for (uint16_t v = 0; v < 4; v++) {
        synth.setCustomWave(v, ESP32Patches::EKS_Guitar);
        synth.setEnv(v, 0, 250, 220, 320);
        synth.setPan(v, -75 + (v * 50));
        synth.setVoiceBus(v, 1);
    }
    synth.setBusOverdrive(1, 0, 140, 155, 165, FX_OD_TUBE, 255);
    synth.setBusPhaser(1, 1, 55, 185, 120, 4, 150, 90);
    synth.setBusFX(1, 2, FX_CUSTOM, stereoCabinetFX);
    synth.setBusFXParam(1, 2, 0, 120);
    synth.setBusDelay(1, 3, 270, 125, 110, true, 120);
    synth.setBusMix(1, 220);

    // Bus 2 (Voices 4..5): Lo-Fi Stereo Backing Synth (Bitcrusher + LFO Filter)
    synth.setWave(4, WAVE_SAW);
    synth.setWave(5, WAVE_PULSE);
    synth.setPulseWidth(5, 80);
    synth.setEnv(4, 10, 140, 160, 200);
    synth.setEnv(5, 10, 140, 160, 200);
    synth.setPan(4, -70);
    synth.setPan(5, 70);
    synth.setVoiceBus(4, 2);
    synth.setVoiceBus(5, 2);
    synth.setBusBitcrush(2, 0, 7, 4, 195);
    synth.setBusFilter(2, 1, 1400, 130, FX_FILTER_LPF, 255);
    synth.setBusMix(2, 165);

    synth.setLFO(0, LFO_TRIANGLE, 65);
    synth.assignLFOToBusFX(0, 2, 1, FX_FILTER_CUTOFF, 450, 2600);

    // Bus 0 (Master): Stereo Hall Reverb + Safety Limiter
    synth.setBusReverb(0, 0, 175, 105, 255, 80);
    synth.setBusCompressor(0, 1, 155, 200, 0, 70, 0, 255);

    uint32_t now = millis();
    lastStrumMs = now;
    lastModeMs  = now;
    lastLogMs   = now;
}

void loop() {
    uint32_t now = millis();

    if (now - lastStrumMs >= 300) {
        lastStrumMs = now;

        uint8_t strIdx = stepIdx & 3;
        synth.noteOn(strIdx, GUITAR_CHORDS[chordIdx][strIdx], 130);

        if (strIdx == 0) {
            synth.noteOn(4, GUITAR_CHORDS[chordIdx][0], 95);
            synth.noteOn(5, GUITAR_CHORDS[chordIdx][2], 95);
        }

        stepIdx++;
        if ((stepIdx & 7) == 0) {
            chordIdx = (chordIdx + 1) & 3;
        }
    }

    // Live swap Slot 1 on Bus 1 between Phaser and Flanger every 6 seconds
    if (now - lastModeMs >= 6000) {
        lastModeMs = now;
        useFlanger = !useFlanger;
        if (useFlanger) {
            synth.setBusFlanger(1, 1, 45, 190, 15, 155, 145, 120);
            Serial.println(">>> Pedal Slot 1 switched to FX_FLANGER");
        } else {
            synth.setBusPhaser(1, 1, 55, 185, 120, 4, 150, 90);
            Serial.println(">>> Pedal Slot 1 switched to FX_PHASER");
        }
    }

    if (now - lastLogMs >= 1000) {
        lastLogMs = now;
        Serial.printf("Guitar Rig (%s) | CPU: %5.2f%% | FreeHeap: %lu\n",
                      useFlanger ? "Flanger" : "Phaser",
                      synth.getCPULoad(),
                      (unsigned long)ESP.getFreeHeap());
    }

    delay(2);
}