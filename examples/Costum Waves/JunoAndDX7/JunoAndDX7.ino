// ESP32Synth - Custom Waves Showcase
// 8 Motores de Síntese O(1) customizados usando o buffer dedicado cw[6].
// Hardware: ESP32 ou ESP32-S3, PCM5102A (I2S_32BIT)

#include <Arduino.h>
#include "ESP32Synth.h"

ESP32Synth synth;

// ====================================================================================
// 1. Roland Juno (Main Sawtooth + Sub-Oscillator Square -1 Oitava)
// ====================================================================================
void IRAM_ATTR renderJuno(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv; int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase; uint32_t inc = vo->phaseInc;
    uint32_t subPh = vo->cw[0]; uint32_t subInc = inc >> 1; // Usando cw[0] para a Sub-Fase

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc*samples; vo->cw[0] += subInc*samples; return; }

        for (int i = 0; i < samples; i++) {
            int16_t saw = (int16_t)(ph >> 16);                 
            int16_t sub = (subPh >> 31) ? 32767 : -32767;      
            mixBuffer[i] += (((saw >> 1) + (sub >> 1)) * finalVol) >> 16;
            ph += inc; subPh += subInc;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            int16_t saw = (int16_t)(ph >> 16);
            int16_t sub = (subPh >> 31) ? 32767 : -32767;
            mixBuffer[i] += (((saw >> 1) + (sub >> 1)) * finalVol) >> 16;
            ph += inc; subPh += subInc; currentEnv += envStep;
        }
    }
    vo->phase = ph; vo->cw[0] = subPh;
}

// ====================================================================================
// 2. Yamaha DX7 E-Piano (FM Dinâmico 2-Op)
// ====================================================================================
void IRAM_ATTR renderDX7(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv; int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase; uint32_t inc = vo->phaseInc;
    uint32_t mPh = vo->cw[0]; uint32_t mInc = inc * 2; // cw[0] é o Modulator

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc*samples; vo->cw[0] += mInc*samples; return; }

        for (int i = 0; i < samples; i++) {
            int32_t modOut = sineLUT[(mPh >> SINE_SHIFT) & 4095]; 
            uint32_t fmPhase = ph + (modOut * (finalVol >> 2));
            mixBuffer[i] += (sineLUT[(fmPhase >> SINE_SHIFT) & 4095] * finalVol) >> 16;
            ph += inc; mPh += mInc;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            int32_t modOut = sineLUT[(mPh >> SINE_SHIFT) & 4095];
            uint32_t fmPhase = ph + (modOut * (finalVol >> 2));
            mixBuffer[i] += (sineLUT[(fmPhase >> SINE_SHIFT) & 4095] * finalVol) >> 16;
            ph += inc; mPh += mInc; currentEnv += envStep;
        }
    }
    vo->phase = ph; vo->cw[0] = mPh;
}

// ====================================================================================
// 3. Yamaha DX7 Tubular Bells (FM Inarmônico Ratio 3.5 com Exponential Decay)
// ====================================================================================
void IRAM_ATTR renderFMBell(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv; int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase; uint32_t inc = vo->phaseInc;
    uint32_t mPh = vo->cw[0]; uint32_t mInc = (inc * 7) >> 1; // cw[0] para Inharmonic

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc*samples; vo->cw[0] += mInc*samples; return; }

        uint32_t snapEnv = ((uint32_t)finalVol * (uint32_t)finalVol) >> 16; 
        for (int i = 0; i < samples; i++) {
            int32_t modOut = sineLUT[(mPh >> SINE_SHIFT) & 4095]; 
            uint32_t fmPhase = ph + (modOut * (snapEnv >> 2));
            mixBuffer[i] += (sineLUT[(fmPhase >> SINE_SHIFT) & 4095] * finalVol) >> 16;
            ph += inc; mPh += mInc;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            uint32_t snapEnv = ((uint32_t)finalVol * (uint32_t)finalVol) >> 16;
            
            int32_t modOut = sineLUT[(mPh >> SINE_SHIFT) & 4095];
            uint32_t fmPhase = ph + (modOut * (snapEnv >> 2));
            mixBuffer[i] += (sineLUT[(fmPhase >> SINE_SHIFT) & 4095] * finalVol) >> 16;
            ph += inc; mPh += mInc; currentEnv += envStep;
        }
    }
    vo->phase = ph; vo->cw[0] = mPh;
}

// ====================================================================================
// 4. String Ensemble (Supersaw 3x Detune Livre de Conflitos)
// ====================================================================================
void IRAM_ATTR renderStrings(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv; int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph1 = vo->phase; uint32_t inc1 = vo->phaseInc;
    uint32_t ph2 = vo->cw[0]; uint32_t inc2 = inc1 + (inc1 >> 6); // cw[0] para a Saw 2
    uint32_t ph3 = vo->cw[1]; uint32_t inc3 = inc1 - (inc1 >> 7); // cw[1] para a Saw 3! LFO liberado!

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc1*samples; vo->cw[0] += inc2*samples; vo->cw[1] += inc3*samples; return; }

        for (int i = 0; i < samples; i++) {
            int16_t mixed = ((int16_t)(ph1 >> 16) >> 2) + ((int16_t)(ph2 >> 16) >> 2) + ((int16_t)(ph3 >> 16) >> 2);
            mixBuffer[i] += (mixed * finalVol) >> 16;
            ph1 += inc1; ph2 += inc2; ph3 += inc3;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            int16_t mixed = ((int16_t)(ph1 >> 16) >> 2) + ((int16_t)(ph2 >> 16) >> 2) + ((int16_t)(ph3 >> 16) >> 2);
            mixBuffer[i] += (mixed * finalVol) >> 16;
            ph1 += inc1; ph2 += inc2; ph3 += inc3; currentEnv += envStep;
        }
    }
    vo->phase = ph1; vo->cw[0] = ph2; vo->cw[1] = ph3;
}

// ====================================================================================
// 5. Prophet-5 Hard Sync Lead
// ====================================================================================
void IRAM_ATTR renderHardSync(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv; int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase; uint32_t inc = vo->phaseInc; 
    uint32_t sPh = vo->cw[0]; // cw[0] para a Escrava

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase+=inc*samples; return; }

        uint32_t sInc = inc + ((inc * 2 * (uint32_t)finalVol) >> 15); 

        for (int i = 0; i < samples; i++) {
            uint32_t nextPh = ph + inc;
            sPh += sInc;
            if (nextPh < ph) sPh = 0; 

            mixBuffer[i] += ((int16_t)(sPh >> 16) * finalVol) >> 16;
            ph = nextPh;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);

            uint32_t sInc = inc + ((inc * 2 * (uint32_t)finalVol) >> 15); 
            
            uint32_t nextPh = ph + inc;
            sPh += sInc;
            if (nextPh < ph) sPh = 0; 
            
            mixBuffer[i] += ((int16_t)(sPh >> 16) * finalVol) >> 16;
            ph = nextPh; currentEnv += envStep;
        }
    }
    vo->phase = ph; vo->cw[0] = sPh;
}

// ====================================================================================
// 6. Cybernetic Bass 
// ====================================================================================
void IRAM_ATTR renderCyberBass(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv; int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase; uint32_t inc = vo->phaseInc;
    uint32_t mPh = vo->cw[0]; uint32_t mInc = inc >> 1; 

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        if (finalVol == 0) { vo->phase += inc*samples; vo->cw[0] += mInc*samples; return; }

        uint32_t snapEnv = ((uint32_t)finalVol * (uint32_t)finalVol) >> 16;
        for (int i = 0; i < samples; i++) {
            int32_t modOut = sineLUT[(mPh >> SINE_SHIFT) & 4095]; 
            uint32_t fmPh = ph + (modOut * (snapEnv >> 3));
            int32_t fmOut = sineLUT[(fmPh >> SINE_SHIFT) & 4095];
            int16_t subOut = (mPh >> 31) ? 32767 : -32767;
            int16_t mixed = (fmOut >> 1) + (fmOut >> 2) + (subOut >> 2);
            mixBuffer[i] += (mixed * finalVol) >> 16;
            ph += inc; mPh += mInc;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14; envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
            uint32_t snapEnv = ((uint32_t)finalVol * (uint32_t)finalVol) >> 16;
            int32_t modOut = sineLUT[(mPh >> SINE_SHIFT) & 4095]; 
            uint32_t fmPh = ph + (modOut * (snapEnv >> 3));
            int32_t fmOut = sineLUT[(fmPh >> SINE_SHIFT) & 4095];
            int16_t subOut = (mPh >> 31) ? 32767 : -32767;
            int16_t mixed = (fmOut >> 1) + (fmOut >> 2) + (subOut >> 2);
            mixBuffer[i] += (mixed * finalVol) >> 16;
            ph += inc; mPh += mInc; currentEnv += envStep;
        }
    }
    vo->phase = ph; vo->cw[0] = mPh;
}

// ====================================================================================
// SETUP E EXECUÇÃO
// ====================================================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    #if defined(CONFIG_IDF_TARGET_ESP32S3)
        bool started = synth.begin(4, 6, 5, I2S_32BIT);
    #else
        bool started = synth.begin(4, 15, 2, I2S_32BIT);
    #endif

    if (started) Serial.println("ESP32Synth: Ultimate Engine 100% ONLINE.");
    
    synth.setMasterVolume(110);
}

void playChord(uint32_t n1, uint32_t n2, uint32_t n3, SynthCustomWaveCallback waveCB, uint16_t a, uint16_t d, uint8_t s, uint16_t r) {
    for(int i = 0; i < 3; i++) {
        synth.setCustomWave(i, waveCB);
        synth.setEnv(i, a, d, s, r);        
    }
    synth.noteOn(0, n1, 150); synth.noteOn(1, n2, 150); synth.noteOn(2, n3, 150);
    delay(1800); 
    synth.noteOff(0); synth.noteOff(1); synth.noteOff(2);
    delay(r + 200); 
}

void loop() {
    Serial.println(">>> 1. Roland Juno (Saw + Sub)");
    playChord(c4, e4, g4, renderJuno, 10, 500, 150, 400);
    delay(500);

    Serial.println(">>> 2. Yamaha DX7 E-Piano (FM Pluck)");
    playChord(a3, c4, e4, renderDX7, 5, 800, 20, 600);
    delay(500);

    Serial.println(">>> 3. Yamaha DX7 Tubular Bells (Inharmonic FM)");
    playChord(c5, fs5, a5, renderFMBell, 5, 1200, 0, 1000);
    delay(500);

    Serial.println(">>> 4. String Ensemble (Supersaw)");
    playChord(f3, a3, c4, renderStrings, 500, 0, 255, 800);
    delay(500);
    
    Serial.println(">>> 5. Prophet-5 Hard Sync Lead (Dynamic Envelope Sweep)");
    playChord(d3, d4, a4, renderHardSync, 100, 800, 40, 500);
    delay(500);

    Serial.println(">>> 6. Cybernetic Bass (Modern Serum Bass)");
    playChord(c2, c3, g3, renderCyberBass, 1, 1000, 0, 400);
    delay(1000);
}