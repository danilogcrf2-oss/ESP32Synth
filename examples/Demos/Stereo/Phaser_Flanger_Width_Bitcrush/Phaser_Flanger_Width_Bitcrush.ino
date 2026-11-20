#include <ESP32Synth.h>
#include <ESP32Synth_Patches.hpp>

ESP32Synth synth;

static const uint32_t STEP_US = 125000UL; // 120 BPM sixteenth-note (125 ms)

const uint32_t PAD_CHORDS[4][3] = {
    { d3, f3, a3 },
    { as2, d3, f3 },
    { c3, e3, g3 },
    { a2, c3, e3 }
};

const uint32_t BASS_ROOTS[4]  = { d2, as1, c2, a1 };
const uint32_t ARP_NOTES[8]   = { d4, f4, a4, c5, d5, c5, a4, f4 };
const uint32_t CHIME_NOTES[4] = { a5, d5, g5, e5 };

const uint8_t ARP_GATE[16] = {
    1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 0
};

volatile int16_t liveSquareLfo = 0;

void onLfoTelemetry(uint8_t lfoId, int16_t value) {
    if (lfoId == 0) {
        liveSquareLfo = value;
    }
}

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
    synth.setMasterStereoWidth(280); // Subtle extra master stereo width

    // Bus 1: Stereo Phaser Pad (Voices 0..2)
    for (uint16_t v = 0; v < 3; v++) {
        synth.setCustomWave(v, ESP32Patches::AA_Pulse);
        synth.setEnv(v, 180, 350, 190, 450);
        synth.setVoiceBus(v, 1);
    }
    synth.setPan(0, -85);
    synth.setPan(1, 0);
    synth.setPan(2, 85);
    synth.setBusFilter(1, 0, 1900, 80, FX_FILTER_LPF, 255);
    synth.setBusPhaser(1, 1, 40, 165, 80, 4, 135, 110);
    synth.setBusMix(1, 165);

    // Bus 2: Flanger Synth Arp (Voice 3) -> 250 ms 8th-Note Ping-Pong Delay
    synth.setCustomWave(3, ESP32Patches::AA_Saw);
    synth.setEnv(3, 2, 150, 0, 80);
    synth.setPan(3, 0);
    synth.setVoiceBus(3, 2);
    synth.setBusFilter(2, 0, 2800, 100, FX_FILTER_LPF, 255);
    synth.setBusFlanger(2, 1, 30, 150, 15, 90, 110, 90);
    synth.setBusDelay(2, 2, 250, 85, 140, true, 75);
    synth.setBusMix(2, 175);

    // Bus 3: Clean 10-bit Retro Chime (Voice 4) -> Bitcrusher -> S&H LPF
    synth.setWave(4, WAVE_SINE);
    synth.setEnv(4, 2, 140, 0, 80);
    synth.setAutoPan(4, 150, 85);
    synth.setVoiceBus(4, 3);
    synth.setBusBitcrush(3, 0, 10, 3, 110);
    synth.setBusFilter(3, 1, 2400, 110, FX_FILTER_LPF, 255);
    synth.setBusMix(3, 150);

    // Voice 5: Clean Center Sub-Bass directly to Bus 0
    synth.setWave(5, WAVE_TRIANGLE);
    synth.setEnv(5, 3, 180, 150, 90);
    synth.setPan(5, 0);
    synth.setVoiceBus(5, 0);

    // Bus 0: Master Reverb + Safety Glue Compressor
    synth.setBusReverb(0, 0, 150, 145, 225, 52);
    synth.setBusCompressor(0, 1, 185, 3, 12, 120, 0, 255);

    // LFO 0: Synced 8th-note Square PWM on Pad + Safe Telemetry Callback
    synth.setLFO(0, LFO_SQUARE, 200);
    synth.setLFODuty(0, 96);
    synth.assignLFOToVoiceRange(0, LFO_TARGET_PULSE_WIDTH, 0, 2, 56, 192);
    synth.setLFOCallback(0, onLfoTelemetry);

    // LFO 1: Synced 8th-note Sample & Hold stepping Bus 3 Filter Cutoff
    synth.setLFO(1, LFO_SAMPLE_HOLD, 400);
    synth.assignLFOToBusFX(1, 3, 1, FX_FILTER_CUTOFF, 1400, 4200);

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
                synth.noteOn(i, PAD_CHORDS[bar][i], 52);
            }
        }

        if (step16 == 0 || step16 == 6 || step16 == 10) {
            synth.noteOn(5, BASS_ROOTS[bar], 95);
        } else if (step16 == 14) {
            synth.noteOff(5);
        }

        if (ARP_GATE[step16]) {
            synth.noteOn(3, ARP_NOTES[arpStep & 7], 85);
            arpStep++;
        }

        if (step16 == 4 || step16 == 12) {
            synth.noteOn(4, CHIME_NOTES[bar], 75);
        }

        stepCount++;
    }

    uint32_t nowMs = millis();
    if (nowMs - lastLogMs >= 1000) {
        lastLogMs = nowMs;
        Serial.printf("Phaser/Flanger/Crush | SqLFO: %6d | S&H: %4ld Hz | CPU: %5.2f%%\n",
                      (int)liveSquareLfo,
                      (long)synth.getLFOValueScaled(1, 1400, 4200),
                      synth.getCPULoad());
    }

    delay(1);
}