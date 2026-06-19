#include <ESP32Synth.h>

ESP32Synth synth;

// ==============================================================================
// 1. ENGINE "BBD CHORUS SUAVE"
// ==============================================================================
#define CHORUS_LEN 1024
#define CHORUS_MASK 1023

int32_t chorusTape[CHORUS_LEN] = {0};
int cHead = 0;
uint32_t lfoPhase = 0;

void IRAM_ATTR chorusDSP(int32_t* mixBuffer, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];
        
        // 1. LFO TRIANGULAR LENTO (~1.5Hz)
        lfoPhase += 2;
        int32_t lfoTri = (lfoPhase & 0x7FFF);
        if (lfoPhase & 0x8000) lfoTri = 32767 - lfoTri;
        
        // 2. ATRASO SUAVE (Entre 50 e 200 samples = ~1ms a ~4ms)
        int delayOffset = 50 + ((lfoTri * 150) >> 15);
        
        int readHead = (cHead - delayOffset) & CHORUS_MASK;
        int32_t wet = chorusTape[readHead];
        
        chorusTape[cHead] = dry;
        cHead = (cHead + 1) & CHORUS_MASK;
        
        // Mixa 50% / 50% sem estourar o volume
        mixBuffer[i] = (dry >> 1) + (wet >> 1);
    }
}

// ==============================================================================
// 2. GERADOR DE ACORDES
// ==============================================================================
uint32_t padChords[4][4] = {
    {c3,  e3,  g3,  b3},  // Cmaj7 
    {a2,  c3,  e3,  g3},  // Am7
    {f2,  a2,  c3,  e3},  // Fmaj7
    {g2,  b2,  d3,  f3}   // G7
};

uint32_t tick = 0;
uint8_t currentChord = 0;

void IRAM_ATTR dreamControl() {
    tick++;

    // LFO bem lento para dar movimento ao Pulse Width (PWM)
    uint8_t pwmWidth = 128 + ((tick % 200) > 100 ? (tick % 100) : (100 - (tick % 100)));
    for(int i = 0; i < 4; i++) synth.setPulseWidth(i, pwmWidth);

    // Muda de acorde a cada 300 ticks (3 segundos)
    if (tick % 300 == 0) {
        currentChord = (currentChord + 1) & 3; // Cicla entre 0, 1, 2, 3 ordenadamente
        
        // Dispara os acordes limpos (sem deslizar pitch para não criar dissonância)
        for(int i = 0; i < 4; i++) {
            synth.noteOn(i, padChords[currentChord][i], 60);
        }
    }
}

// ==============================================================================
// SETUP MAIN
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    for (int i = 0; i < 4; i++) {
        synth.setWave(i, (i % 2 == 0) ? WAVE_PULSE : WAVE_TRIANGLE);
        // Attack lento e Release looongo criam o "Pad" atmosférico
        synth.setEnv(i, 2000, 1000, 255, 3000); 
    }

    synth.setCustomDSP(chorusDSP);
    synth.setCustomControl(dreamControl);

    // Ajuste seus pinos aqui! (BCLK, WS, DATA)
    if (synth.begin(4, 15, 2, I2S_32BIT)) {
        Serial.println("Dream Pads (Chorus Corrigido) Operantes.");
    }
}

void loop() {
    delay(1000);
}