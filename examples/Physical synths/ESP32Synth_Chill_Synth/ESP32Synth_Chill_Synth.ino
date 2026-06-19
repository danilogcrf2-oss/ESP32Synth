/*
   ESP32Synth - Chill Synth
   Autor: Danilo Gabriel
   
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Encoder.h>
#include <ESP32Synth.h>

// Instâncias
ESP32Synth synth;
ESP32Encoder encoder;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// Pinos Botões
const int btnPins[6] = {5, 27, 14, 13, 18, 19};
bool btnStates[6] = {true, true, true, true, true, true};

// ====================================================================================
// BANCO DE ACORDES E ESCALAS (6 Notas por Botão) - Fica na Flash (Zero RAM gasta)
// ====================================================================================
#define NUM_CHORDS 12

const char* chordNames[NUM_CHORDS] = {
    "Paz Pentatonica",
    "C Major 11",
    "C Minor 11",
    "C Major 9",
    "C Minor 9",
    "C Sus4 Aberto",
    "C Diminuto 7",
    "C Lydian Dream",
    "C Phrygian Dark",
    "C Major 6/9",
    "C Minor 6/9",
    "Quartal Espacial"
};

const uint32_t chordBank[NUM_CHORDS][6] = {
    {c4, d4, e4, g4, a4, c5},         // 0: Paz Pentatonica (C, D, E, G, A, C)
    {c4, e4, g4, b4, d5, f5},         // 1: C Maj 11
    {c4, ds4, g4, as4, d5, f5},       // 2: C Min 11 (C, Eb, G, Bb, D, F)
    {c4, e4, g4, b4, d5, g5},         // 3: C Maj 9
    {c4, ds4, g4, as4, d5, g5},       // 4: C Min 9
    {c4, f4, g4, c5, f5, g5},         // 5: C Sus4 Aberto
    {c4, ds4, fs4, a4, c5, ds5},      // 6: C Dim 7 (C, Eb, Gb, A, C, Eb)
    {c4, e4, g4, b4, d5, fs5},        // 7: C Lydian Dream (C Maj com #4)
    {c4, cs4, f4, g4, as4, c5},       // 8: C Phrygian Dominant (Dark/Egípcio)
    {c4, e4, g4, a4, d5, g5},         // 9: C Maj 6/9
    {c4, ds4, g4, a4, d5, g5},        // 10: C Min 6/9
    {c4, f4, as4, ds5, gs5, cs6}      // 11: Quartal Espacial (Tudo em quartas)
};

// ====================================================================================
// DSP DELAY BUFFER (~300ms a 48kHz = ~57KB). Na RAM Interna (Zero Cache Miss)
// ====================================================================================
#define DELAY_SAMPLES 14400 
int32_t* delayBuffer = nullptr;
int delayIdx = 0;

// ====================================================================================
// PARÂMETROS DO SYNTH
// ====================================================================================
// Índices: 0=Acorde, 1=Cutoff, 2=Delay, 3=Attack, 4=Release
int params[5] = {0, 100, 140, 400, 2000}; 
const char* paramNames[5] = {"Acorde", "Cutoff", "Delay", "Attack(ms)", "Release(ms)"};
int paramMax[5] = {NUM_CHORDS - 1, 255, 255, 2000, 4000};

uint8_t currentParam = 0;
long lastEnc = 0;
bool lastSw = true;
bool needsUIUpdate = true;

// Variáveis espelhadas para o fluxo rápido do DSP
volatile uint8_t synth_cutoff = 100;
volatile uint8_t synth_delay = 140;

// ====================================================================================
// HOOKS DSP & CUSTOM WAVES (Otimização Extrema, sem floats)
// ====================================================================================

// 1. Onda Customizada: Sawtooth com Filtro Passa-Baixa
void IRAM_ATTR customWaveFilteredSaw(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    
    // TRUQUE DE MESTRE: Usa streamFracAccum como memória do filtro. Zero custo de RAM.
    int32_t fState = (int32_t)vo->streamFracAccum; 
    int32_t alpha = synth_cutoff;

    if (envStep == 0) {
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);
        
        if (finalVol == 0) { 
            vo->phase += inc * samples; 
            vo->streamFracAccum = (uint32_t)((fState * 250) >> 8); 
            return; 
        }

        for (int i = 0; i < samples; i++) {
            int16_t rawSaw = (int16_t)(ph >> 16);
            fState += (alpha * (rawSaw - fState)) >> 8; 
            mixBuffer[i] += (fState * finalVol) >> 16;
            ph += inc;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);

            int16_t rawSaw = (int16_t)(ph >> 16);
            fState += (alpha * (rawSaw - fState)) >> 8; 
            mixBuffer[i] += (fState * finalVol) >> 16;
            
            ph += inc; 
            currentEnv += envStep;
        }
    }
    vo->phase = ph;
    vo->streamFracAccum = (uint32_t)fState;
}

// 2. DSP Delay Mestre (Comb Filter)
void IRAM_ATTR dspDelay(int32_t* mixBuffer, int numSamples) {
    if (!delayBuffer || synth_delay == 0) return;
    
    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];
        int32_t wet = delayBuffer[delayIdx];
        
        // Adiciona o delay na mix
        mixBuffer[i] = dry + ((wet * synth_delay) >> 8);
        
        // Realimenta o buffer (aprox 60% de feedback = 150/256 para não clippar infinito)
        delayBuffer[delayIdx] = dry + ((wet * 150) >> 8); 
        
        delayIdx++;
        if (delayIdx >= DELAY_SAMPLES) delayIdx = 0;
    }
}

// ====================================================================================
// DISPLAY UI
// ====================================================================================

void updateUI() {
    display.clearDisplay();
    
    // Header Amarelo
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 4);
    display.print("ESP32Synth ChillSynth");
    display.drawLine(0, 15, 128, 15, SSD1306_WHITE);

    // Renderiza os 5 parâmetros perfeitamente alinhados na altura de 64px
    for (int i = 0; i < 5; i++) {
        int y = 17 + (i * 9);
        
        if (i == currentParam) {
            display.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK); 
        } else {
            display.setTextColor(SSD1306_WHITE);
        }
        
        display.setCursor(2, y);
        
        // Se for o primeiro parâmetro (Acorde), escrevemos o NOME, pros outros o NÚMERO
        if (i == 0) {
            display.print("> ");
            display.print(chordNames[params[0]]);
        } else {
            display.print(paramNames[i]);
            display.setCursor(95, y);
            display.print(params[i]);
        }
    }
    
    display.display();
}

// ====================================================================================
// SETUP E LOOP
// ====================================================================================

void setup() {
    for (int i = 0; i < 6; i++) pinMode(btnPins[i], INPUT_PULLUP);

    Wire.begin();
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    
    // Configuração Encoder
    pinMode(25, INPUT_PULLUP);
    pinMode(33, INPUT_PULLUP);
    pinMode(32, INPUT_PULLUP);
    encoder.attachHalfQuad(33, 25);
    encoder.setCount(params[0] * 2);
    lastEnc = params[0];

    // Alocação bruta na Memória SRAM Interna
    delayBuffer = (int32_t*)heap_caps_calloc(DELAY_SAMPLES, sizeof(int32_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    synth.setMasterVolume(255);
    synth.setCustomDSP(dspDelay);

    #if defined(CONFIG_IDF_TARGET_ESP32S3)
        synth.begin(4, 6, 5, I2S_32BIT);
    #else
        synth.begin(4, 15, 2, I2S_32BIT);
    #endif

    updateUI();
}

void loop() {
    // 1. Processa Encoder (Divisão por 2)
    long encVal = encoder.getCount() / 2;
    if (encVal != lastEnc) {
        params[currentParam] += (encVal - lastEnc);
        
        // Limita o clamp dinâmico
        if (params[currentParam] < 0) params[currentParam] = 0;
        if (params[currentParam] > paramMax[currentParam]) params[currentParam] = paramMax[currentParam];
        
        synth_cutoff = params[1];
        synth_delay = params[2];
        
        lastEnc = params[currentParam];
        encoder.setCount(lastEnc * 2); 
        needsUIUpdate = true;
    }

    // 2. Processa Botão do Encoder
    bool sw = digitalRead(32);
    if (sw == LOW && lastSw == HIGH) {
        currentParam = (currentParam + 1) % 5; // Agora são 5 parâmetros
        lastEnc = params[currentParam];
        encoder.setCount(lastEnc * 2);
        needsUIUpdate = true;
    }
    lastSw = sw;

    // 3. Lê o teclado físico e toca do Acorde selecionado
    uint8_t selectedChord = params[0];

    for (int i = 0; i < 6; i++) {
        bool state = digitalRead(btnPins[i]);
        if (state == LOW && btnStates[i] == true) { 
            synth.setEnv(i, params[3], 0, 255, params[4]);
            synth.setCustomWave(i, customWaveFilteredSaw);
            
            // Pega a nota de dentro do acorde/escala selecionado na matriz!
            uint32_t noteToPlay = chordBank[selectedChord][i];
            
            synth.noteOn(i, noteToPlay, 40); 
        } else if (state == HIGH && btnStates[i] == false) { 
            synth.noteOff(i);
        }
        btnStates[i] = state;
    }

    // 4. Update de UI 
    if (needsUIUpdate) {
        updateUI();
        needsUIUpdate = false;
    }
    
    delay(1); 
}