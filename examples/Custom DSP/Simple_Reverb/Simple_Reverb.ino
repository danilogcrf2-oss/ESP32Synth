#include <ESP32Synth.h>
#include <esp_heap_caps.h>

ESP32Synth synth;

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
// 2. O MAESTRO PROCEDURAL
// ==============================================================================
uint32_t fm9Chord[] = {f3, gs3, c4, ds4, g4}; 
uint32_t ticks = 0;

void IRAM_ATTR theMaestroControl() {
    ticks++;

    // LFO Respiratório nos Pads
    uint8_t breathPWM = 128 + (sin(ticks * 0.005) * 110);
    synth.setPulseWidth(1, breathPWM);
    synth.setPulseWidth(2, breathPWM);
    synth.setPulseWidth(3, breathPWM);

    // Gotas agudas arpejadas
    if (ticks % 12 == 0) {
        if (random(0, 3) == 0) { 
            int note = random(0, 5);
            synth.noteOn(0, fm9Chord[note] * 2, random(50, 150)); 
        }
    }

    // Acordes e Sub-Grave Monstruoso
    if (ticks % 400 == 0) {
        synth.noteOn(4, f1, 55); 
        synth.noteOn(5, f2, 40); 
        synth.noteOn(6, f1, 55); 
        synth.noteOn(7, f2, 40); 

        synth.noteOn(1, fm9Chord[random(0, 2)], 30);
        synth.noteOn(2, fm9Chord[random(2, 4)], 30);
        synth.noteOn(3, fm9Chord[random(3, 5)], 30);
    }
}

// ==============================================================================
// SETUP MAIN
// ==============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n--- ESP32Synth: BLADE RUNNER ---");

    // Tenta alocar os 80 KB
    reverbTape = (int32_t*)heap_caps_calloc(TAPE_LEN, sizeof(int32_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    
    if (reverbTape) {
        Serial.printf("Buffer Reverb Alocado com Sucesso: %d KB\n", (TAPE_LEN * 4) / 1024);
    } else {
        Serial.println("ALERTA: Faltou RAM para o Reverb!");
    }

    // Configura os timbres
    synth.setWave(0, WAVE_TRIANGLE);
    synth.setEnv(0, 10, 300, 0, 0); 

    for (int i = 1; i <= 3; i++) {
        synth.setWave(i, WAVE_PULSE);
        synth.setEnv(i, 2000, 1000, 180, 4000);
    }

    synth.setWave(4, WAVE_SAW);
    synth.setEnv(4, 1500, 2000, 200, 3000);
    synth.setWave(5, WAVE_SAW);
    synth.setEnv(5, 1000, 2000, 180, 3000);
    synth.setWave(6, WAVE_SINE);
    synth.setEnv(4, 1500, 2000, 200, 3000);
    synth.setWave(7, WAVE_SINE);
    synth.setEnv(5, 1000, 2000, 180, 3000);
    // Injeta os Hooks
    synth.setCustomDSP(reverbDSP);
    synth.setCustomControl(theMaestroControl);

    // Inicia Engine - AJUSTE SEUS PINOS AQUI: (BCLK, WS, DATA)
    if (synth.begin(4, 15, 2, I2S_32BIT)) {
        Serial.println("Sintetizador Operacional! Coloque os fones de ouvido.");
    } else {
        Serial.println("Erro ao ligar a I2S.");
    }

}

void loop() {
    // Nada aqui para demonstrar que tudo pode funcionar no CostumControl
    delay(100);
}