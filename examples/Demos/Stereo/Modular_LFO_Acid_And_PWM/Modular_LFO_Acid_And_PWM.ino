#include <ESP32Synth.h>
#include <ESP32Synth_Patches.hpp>

ESP32Synth synth;

static const uint32_t STEP_US = 125000UL; // 120 BPM sixteenth-note (125 ms)

const uint32_t PAD_CHORDS[4][3] = {
    { a2, c3, e3 },
    { f2, a2, c3 },
    { g2, b2, d3 },
    { e2, g2, b2 }
};

const uint32_t BASS_ROOTS[4] = { a1, f1, g1, e1 };

const uint8_t BASS_PATTERN[16] = {
    1, 0, 2, 0, 1, 0, 2, 1, 0, 1, 2, 0, 1, 0, 2, 0 // 0=rest, 1=root, 2=octave
};

const uint32_t LEAD_PHRASE[4][3] = {
    { e4, a4, g4 },
    { c4, f4, e4 },
    { d4, g4, f4 },
    { b3, e4, d4 }
};

uint32_t nextStepUs = 0;
uint32_t lastLogMs  = 0;
uint32_t stepCount  = 0;

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!synth.begin(4, 6, 5, I2S_32BIT)) {
        Serial.println("Failed to initialize ESP32Synth!");
        while (true) delay(1000);
    }

    synth.setMasterVolume(210);

    // Bus 1: PWM Stereo Pad (Voices 0..2) -> LPF + Chorus
    for (uint16_t v = 0; v < 3; v++) {
        synth.setCustomWave(v, ESP32Patches::AA_Pulse);
        synth.setEnv(v, 140, 350, 195, 450);
        synth.setVoiceBus(v, 1);
    }
    synth.setPan(0, -80);
    synth.setPan(1, 0);
    synth.setPan(2, 80);
    synth.setBusFilter(1, 0, 1750, 90, FX_FILTER_LPF, 255);
    synth.setBusChorus(1, 1, 50, 95, 12, 15, 95, 180);
    synth.setBusMix(1, 160);

    // Bus 2: Resonant Synth Bass (Voice 3) -> Resonant LPF -> Warm Tube Saturation -> 375 ms Delay
    synth.setCustomWave(3, ESP32Patches::AA_Saw);
    synth.setEnv(3, 3, 150, 85, 90);
    synth.setPan(3, 0);
    synth.setVoiceBus(3, 2);
    synth.setBusFilter(2, 0, 900, 220, FX_FILTER_LPF, 255);
    synth.setBusOverdrive(2, 1, 28, 180, 210, FX_OD_TUBE, 150);
    synth.setBusDelay(2, 2, 375, 85, 145, true, 75);
    synth.setBusMix(2, 175);

    // Bus 3: Expressive Lead (Voice 4) -> LPF + 250 ms 8th-Note Ping-Pong Delay
    synth.setWave(4, WAVE_TRIANGLE);
    synth.setEnv(4, 18, 240, 175, 240);
    synth.setPan(4, 35);
    synth.setVoiceBus(4, 3);
    synth.setBusFilter(3, 0, 2800, 80, FX_FILTER_LPF, 255);
    synth.setBusDelay(3, 1, 250, 90, 135, true, 85);
    synth.setBusMix(3, 175);

    // Bus 0: Clean Hall Space
    synth.setBusReverb(0, 0, 145, 145, 220, 48);

    // LFO 0: Triangle PWM across Voices 0..2 (0.50 Hz = 1 bar)
    synth.setLFO(0, LFO_TRIANGLE, 50);
    synth.assignLFOToVoiceRange(0, LFO_TARGET_PULSE_WIDTH, 0, 2, 48, 208);

    // LFO 1: Slow Bus 1 stereo pan drift (0.25 Hz = 2 bars)
    synth.setLFO(1, LFO_SINE, 25);
    synth.routeLFOToBus(1, LFO_TARGET_BUS_PAN, 1, -45, 45, true);

    // LFO 2 + LFO 3 stacked on Bus 2 Filter Cutoff (Range + Additive Smooth Random)
    synth.setLFO(2, LFO_SINE, 50);
    synth.assignLFOToBusFX(2, 2, 0, FX_FILTER_CUTOFF, 360, 2300);

    synth.setLFO(3, LFO_SMOOTH_RANDOM, 200);
    synth.routeLFOToBusFX(3, 2, 0, FX_FILTER_CUTOFF, 260, 0, false);

    // LFO 4: Delayed vibrato on Lead Voice 4 (+-20 cents) cross-modulated by LFO 5
    synth.setLFO(4, LFO_SINE, 520, 0, 180, 280, false);
    synth.assignLFOToVoice(4, LFO_TARGET_VOICE_PITCH, 4, 500);

    synth.setLFO(5, LFO_TRIANGLE, 25);
    synth.routeLFOToLFO(5, LFO_TARGET_LFO_AMP, 4, 130);

    nextStepUs = micros();
    lastLogMs  = millis();
}

void loop() {
    uint32_t nowUs = micros();

    if ((int32_t)(nowUs - nextStepUs) >= 0) {
        nextStepUs += STEP_US;
        if ((int32_t)(nowUs - nextStepUs) > (int32_t)(STEP_US * 4)) {
            nextStepUs = nowUs + STEP_US;
        }

        uint8_t step16 = stepCount & 15;
        uint8_t bar    = (stepCount >> 4) & 3;

        if (step16 == 0) {
            for (uint8_t i = 0; i < 3; i++) {
                synth.noteOn(i, PAD_CHORDS[bar][i], 50);
            }
        }

        uint8_t bType = BASS_PATTERN[step16];
        if (bType == 1) {
            synth.noteOn(3, BASS_ROOTS[bar], 95);
        } else if (bType == 2) {
            synth.noteOn(3, BASS_ROOTS[bar] * 2, 85);
        } else if (step16 == 15) {
            synth.noteOff(3);
        }

        if (step16 == 0) {
            synth.noteOn(4, LEAD_PHRASE[bar][0], 85);
            synth.retriggerLFO(4);
        } else if (step16 == 6) {
            synth.noteOn(4, LEAD_PHRASE[bar][1], 80);
            synth.retriggerLFO(4);
        } else if (step16 == 10) {
            synth.noteOn(4, LEAD_PHRASE[bar][2], 85);
            synth.retriggerLFO(4);
        } else if (step16 == 14) {
            synth.noteOff(4);
        }

        stepCount++;
    }

    uint32_t nowMs = millis();
    if (nowMs - lastLogMs >= 1000) {
        lastLogMs = nowMs;
        Serial.printf("Modular LFO Matrix | Cutoff: %d Hz | CPU: %5.2f%%\n",
                      synth.getLFOValueScaled(2, 360, 2300),
                      synth.getCPULoad());
    }

    delay(1);
}