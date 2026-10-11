#include <Arduino.h>
#include "ESP32Synth.h"

// Definições de Pinos I2S (Altere conforme a sua placa)
#define I2S_BCLK 4
#define I2S_LRCK 15
#define I2S_DOUT 2

#define DAC_PIN 25
#define PDM_PIN 25

ESP32Synth synth;

#define dac 0
#define i2s 1
#define pdm 2

// #define OUT_MODE dac
#define OUT_MODE i2s
// #define OUT_MODE pdm

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n--- ESP32Synth: Iniciando Stress Test de Polifonia ---");
    Serial.printf("Chip Detectado: %s\n", synth.getChipModel());
    Serial.printf("Limite de Vozes Configurado: %d\n", MAX_VOICES);

    switch(OUT_MODE){
        case dac:
            if (!synth.begin(DAC_PIN)) {
            Serial.println("ERRO: Falha ao iniciar a engine de áudio!");
            while(1);
            }
        break;

        case i2s:
            if (!synth.begin(I2S_BCLK, I2S_LRCK, I2S_DOUT, I2S_32BIT)) {
            Serial.println("ERRO: Falha ao iniciar a engine de áudio!");
            while(1);
            }
        break;

        case pdm:
            if (!synth.begin(PDM_PIN)) {
            Serial.println("ERRO: Falha ao iniciar a engine de áudio!");
            while(1);
            }
        break;
    }

    synth.setMasterVolume(200);

    uint32_t fundamentalCentiHz = 5500;
    uint8_t baseVolume = 255;

    Serial.println("Ligando osciladores senoidais em serie...");

    for (int i = 0; i < MAX_VOICES; i++) {
        uint32_t harmonicNum = i + 1; 
        Serial.printf("voz %d, h %d \n",i , harmonicNum);
        uint32_t freq = fundamentalCentiHz * harmonicNum;
        uint8_t vol;
        if( (baseVolume / harmonicNum) < 1){
            vol = 1;
        }else{
            vol = baseVolume / harmonicNum; 
        }
        synth.setWave(i, WAVE_SINE);
        synth.setEnv(i, 0, 0, 255, 0); 
        synth.noteOn(i, freq, vol);
        delay(i >= 64 ? 1000 : 2);
    }

    Serial.println("Teste rodando! Ouça a onda se formando.");
    Serial.println("Vá no ESP32Synth.h, aumente MAX_VOICES, recompile e veja quando o ESP32 começa a pedir socorro!");
}

void loop() {
  Serial.printf("Polifonia atual: %d vozes gerando 1 onda Dente de Serra gigante.\n", MAX_VOICES);
  delay(2000);
}