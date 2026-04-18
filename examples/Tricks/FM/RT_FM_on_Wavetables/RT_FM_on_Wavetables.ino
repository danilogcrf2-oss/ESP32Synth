#include <Arduino.h>
#include <ESP32Synth.h>
#include <math.h>

ESP32Synth synth;


int16_t* wt_pad[16];
int16_t* wt_pluck[16];
int16_t* wt_bass[16];

// Acordes da música relaxante
const uint32_t chords[4][4] = {
    {c3, ds3, g3, c4},    // Cm
    {gs2, c3, ds3, g3},   // Ab
    {ds3, g3, as3, ds4},  // Eb
    {as2, d3, f3, as3}    // Bb
};

// ====================================================================================
// == INSTRUMENTOS E ENVELOPES
// ====================================================================================

// --- PAD (IDs 0 a 15) ---
const int16_t padWaves[16]   = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
const uint8_t padVols[16]    = {60, 90, 120, 150, 130, 110, 90, 80, 70, 60, 55, 50, 45, 40, 35, 30};
const int16_t padRelWaves[4] = {15, 15, 15, 15}; 
const uint8_t relVols[4]     = {20, 10, 5, 0};

Instrument padInst = {
    .seqVolumes = padVols, .seqWaves = padWaves, .relVolumes = relVols, .relWaves = padRelWaves,
    .seqSpeedMs = 150, .susWave = 15, .relSpeedMs = 300, .seqLen = 16, .susVol = 25, .relLen = 4,
    .smoothMorph = true, .smoothVolume = true
};

// --- PLUCK / ARPEJO (IDs 16 a 31) ---
const int16_t pluckWaves[8]   = {16, 18, 20, 22, 24, 26, 28, 31};
const uint8_t pluckVols[8]    = {150, 90, 50, 25, 10, 5, 0, 0};
const int16_t pluckRelWaves[1]= {31};

Instrument pluckInst = {
    .seqVolumes = pluckVols, .seqWaves = pluckWaves, .relVolumes = relVols, .relWaves = pluckRelWaves,
    .seqSpeedMs = 60, .susWave = 31, .relSpeedMs = 50, .seqLen = 8, .susVol = 0, .relLen = 1,
    .smoothMorph = true, .smoothVolume = true
};

// --- BASS / BAIXO (IDs 32 a 47) ---
const int16_t bassWaves[16]   = {32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};
const uint8_t bassVols[16]    = {200, 190, 180, 160, 140, 120, 100, 90, 80, 70, 60, 50, 40, 30, 20, 10};
const int16_t bassRelWaves[4] = {47, 47, 47, 47};

Instrument bassInst = {
    .seqVolumes = bassVols, .seqWaves = bassWaves, .relVolumes = relVols, .relWaves = bassRelWaves,
    .seqSpeedMs = 120, .susWave = 47, .relSpeedMs = 200, .seqLen = 16, .susVol = 10, .relLen = 4,
    .smoothMorph = true, .smoothVolume = true
};

// ====================================================================================
// == MOTOR FM OTIMIZADO
// ====================================================================================
void generateFMBank(int16_t* dest[16], float ratioC, float ratioM, float maxDepth, float amplitude) {
    for (int w = 0; w < 16; w++) {
        float depth = maxDepth * (1.0f - ((float)w / 15.0f)); 
        for (int i = 0; i < 512; i++) {
            float phaseC = ((float)i / 512.0f) * 2.0f * PI * ratioC;
            float phaseM = ((float)i / 512.0f) * 2.0f * PI * ratioM;
            // sinf() força o uso da FPU do ESP32 para cálculos absurdamente rápidos
            dest[w][i] = (int16_t)(sinf(phaseC + depth * sinf(phaseM)) * amplitude);
        }
    }
}

// ====================================================================================
// == TASK BACKGROUND (CORE 0) - MÚSICA E MATEMÁTICA LFO
// ====================================================================================
void fmMusicTask(void* param) {
    unsigned long lastSequencerTick = 0;
    unsigned long lastTimbreTick = 0;
    int tickCount = 0;
    int chordIdx = 0;

    while(true) {
        unsigned long currentMs = millis();

        if (currentMs - lastTimbreTick >= 10) {
            lastTimbreTick = currentMs;
            float t = currentMs * 0.001f; 

            // Pad flutuante (Chorus Efeito)
            float padDepth = 1.5f + 1.0f * sinf(t * 0.5f);
            float padModRatio = 2.0f + 0.02f * cosf(t * 0.3f); 
            generateFMBank(wt_pad, 1.0f, padModRatio, padDepth, 12000.0f);

            // Pluck Metálico
            float pluckDepth = 3.0f + 2.0f * sinf(t * 0.8f);
            generateFMBank(wt_pluck, 1.0f, 3.5f, pluckDepth, 14000.0f);

            // Bass Wobble
            float bassDepth = 1.0f + 2.5f * sinf(t * 1.5f);
            generateFMBank(wt_bass, 1.0f, 0.5f, bassDepth, 16000.0f);
        }

        if (currentMs - lastSequencerTick >= 500) {
            lastSequencerTick = currentMs;

            if (tickCount % 8 == 0) {
                for(int i = 0; i <= 4; i++) synth.noteOff(i); 

                for(int i = 0; i < 4; i++) synth.noteOn(i, chords[chordIdx][i], 200);
                synth.noteOn(4, chords[chordIdx][0] / 4, 255); // Bass
                
                chordIdx = (chordIdx + 1) % 4;
            }

            int arpNoteIdx = tickCount % 4;
            int pluckVoice = 5 + arpNoteIdx; 
            uint32_t currentChordBase = chords[(chordIdx == 0 ? 3 : chordIdx - 1)][arpNoteIdx];
            synth.noteOn(pluckVoice, currentChordBase * 4, 180);

            tickCount++;
        }

        // Devolve o controle pro FreeRTOS evitar pânico
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ====================================================================================
// == SETUP E LOOP PRINCIPAL
// ====================================================================================
void setup() {
    Serial.begin(115200);

    // 1. Aloca os 48KB de Wavetables DIRETAMENTE no Heap livre
    for(int i = 0; i < 16; i++) {
        wt_pad[i]   = (int16_t*)malloc(512 * sizeof(int16_t));
        wt_pluck[i] = (int16_t*)malloc(512 * sizeof(int16_t));
        wt_bass[i]  = (int16_t*)malloc(512 * sizeof(int16_t));
    }

    // 2. Gera a base inicial
    generateFMBank(wt_pad,   1.0f, 2.0f, 2.0f, 12000.0f);
    generateFMBank(wt_pluck, 1.0f, 3.5f, 4.0f, 14000.0f);
    generateFMBank(wt_bass,  1.0f, 0.5f, 3.0f, 16000.0f);

    // 3. Registra os ponteiros no ESP32Synth
    for (int i = 0; i < 16; i++) {
        synth.registerWavetable(i,      wt_pad[i],   512, BITS_16);
        synth.registerWavetable(i + 16, wt_pluck[i], 512, BITS_16);
        synth.registerWavetable(i + 32, wt_bass[i],  512, BITS_16);
    }

    // 4. Inicializa o ESP32Synth
    if (!synth.begin(4, 15, 2, I2S_32BIT)) {
        Serial.println("Erro ao iniciar ESP32Synth!");
        while (true);
    }
    
    synth.setMasterVolume(110);

    // 5. Configura as vozes
    for (int i = 0; i < 4; i++) synth.setInstrument(i, &padInst);
    synth.setInstrument(4, &bassInst);
    for (int i = 5; i < 9; i++) synth.setInstrument(i, &pluckInst);

    // 6. Joga a música e os cálculos matemáticos FM pro CORE 0
    // (O ESP32Synth já fica blindado rodando no CORE 1)
    xTaskCreatePinnedToCore(
        fmMusicTask,     // Função da task
        "FMMusicTask",   // Nome
        4096,            // Stack (4KB é bastante)
        NULL,            // Sem parâmetros
        1,               // Prioridade normal
        NULL,            // Sem Handle
        0                // Core 0
    );

    Serial.println("Síntese Multi-FM Otimizada rodando no Background!");
}

void loop() {
    // O seu loop agora está 100% livre. 
    // Coloque seu código de Display LVGL, Wifi, ou o que quiser aqui!
    
    vTaskDelay(pdMS_TO_TICKS(1000));
}