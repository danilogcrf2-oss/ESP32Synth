#include <ESP32Synth.h>
#include <ESP32Synth_Patches.hpp>

ESP32Synth synth;

static const uint32_t STEP_US = 125000UL; // 120 BPM sixteenth-note (125 ms)

const uint32_t PAD_CHORDS[4][3] = {
    { c3, ds3, g3 },
    { f2, gs2, c3 },
    { gs2, c3, ds3 },
    { g2, as2, d3 }
};

const uint32_t BASS_ROOTS[4] = { c2, f1, gs1, g1 };

const uint8_t BASS_GATE[16] = {
    1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0
};

const uint8_t ARP_GATE[16] = {
    1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0
};

const uint32_t ARP_NOTES[8] = { c4, ds4, g4, as4, c5, as4, g4, ds4 };
const uint32_t SHIMMER_NOTES[4] = { g5, c6, ds5, d5 };

uint32_t nextStepUs = 0;
uint32_t lastLogMs  = 0;
uint32_t stepCount  = 0;
uint8_t  arpStep    = 0;

void setup() {
    Serial.begin(115200);
    delay(200);

    if (!synth.begin(4, 6, 5, I2S_32BIT)) {
        Serial.println("Failed to initialize ESP32Synth!");
        while (true) delay(1000);
    }

    synth.setMasterVolume(210);
    synth.setPanLaw(PAN_LAW_CONSTANT_POWER);

    // Bus 1: Pluck arp (Voice 0) + Auto-Pan shimmer (Voice 5) -> Warm LPF + 375 ms Ping-Pong Delay
    synth.setCustomWave(0, ESP32Patches::AA_Saw);
    synth.setEnv(0, 2, 160, 0, 80);
    synth.setVoiceBus(0, 1);

    synth.setWave(5, WAVE_SINE);
    synth.setEnv(5, 8, 260, 0, 180);
    synth.setAutoPan(5, 100, 95); // 1.00 Hz stereo swing (2 beats at 120 BPM)
    synth.setVoiceBus(5, 1);

    synth.setBusFilter(1, 0, 2600, 110, FX_FILTER_LPF, 255);
    synth.setBusDelay(1, 1, 375, 100, 135, true, 90); // Dotted-8th (3 * 125 ms)
    synth.setBusMix(1, 185);

    // Bus 2: Wide stereo pad (Voices 1..3) -> Warm LPF + Stereo Chorus + Width
    for (uint16_t v = 1; v <= 3; v++) {
        synth.setCustomWave(v, ESP32Patches::AA_Pulse);
        synth.setPulseWidth(v, 96);
        synth.setEnv(v, 220, 400, 200, 500);
        synth.setVoiceBus(v, 2);
    }
    synth.setPan(1, -85);
    synth.setPan(2, 0);
    synth.setPan(3, 85);
    synth.setBusFilter(2, 0, 1450, 90, FX_FILTER_LPF, 255);
    synth.setBusChorus(2, 1, 55, 100, 12, 15, 105, 180);
    synth.setBusWidth(2, 300);
    synth.setBusMix(2, 165);

    // Bus 3: Tight center bass (Voice 4) -> Warm LPF + Smooth Compression
    synth.setCustomWave(4, ESP32Patches::AA_Saw);
    synth.setEnv(4, 3, 180, 140, 90);
    synth.setPan(4, 0);
    synth.setVoiceBus(4, 3);
    synth.setBusFilter(3, 0, 620, 130, FX_FILTER_LPF, 255);
    synth.setBusCompressor(3, 1, 130, 3, 10, 90, 12, 255);
    synth.setBusMix(3, 200);

    // Bus 0: Master Reverb + Gentle Glue Compressor
    synth.setBusReverb(0, 0, 155, 140, 230, 58);
    synth.setBusCompressor(0, 1, 185, 3, 12, 120, 0, 255);

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
                synth.noteOn(1 + i, PAD_CHORDS[bar][i], 55);
            }
        }

        if (BASS_GATE[step16]) {
            uint32_t note = (step16 == 14) ? (BASS_ROOTS[bar] * 2) : BASS_ROOTS[bar];
            synth.noteOn(4, note, 95);
        } else if (step16 == 12) {
            synth.noteOff(4);
        }

        if (ARP_GATE[step16]) {
            int32_t targetPan = (arpStep & 1) ? 90 : -90;
            synth.noteOn(0, ARP_NOTES[arpStep & 7], 105);
            synth.slidePanTo(0, targetPan, 110); // Always call slidePanTo after noteOn
            arpStep++;
        }

        if (step16 == 4 || step16 == 12) {
            synth.noteOn(5, SHIMMER_NOTES[bar], 60);
        }

        stepCount++;
    }

    uint32_t nowMs = millis();
    if (nowMs - lastLogMs >= 1000) {
        lastLogMs = nowMs;
        Serial.printf("Stereo FX Stage | CPU: %5.2f%% | FreeHeap: %lu\n",
                      synth.getCPULoad(), (unsigned long)ESP.getFreeHeap());
    }

    delay(1);
}