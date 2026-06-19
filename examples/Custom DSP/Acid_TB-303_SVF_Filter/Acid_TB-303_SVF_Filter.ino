#include <ESP32Synth.h>

ESP32Synth synth;

// ==============================================================================
// ENGINE "ACID VCF" (Filtro SVF Chamberlin Otimizado em Ponto Fixo)
// ==============================================================================
int32_t bpState = 0; // Band-pass state
int32_t lpState = 0; // Low-pass state
int32_t vcfCutoff = 0; // Dinâmico: 10 a 120 (Seguro para 48kHz)
const int32_t vcfRes = 35; // Ressonância: Quanto MENOR, mais ele "grita" (0 a 255)

void IRAM_ATTR acidDSP(int32_t* mixBuffer, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        int32_t input = mixBuffer[i] >> 2; // Reduz ganho inicial para dar espaço ao filtro

        // FILTRO SVF (2-Pole Resonant) com bit-shifts
        int32_t hp = input - lpState - ((bpState * vcfRes) >> 8);
        bpState += (vcfCutoff * hp) >> 8;
        lpState += (vcfCutoff * bpState) >> 8;

        // Limita a ressonância para não explodir a matemática (estabilidade)
        if(bpState > 32767) bpState = 32767; else if(bpState < -32768) bpState = -32768;
        if(lpState > 32767) lpState = 32767; else if(lpState < -32768) lpState = -32768;

        int32_t output = lpState; 
        mixBuffer[i] = output;
    }
}

// ==============================================================================
// 2. O SEQUENCIADOR ACID
// ==============================================================================
uint32_t acidSeq[16]  = { c2, c3, 0, ds2, c2, f2, 0, c2, c2, gs2, 0, as2, c2, c3, ds3, 0 };
uint8_t  acidAcc[16]  = { 1,  0,  0, 1,   0,  1,  0, 0,  1,  1,   0, 0,   1,  0,  1,   0 };

uint32_t tick = 0;
uint8_t  step = 0;

void IRAM_ATTR acidControl() {
    tick++;

    // Envelope do Filtro (Decay agressivo)
    if (vcfCutoff > 15) vcfCutoff -= 6;

    if (tick % 12 == 0) {
        uint32_t note = acidSeq[step];
        if (note > 0) {
            // Se tiver Accent, o envelope abre ao máximo.
            if (acidAcc[step]) {
                vcfCutoff = 110; 
                synth.noteOn(0, note, 255);
            } else {
                vcfCutoff = 60;
                synth.noteOn(0, note, 150);
            }
        }
        step = (step + 1) & 15; // & 15 substitui % 16 (muito mais rápido)
    }
}

// ==============================================================================
// SETUP MAIN
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    synth.setWave(0, WAVE_SAW);
    synth.setEnv(0, 5, 200, 0, 50); // Pluck rápido

    synth.setCustomDSP(acidDSP);
    synth.setCustomControl(acidControl);

    // Ajuste seus pinos aqui! (BCLK, WS, DATA)
    if (synth.begin(25)) {
        Serial.println("Acid TB-303 (Filtro SVF Corrigido) Rodando.");
    }
}

void loop() {
    delay(1000);
}