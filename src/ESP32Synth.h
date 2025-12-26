/**
 * @file ESP32Synth.h
 * @brief Sintetizador Polifônico de 32 Vozes com Filtros SVF, PWM e LFO.
 * @author Danilo
 * @version 1.1.0
 */

#ifndef ESP32_SYNTH_H
#define ESP32_SYNTH_H

#include <Arduino.h>
#include "driver/i2s_pdm.h"
#include "ESP32SynthNotes.h" 

// Configurações de Taxa de Amostragem por Hardware
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define SYNTH_CHIP "ESP32-S3"
    #define SYNTH_RATE 52036  // Ajuste fino para S3 Zero
#elif defined(CONFIG_IDF_TARGET_ESP32)
    #define SYNTH_CHIP "ESP32-Standard"
    #define SYNTH_RATE 48000  // Frequência padrão WROOM 
#else
    #define SYNTH_CHIP "Generic ESP32"
    #define SYNTH_RATE 48000 // Coloque aqui a nova taxa calculada  
#endif

// Definições Globais 
#define MAX_VOICES 32        // Limite de polifonia simultânea
#define I2S_RATE 48000       // Taxa de saída do driver
#define ENV_MAX 268435456    // Resolução do acumulador de envelope (28-bit)

// Pinagem Padrão (I2S/PDM)
#define I2S_BCK_PIN -1
#define I2S_WS_PIN -1
#define I2S_DATA_PIN 5

enum WaveType { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_PULSE, WAVE_WAVETABLE, WAVE_NOISE };
enum FilterType { FILTER_NONE, FILTER_LP, FILTER_HP, FILTER_BP };
enum BitDepth { BITS_4, BITS_8, BITS_16 };
enum EnvState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

// Estrutura de controle individual por voz
struct Voice {
    bool active = false; 
    
    // Oscilador e Fase
    uint32_t freqVal = 0;      
    uint32_t phase = 0;        
    uint32_t phaseInc = 0;     
    uint8_t vol = 127; 
    
    // PWM (Largura de Pulso)
    uint32_t pulseWidth = 0x80000000; // Padrão 50%

    // Motor de Ruído
    uint32_t rngState = 12345; 
    int16_t noiseSample = 0;   

    // Modulação (LFO/Vibrato)
    uint32_t vibPhase = 0;
    uint32_t vibRateInc = 0;
    uint32_t vibDepthInc = 0;

    // Gerador de Envelope (ADSR)
    EnvState envState = ENV_IDLE;
    uint32_t currEnvVal = 0;   
    uint32_t rateAttack = 0;
    uint32_t rateDecay = 0;
    uint32_t rateRelease = 0;
    uint32_t levelSustain = 0;

    // Motor de Wavetable
    WaveType type = WAVE_SINE;
    const void* wtData = nullptr;
    uint32_t wtSize = 0;
    BitDepth depth = BITS_8; // Profundidade de bits da amostra

    // Filtro de Estado Variável (Chamberlin SVF)
    FilterType filterType = FILTER_NONE;
    int32_t filterLow = 0;   
    int32_t filterBand = 0;  
    int16_t coefF = 0;       
    int16_t coefQ = 0;       
};

class ESP32Synth {
public:
    ESP32Synth();
    ~ESP32Synth();

    // Inicialização e Driver
    bool begin(int clkPin = -1, int dataPin = -1);

    // Gestão de Notas e Parâmetros de Áudio 
    void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume);
    void noteOff(uint8_t voice); 
    
    // Modulação em tempo real 
    void setFrequency(uint8_t voice, uint32_t freqCentiHz);
    void setVolume(uint8_t voice, uint8_t volume);
    void setWave(uint8_t voice, WaveType type);
    void setPulseWidth(uint8_t voice, uint8_t width); // Novo: Controle PWM (0-255)
    void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);
    void setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t resonance);
    void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);

    // Amostras Externas
    void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth);
    void setWavetable(const void* data, uint32_t size, BitDepth depth);
    
    // Engine de Renderização
    void renderLoop();
    
    // Status e Debug
    bool isVoiceActive(uint8_t voice) { return voices[voice].active; }
    const char* getChipModel() { return SYNTH_CHIP; }
    uint32_t getSampleRate() { return SYNTH_RATE; }

private:
    Voice voices[MAX_VOICES];
    i2s_chan_handle_t tx_handle = NULL;
    static void audioTask(void* param);
    void render(int16_t* buffer, int samples);
    int16_t sineLUT[256];
};

#endif