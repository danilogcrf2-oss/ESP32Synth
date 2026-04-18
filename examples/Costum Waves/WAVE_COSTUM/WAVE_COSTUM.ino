#include <Arduino.h>
#include "ESP32Synth.h"

ESP32Synth synth;

// Pinos I2S Padrão (Altere se necessário)
#define PIN_BCK  26
#define PIN_WS   25
#define PIN_DATA 22

// ==============================================================================
// 1. ENGINE DO REVERB
// ==============================================================================
#define TAPE_LEN 20000 
int32_t* reverbTape = nullptr;
int writeHead = 0;

// Estados dos filtros
int32_t lpState = 0;          // Low-Pass 
int32_t dcBlockerState = 0;   // High-Pass 
int32_t dcBlockerPrevWet = 0;

void IRAM_ATTR reverbDSP(int32_t* mixBuffer, int numSamples) {
    if (!reverbTape) return; 

    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];

        // 1. Leituras Seguras do Buffer Circular (Sem usar o operador '%')
        // Escolhemos tempos primos menores que TAPE_LEN (20000)
        int tap1 = writeHead - 4327;  if (tap1 < 0) tap1 += TAPE_LEN;
        int tap2 = writeHead - 11003; if (tap2 < 0) tap2 += TAPE_LEN;
        int tap3 = writeHead - 19013; if (tap3 < 0) tap3 += TAPE_LEN;

        // Soma as 3 cabeças e divide por 4 (>> 2) para não clipar
        int32_t wet = (reverbTape[tap1] >> 2) + (reverbTape[tap2] >> 2) + (reverbTape[tap3] >> 2);

        // 2. DC Blocker (Filtro Passa-Alta Crucial!)
        // Isso mata qualquer energia parada que causaria o loop infinito de ruído.
        int32_t dcBlocked = wet - dcBlockerPrevWet + ((dcBlockerState * 253) >> 8);
        dcBlockerPrevWet = wet;
        dcBlockerState = dcBlocked;

        // 3. Filtro Low-Pass (Deixa as repetições cada vez mais abafadas)
        lpState = ((dcBlocked * 50) + (lpState * 206)) >> 8; 

        // 4. Calcula o Feedback para gravar na fita (~78% de feedback)
        int32_t feedback = (dry >> 1) + ((lpState * 200) >> 8);

        // 5. SATURAÇÃO ANALÓGICA (Soft-Clipping de segurança)
        // Se a matemática tentar explodir, esmagamos o som no limite do 16-bit.
        if (feedback > 32767) feedback = 32767;
        else if (feedback < -32768) feedback = -32768;

        // Grava na fita e avança
        reverbTape[writeHead] = feedback;
        writeHead++;
        if (writeHead >= TAPE_LEN) writeHead = 0;

        // Mixagem Master
        mixBuffer[i] = dry + lpState;
    }
}

// ==============================================================================
// 2. AS 3 ONDAS CUSTOMIZADAS (WAVE_CUSTOM)
// ==============================================================================

// ONDA 1: FOLDED SINE (Agora com Drive agressivo)
void IRAM_ATTR waveFoldedSine(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;

    for (int i = 0; i < samples; i++) {
        int32_t rawSine = sineLUT[ph >> SINE_SHIFT]; 
        int32_t s = rawSine >> 15; // Deixa o volume base mais alto (~32000)

        // DRIVE! Multiplica por 4 antes de bater no teto.
        s = s * 4; 
        
        // WAVEFOLDING REAL: Bateu no teto? Rebate pra baixo. Passou do fundo? Rebate pra cima.
        int32_t limit = 20000;
        while(s > limit || s < -limit) {
            if (s > limit) s = (limit << 1) - s;
            else if (s < -limit) s = -(limit << 1) - s;
        }

        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        mixBuffer[i] += (s * finalVol) >> 16;
        ph += inc;
        currentEnv += envStep;
    }
    vo->phase = ph;
}

// ONDA 2: STEPPED TRIANGLE (Bitcrush Manual - Pitch perfeito, degraus agressivos, ZERO chiado!)
void IRAM_ATTR waveSteppedTri(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;

    for (int i = 0; i < samples; i++) {
        // 1. Gera o triângulo matematicamente perfeito (Para não perder a afinação e não chiar)
        int16_t saw = (int16_t)(ph >> 16);
        int32_t s = (int16_t)(((saw ^ (saw >> 15)) * 2) - 32767); 

        // 2. A MÁGICA DO CHIPTUNE: Bitcrush de Amplitude (4-bits)
        // O ">> 12" joga fora as informações suaves da onda, e o "<< 12" devolve pro volume normal.
        // O resultado são degraus duros e puramente retrô!
        s = (s >> 12) << 12; 

        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        mixBuffer[i] += (s * finalVol) >> 16;
        ph += inc;
        currentEnv += envStep;
    }
    vo->phase = ph;
}

// ONDA 3: FM FEEDBACK SINE (Metálico, musical, sem virar ruído branco)
void IRAM_ATTR waveFMSine(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    
    int16_t prevOut = vo->noiseSample; 

    for (int i = 0; i < samples; i++) {
        // Reduzimos o shift para 15! 
        // Agora o feedback torce a onda em ~12% (Ponto de ressonância musical perfeito)
        uint32_t modPh = ph + ((int32_t)prevOut << 15); 
        
        int32_t s = sineLUT[(modPh >> SINE_SHIFT) & SINE_LUT_MASK] >> 16;
        prevOut = (int16_t)s;

        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        mixBuffer[i] += (s * finalVol) >> 16;
        ph += inc;
        currentEnv += envStep;
    }
    vo->phase = ph;
    vo->noiseSample = prevOut; 
}

// ==============================================================================
// 3. SETUP & LOOP
// ==============================================================================

void setup() {
    Serial.begin(115200);

    // 1. Aloca os 80KB de RAM para a fita do Reverb de forma segura no Heap
    reverbTape = (int32_t*)malloc(TAPE_LEN * sizeof(int32_t));
    if (reverbTape != nullptr) {
        memset(reverbTape, 0, TAPE_LEN * sizeof(int32_t));
        Serial.println("Reverb Tape Alocada com Sucesso!");
    } else {
        Serial.println("ERRO FATAL: Sem RAM para o Reverb!");
        while(1);
    }

    // 2. Inicia o Synth
    if (!synth.begin(4,15,2,I2S_32BIT)) {
        Serial.println("Falha ao iniciar i2s!");
        while(1);
    }

    // 3. Atrela o Reverb ao Final da Mixagem
    synth.setCustomDSP(reverbDSP);

    // 4. Configura as 3 Vozes para usarem nossas funções CUSTOM
    synth.setCustomWave(0, waveFoldedSine);
    synth.setCustomWave(1, waveSteppedTri);
    synth.setCustomWave(2, waveFMSine);

    // 5. Configura Envelopes para soarem bem (A, D, S, R)
    synth.setEnv(0, 10,  300, 100, 1500); // Pluck suave
    synth.setEnv(1, 0,   0, 255,   200);  // Arp curto (chiptune)
    synth.setEnv(2, 500, 500, 200, 3000); // Pad sombrio e longo

    // Volume Master 
    synth.setMasterVolume(255); // Abaixei um pouco pois essas ondas são ricas
}

void loop() {
    // Voz 0 (Folded Sine) - Baixo profundo
    synth.noteOn(0, c2, 200);
    delay(2000);
    synth.noteOff(0);
    delay(4000);

    // Voz 1 (Stepped Tri) - Arpejo agudo chiptune
    synth.setArpeggio(1, 25, c4, ds4, g4, c5);
    synth.noteOn(1, c4, 100); 
    delay(2000);
    synth.noteOff(1);
    delay(4000);

    // Voz 2 (FM Feedback) - Pad Metálico rasgando o fundo
    synth.noteOn(2, c3, 180);
    delay(2000);
    synth.noteOff(2);
    delay(4000);

    // Voz 0 (Folded Sine) - Baixo profundo
    synth.noteOn(0, c2, 200);
    
    synth.setArpeggio(1, 25, c4, ds4, g4, c5);
    synth.noteOn(1, c4, 100);  
    
    synth.noteOn(2, c3, 180);

    delay(2000);

    synth.noteOff(0);
    synth.noteOff(1);
    synth.noteOff(2);

    delay(4000); // Tempo para curtir a cauda infinita do  reverb
}