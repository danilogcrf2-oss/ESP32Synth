/**
 * @brief Sintetizador Aditivo com Log Detalhado por Voz.
 * Mostra a matemática da Série de Fourier em tempo real.
 * Hardware: ESP32-S3 Zero (Data/Audio: 5)
 */

#include "ESP32Synth.h"

ESP32Synth synth;

// --- PARÂMETROS ---
#define BASE_FREQ 110       // 110 Hz (Lá A2)
#define MAX_VOICES_USE 32   // Máximo de vozes (harmônicos)
#define STEP_DELAY 200      // Tempo entre cada adição (ms)
#define HOLD_TIME 3000      // Tempo segurando a onda completa

// Pinos
#define I2S_DATA 5


// Controle
unsigned long lastTime = 0;
int currentVoice = 0;
bool building = true;
bool holding = false;

void setup() {
    Serial.begin(115200);
    delay(2000); 

    Serial.printf("CPU: %s | SR: %d Hz\n", synth.getChipModel(), synth.getSampleRate());
    Serial.println("Formula da Onda Quadrada: Volume = 1 / Harmônico");
    Serial.println("------------------------------------------------");

    if (!synth.begin(5)) {
        Serial.println("FALHA CRITICA NO I2S"); while(1);
    }

    // Configuração Inicial das Vozes
    for(int i=0; i<32; i++) {
        synth.setWave(i, WAVE_SINE);
        // Envelope sem Attack, Sustain total (como um órgão)
        synth.setEnv(i, 0, 0, 255, 50); 
    }
}

void loop() {
    unsigned long now = millis();

    // Se estamos em espera, não faz nada, só checa o tempo
    if (holding) {
        if (now - lastTime > HOLD_TIME) {
            holding = false;
            lastTime = now;
            
            if (building) {
                // Terminou de construir, hora de esperar e depois destruir
                Serial.println("\n>>> ESTADO: ONDA QUADRADA COMPLETA (Segurando...) <<<\n");
                building = false; // Inverte direção
                // Truque: Não resetamos currentVoice aqui, mantemos em 32
            } else {
                // Terminou de destruir, hora de esperar e recomeçar
                Serial.println("\n>>> ESTADO: SILÊNCIO (Reiniciando Ciclo...) <<<\n");
                building = true;  // Inverte direção
                currentVoice = 0; 
                synth.noteOff(0); // Garante que a fundamental morre
            }
        }
        return;
    }

    // Lógica de Passo-a-Passo
    if (now - lastTime > STEP_DELAY) {
        lastTime = now;

        if (building) {
            if (currentVoice < MAX_VOICES_USE) {
                addHarmonic(currentVoice);
                currentVoice++;
            } else {
                holding = true; // Entra em espera no topo
            }
        } else {
            if (currentVoice > 0) {
                currentVoice--;
                removeHarmonic(currentVoice);
            } else {
                holding = true; // Entra em espera no fundo
            }
        }
    }
}

// --- ENGINE MATEMÁTICA ---
void addHarmonic(int voiceIndex) {
    // Harmônico Ímpar: 1, 3, 5, 7, 9...
    int harmonicNum = (voiceIndex * 2) + 1;

    // Freq = Fundamental * N
    uint32_t freqHz = BASE_FREQ * harmonicNum;
    
    // Volume = Max / N (Decaimento 1/x)
    // Usamos 255 como base para ter resolução
    int vol = 255 / harmonicNum;
    
    // Toca a nota
    synth.noteOn(voiceIndex, freqHz * 100, vol); // *100 p/ CentiHz

   
    // Formatação alinhada para leitura fácil
    Serial.printf("[+] ADD Voz %02d | Harm: #%02d | Freq: %5d Hz | Vol: %03d | ", 
                  voiceIndex, harmonicNum, freqHz, vol);
    
    // Barrinha visual lateral para o volume
    Serial.print("Energia: ");
    int bars = vol / 10; 
    if(bars == 0 && vol > 0) bars = 1; // Pelo menos 1 pontinho se tiver som
    for(int i=0; i<bars; i++) Serial.print("█");
    Serial.println(); // Pula linha
}

void removeHarmonic(int voiceIndex) {
    synth.noteOff(voiceIndex);
    
    int harmonicNum = (voiceIndex * 2) + 1;
    
    Serial.printf("[-] DEL Voz %02d | Harm: #%02d | Desligando...  | ", 
                  voiceIndex, harmonicNum);
    Serial.println("Estado: OFF");

}

