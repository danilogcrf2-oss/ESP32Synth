#include <Arduino.h>
#include <ESP32Synth.h>

ESP32Synth synth;

#define WT_SIZE 512

// ==============================================================================
// ONDAS BASE EM FLOAT
// ==============================================================================
float baseSaw[WT_SIZE];
float basePulse[WT_SIZE];

// ==============================================================================
// DOUBLE BUFFERING PARA WAVETABLES
// ==============================================================================
int16_t wtFilteredLP[2][WT_SIZE];
int16_t wtFilteredBP[2][WT_SIZE];
int16_t wtFilteredHP[2][WT_SIZE];
volatile uint8_t activeBuf[3] = {0, 0, 0};

volatile uint32_t noteOnTime[3] = {0, 0, 0};
volatile bool     noteActive[3] = {false, false, false};

// ==============================================================================
// MÁQUINA DE ESTADOS (Para organizar a demonstração antes da música)
// ==============================================================================
enum AppState {
    DEMO_LOW_PASS,
    DEMO_BAND_PASS,
    DEMO_HIGH_PASS,
    PLAY_MUSIC
};
AppState currentState = DEMO_LOW_PASS;
bool stateTriggered = false;
uint32_t stateTimer = 0;

// ==============================================================================
// GERADOR DE ONDAS BASE
// ==============================================================================
void generateBaseWaves() {
    for (int i = 0; i < WT_SIZE; i++) {
        baseSaw[i] = (i / (float)WT_SIZE) * 2.0f - 1.0f;
        basePulse[i] = (i < WT_SIZE / 2) ? 1.0f : -1.0f;
    }
}

// ==============================================================================
// FILTRO SVF MUSICAL (OTIMIZADO PARA NÃO ESTOURAR)
// ==============================================================================
void applyFilter(const float* input, int16_t* output, int mode, float cutoff, float resonance) {
    if (cutoff < 0.002f) cutoff = 0.002f; // Deixa passar graves profundos
    if (cutoff > 0.45f)  cutoff = 0.45f;
    
    float f = 2.0f * sin(PI * cutoff / 2.0f);
    float q = 1.0f - resonance;
    float lp = 0, bp = 0, hp = 0;

    // Pass 1: Estabiliza o filtro (tira o estalo de início)
    for (int i = 0; i < WT_SIZE; i++) {
        hp = input[i] - lp - q * bp;
        bp += f * hp;
        lp += f * bp;
    }

    float outBuf[WT_SIZE];
    float maxPeak = 0.001f; 

    // Pass 2: Calcula pra valer
    for (int i = 0; i < WT_SIZE; i++) {
        hp = input[i] - lp - q * bp;
        bp += f * hp;
        lp += f * bp;

        float out = (mode == 0) ? lp : (mode == 1) ? bp : hp;
        outBuf[i] = out;

        float absOut = abs(out);
        if (absOut > maxPeak) maxPeak = absOut;
    }

    // Auto-gain para ressonância não clipar o áudio
    float gain = (maxPeak > 1.0f) ? (0.95f / maxPeak) : 1.0f;

    for (int i = 0; i < WT_SIZE; i++) {
        int32_t sample = (int32_t)(outBuf[i] * gain * 32767.0f);
        if (sample > 32767) sample = 32767;
        else if (sample < -32768) sample = -32768;
        output[i] = (int16_t)sample;
    }
}

// ==============================================================================
// TASK DOS FILTROS (Processamento assíncrono seguro)
// ==============================================================================
void filterSweepTask(void* pvParameters) {
    while (true) {
        uint32_t t = millis();

        for (int v = 0; v < 3; v++) {
            if (!noteActive[v]) continue;

            uint32_t elapsed = t - noteOnTime[v];
            float cutoff = 0.1f;
            float res = 0.0f;
            uint8_t nextBuf = activeBuf[v] ^ 1;

            // VOZ 0: LOW PASS (Sawtooth) -> Fecha rápido + Oscila LFO
            if (v == 0) {
                res = 0.65f; 
                if (elapsed < 400) {
                    float env = 1.0f - (elapsed / 400.0f);
                    cutoff = 0.015f + (env * 0.20f); 
                } else {
                    float lfo = (sin((elapsed - 400) * 0.003f) + 1.0f) * 0.5f;
                    cutoff = 0.015f + (lfo * 0.03f); 
                }
                applyFilter(baseSaw, wtFilteredLP[nextBuf], 0, cutoff, res);
                synth.setWavetable(0, wtFilteredLP[nextBuf], WT_SIZE, BITS_16);
            }
            
            // VOZ 1: BAND PASS (Pulse) -> Varredura suave até virar um sino oco
            else if (v == 1) {
                res = 0.35f; 
                if (elapsed < 200) { 
                    float env = 1.0f - (elapsed / 200.0f);
                    cutoff = 0.01f + (env * 0.12f); 
                } else {
                    cutoff = 0.01f;
                }
                applyFilter(basePulse, wtFilteredBP[nextBuf], 1, cutoff, res);
                synth.setWavetable(1, wtFilteredBP[nextBuf], WT_SIZE, BITS_16);
            }
            
            // VOZ 2: HIGH PASS (Sawtooth) -> Sweep super lento, respira os graves
            else if (v == 2) {
                res = 0.15f; 
                float sweepLfo = (sin(elapsed * 0.001f) + 1.0f) * 0.5f; 
                cutoff = 0.001f + (sweepLfo * 0.05f); 
                
                applyFilter(baseSaw, wtFilteredHP[nextBuf], 2, cutoff, res);
                synth.setWavetable(2, wtFilteredHP[nextBuf], WT_SIZE, BITS_16);
            }

            activeBuf[v] = nextBuf; 
        }

        vTaskDelay(pdMS_TO_TICKS(15)); // Deixa o ESP32 respirar
    }
}

// ==============================================================================
// SETUP 
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- INICIANDO ESP32 SYNTH: DEMO DE FILTROS ---");

    generateBaseWaves(); 

    if (!synth.begin(4, 15, 2, I2S_32BIT)) {
        Serial.println("Erro ao inicializar o ESP32Synth!");
        while(1);
    }
    
    synth.setMasterVolume(220);

    // VOZ 0 (BASS / LP)
    synth.setWave(0, WAVE_WAVETABLE); 
    synth.setVolume(0, 255);
    synth.setEnv(0, 5, 250, 150, 400); 

    // VOZ 1 (ARP / BP)
    synth.setWave(1, WAVE_WAVETABLE); 
    synth.setVolume(1, 160); 
    synth.setEnv(1, 15, 300, 50, 300); 

    // VOZ 2 (PAD / HP)
    synth.setWave(2, WAVE_WAVETABLE); 
    synth.setVolume(2, 140);
    synth.setEnv(2, 800, 1000, 200, 2500); 

    xTaskCreatePinnedToCore(filterSweepTask, "FilterTask", 8192, NULL, 1, NULL, 0);
}

// ==============================================================================
// VARIÁVEIS DO SEQUENCIADOR
// ==============================================================================
#define TEMPO_MS 125 
uint32_t lastTick = 0;
uint8_t currentStep = 0;

const int seqBass[32] = {
    c2, 0, 0, c2,  0, 0, c2, 0,  ds2, 0, 0, ds2,  0, 0, as1, 0,
    c2, 0, 0, c2,  0, 0, c2, 0,  g1,  0, 0, g1,   0, 0, as1, 0
};

const int seqArp[32] = {
    c3, g3, ds3, g3, c4, g3, ds3, g3,  c3, g3, ds3, g3, c4, g3, ds3, g3,
    c3, g3, d3,  g3, c4, g3, d3,  g3,  c3, f3, c3,  f3, d3, f3, as3, f3
};

const int seqPad[32] = {
    c4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    as3,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  
};

void playTrack(uint8_t voice, int note, bool legato) {
    if (note > 0) {
        noteOnTime[voice] = millis(); 
        noteActive[voice] = true;
        synth.noteOn(voice, note, synth.getVolumeRaw(voice)); 
    }
}

// ==============================================================================
// LOOP PRINCIPAL (Controla os estágios da Demonstração e a Música)
// ==============================================================================
void loop() {
    uint32_t t = millis();

    switch (currentState) {
        
        case DEMO_LOW_PASS:
            if (!stateTriggered) {
                Serial.println(">[1] DEMO: Low Pass (Sawtooth) - Pluck Rápido + LFO");
                noteOnTime[0] = t; noteActive[0] = true;
                synth.noteOn(0, c2, 255); // Toca um C2 Grave
                stateTriggered = true;
                stateTimer = t;
            }
            // Segura a nota por 3 segundos, desliga e espera +1s de cauda (release)
            if (t - stateTimer > 3000) synth.noteOff(0);
            if (t - stateTimer > 4000) {
                noteActive[0] = false;
                currentState = DEMO_BAND_PASS;
                stateTriggered = false;
            }
            break;

        case DEMO_BAND_PASS:
            if (!stateTriggered) {
                Serial.println("> [2] DEMO: Band Pass (Pulse) - Varredura media fechando nos médios");
                noteOnTime[1] = t; noteActive[1] = true;
                synth.noteOn(1, c3, 200); // Toca um C3
                stateTriggered = true;
                stateTimer = t;
            }
            if (t - stateTimer > 2500) synth.noteOff(1);
            if (t - stateTimer > 3500) {
                noteActive[1] = false;
                currentState = DEMO_HIGH_PASS;
                stateTriggered = false;
            }
            break;

        case DEMO_HIGH_PASS:
            if (!stateTriggered) {
                Serial.println("> [3] DEMO: High Pass (Sawtooth) - Subida lenta LFO (6 segundos)");
                noteOnTime[2] = t; noteActive[2] = true;
                synth.noteOn(2, c3, 180); // Toca um C3 (tem harmônicos suficientes pra varrer)
                stateTriggered = true;
                stateTimer = t;
            }
            // Esse filtro é demorado, vamos escutar ele abrir por 6 segundos!
            if (t - stateTimer > 6000) synth.noteOff(2);
            if (t - stateTimer > 8000) {
                noteActive[2] = false;
                currentState = PLAY_MUSIC;
                stateTriggered = false;
            }
            break;

        case PLAY_MUSIC:
            if (!stateTriggered) {
                Serial.println("\n>>> INICIANDO A MÚSICA COM OS FILTROS SINCRONIZADOS! <<<");
                lastTick = t; // Reseta o relógio do sequenciador
                currentStep = 0;
                stateTriggered = true;
            }
            
            // Sequenciador de 120BPM (Roda infinitamente)
            if (t - lastTick >= TEMPO_MS) {
                lastTick = t;

                playTrack(0, seqBass[currentStep], false);
                playTrack(1, seqArp[currentStep], false);
                playTrack(2, seqPad[currentStep], true); 

                currentStep++;
                if (currentStep >= 32) currentStep = 0;
            }
            break;
    }
    
    delay(1); 
}