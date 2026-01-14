/**
 * @file ESP32Synth.h
 * @brief Header corrigido com suporte a Loop PingPong/Reverse e Contador 64-bit
 * @brief Header corrected with PingPong/Reverse Loop support and 64-bit Counter
 */

#ifndef ESP32_SYNTH_H
#define ESP32_SYNTH_H

#include <Arduino.h>
#include <math.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"

#include "ESP32SynthNotes.h" 

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define SYNTH_CHIP "ESP32-S3"
    #define SYNTH_RATE 52036 
#elif defined(CONFIG_IDF_TARGET_ESP32)
    #define SYNTH_CHIP "ESP32-Standard"
    #define SYNTH_RATE 48000 
#else
    #define SYNTH_CHIP "Generic ESP32"
    #define SYNTH_RATE 48000 
#endif

#define MAX_VOICES 32        // Máximo de vozes simultâneas / Maximum simultaneous voices
#define I2S_RATE 48000       // Taxa de amostragem I2S / I2S sample rate
#define ENV_MAX 268435456    // Valor máximo do envelope / Maximum envelope value
#define MAX_WAVETABLES 1000  // Máximo de wavetables / Maximum wavetables
#define MAX_ARP_NOTES 16     // Máximo de notas no arpejo / Maximum notes in arpeggio

enum SynthOutputMode { SMODE_PDM, SMODE_I2S }; 
enum WaveType { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_PULSE, WAVE_WAVETABLE, WAVE_NOISE, WAVE_SAMPLE };
enum FilterType { FILTER_NONE, FILTER_LP, FILTER_HP, FILTER_BP };
enum BitDepth { BITS_4, BITS_8, BITS_16 };
enum EnvState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

// --- NOVO ENUM PARA MODOS DE LOOP ---
// --- NEW ENUM FOR LOOP MODES ---
enum LoopMode { 
    LOOP_OFF, 
    LOOP_FORWARD,   // Esquerda -> Direita (Padrão) / Left -> Right (Default)
    LOOP_PINGPONG,  // Vai e Volta / Back and Forth
    LOOP_REVERSE    // Direita -> Esquerda / Right -> Left
};

// Definições para facilitar sua vida (Números negativos para ondas básicas)
// Definitions to make life easier (Negative numbers for basic waves)
#define W_SINE     -1
#define W_TRI      -2
#define W_SAW      -3
#define W_PULSE    -4
#define W_NOISE    -5

struct Instrument {
    const uint8_t* seqVolumes;    
    const int16_t* seqWaves;      
    uint8_t        seqLen;        
    uint16_t       seqSpeedMs;    

    uint8_t        susVol;
    int16_t        susWave;       

    const uint8_t* relVolumes;
    const int16_t* relWaves;
    uint8_t        relLen;
    uint16_t       relSpeedMs;

    bool           smoothMorph; 
};

#define MAX_SAMPLES 100 

struct SampleData {
    const int16_t* data;
    uint32_t length;
    uint32_t sampleRate;      
    uint32_t rootFreqCentiHz; 
};

struct SampleZone {
    uint32_t lowFreq;   
    uint32_t highFreq;  
    uint16_t sampleId;  
    uint32_t rootOverride; 
};

// --- STRUCT ATUALIZADA COM LOOP MODE ---
// --- UPDATED STRUCT WITH LOOP MODE ---
struct Instrument_Sample {
    const SampleZone* zones;
    uint8_t           numZones;
    LoopMode          loopMode;    // Novo: Modo de loop / New: Loop mode
    uint32_t          loopStart;   // Novo: Início do loop / New: Loop start
    uint32_t          loopEnd;     // Novo: Fim do loop (0 = fim do arquivo) / New: Loop end (0 = end of file)
};

struct Voice {
    bool active = false; 
    uint32_t freqVal = 0;      
    uint32_t phase = 0;        
    uint32_t phaseInc = 0;     
    uint8_t vol = 127; 
    uint32_t pulseWidth = 0x80000000; 
    uint32_t rngState = 12345; 
    int16_t noiseSample = 0;   
    
    // --- VIBRATO ---
    // --- VIBRATO ---
    uint32_t vibPhase = 0;
    uint32_t vibRateInc = 0;
    uint32_t vibDepthInc = 0;
    int32_t  vibOffset = 0; 

    // --- TREMOLO ---
    // --- TREMOLO ---
    uint32_t trmPhase = 0;      
    uint32_t trmRateInc = 0;    
    uint16_t trmDepth = 0;      
    uint16_t trmModGain = 256;  

    EnvState envState = ENV_IDLE;
    uint32_t currEnvVal = 0;   
    uint32_t rateAttack = 0;
    uint32_t rateDecay = 0;
    uint32_t rateRelease = 0;
    uint32_t levelSustain = 0;

    // Parâmetros de síntese legados
    // Legacy synthesis parameters
    WaveType type = WAVE_SINE;
    const void* wtData = nullptr;
    uint32_t wtSize = 0;
    BitDepth depth = BITS_8;
    FilterType filterType = FILTER_NONE;
    int32_t filterLow = 0;   
    int32_t filterBand = 0;  
    int16_t coefF = 0;       
    int16_t coefQ = 0;       

    // Estado do modo de instrumento
    // Instrument mode state
    Instrument* inst = nullptr;
    uint8_t stageIdx = 0;       
    uint32_t controlTick = 0;   
    uint16_t currWaveId = 0;    
    uint16_t nextWaveId = 0;    
    uint8_t currWaveIsBasic = 0; 
    uint8_t nextWaveIsBasic = 0; 
    uint8_t currWaveType = 0;   
    uint8_t nextWaveType = 0;   
    uint8_t morph = 0;          

    uint16_t attackMs = 0;
    uint16_t decayMs = 0;
    uint16_t releaseMs = 0;

    // Estado de deslize de tom (Pitch slide)
    // Pitch slide state
    bool slideActive = false;
    uint32_t slideTicksRemaining = 0; 
    uint32_t slideTicksTotal = 0;     
    int32_t slideDeltaInc = 0;        
    uint32_t slideTargetInc = 0;      
    uint32_t slideTargetFreqCenti = 0; 
    int32_t slideRem = 0;              
    int32_t slideRemAcc = 0;          

    // Estado do arpejador
    // Arpeggiator state
    bool     arpActive = false;       
    uint32_t arpNotes[MAX_ARP_NOTES]; 
    uint8_t  arpLen = 0;              
    uint8_t  arpIdx = 0;              
    uint16_t arpSpeedMs = 100;        
    uint32_t arpTickCounter = 0;     

    // --- MOTOR DE SAMPLE ATUALIZADO (64 BIT + CONTROLE DE LOOP) ---
    // --- UPDATED SAMPLE ENGINE (64 BIT + LOOP CONTROL) ---
    Instrument_Sample* instSample = nullptr; 
    uint16_t curSampleId = 0;
    
    // ATENÇÃO: Mudou para 64 bits para evitar overflow em samples grandes
    // ATTENTION: Changed to 64 bits to avoid overflow in large samples
    uint64_t samplePos1616 = 0; 
    uint32_t sampleInc1616 = 0; 
    
    // Controle de Loop avançado
    // Advanced Loop Control
    LoopMode sampleLoopMode = LOOP_OFF;
    bool     sampleDirection = true; // true = indo, false = voltando / true = going, false = returning
    uint32_t sampleLoopStart = 0;
    uint32_t sampleLoopEnd = 0;
    bool     sampleFinished = false; 
};

class ESP32Synth {
public:
    ESP32Synth();
    ~ESP32Synth();

    // Inicializa o sintetizador / Initialize the synthesizer
    bool begin(int dataPin = 5, SynthOutputMode mode = SMODE_PDM, int clkPin = -1, int wsPin = -1);

    // Controle de notas / Note control
    void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume);
    void noteOff(uint8_t voice); 
    void setFrequency(uint8_t voice, uint32_t freqCentiHz);
    void setVolume(uint8_t voice, uint8_t volume);
    void setWave(uint8_t voice, WaveType type);
    void setPulseWidth(uint8_t voice, uint8_t width); 
    void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);
    void setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t resonance);
    void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);
    void setTremolo(uint8_t voice, uint32_t rateCentiHz, uint16_t depth);
    void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth);
    void setWavetable(const void* data, uint32_t size, BitDepth depth);
    
    void setInstrument(uint8_t voice, Instrument* inst); 
    void detachWave(uint8_t voice, WaveType type); 
    void detachInstrumentAndSetWave(uint8_t voice, WaveType type); 
    void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth); 
    void setControlRateHz(uint16_t hz); 

    void slide(uint8_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs);
    void slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs);

    uint32_t getFrequencyCentiHz(uint8_t voice);
    uint8_t getVolume8Bit(uint8_t voice);   
    uint8_t getEnv8Bit(uint8_t voice);      
    uint8_t getOutput8Bit(uint8_t voice);   

    uint32_t getVolumeRaw(uint8_t voice);   
    uint32_t getEnvRaw(uint8_t voice);      
    uint32_t getOutputRaw(uint8_t voice);   

    // Samples
    // Samples
    void setInstrument(uint8_t voice, Instrument_Sample* inst); 
    void registerSample(uint16_t id, const int16_t* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz);
    
    void renderLoop();
    
    bool isVoiceActive(uint8_t voice) { return voices[voice].active; }
    const char* getChipModel() { return SYNTH_CHIP; }
    uint32_t getSampleRate() { return SYNTH_RATE; }

    void detachArpeggio(uint8_t voice);

    template <typename... Args>
    void setArpeggio(uint8_t voice, uint16_t durationMs, Args... freqs) {
        if (voice >= MAX_VOICES) return;
        uint32_t tempNotes[] = { (uint32_t)freqs... };
        size_t count = sizeof...(freqs);
        if (count > MAX_ARP_NOTES) count = MAX_ARP_NOTES; 
        Voice* v = &voices[voice];
        for(size_t i = 0; i < count; i++) {
            v->arpNotes[i] = tempNotes[i];
        }
        v->arpLen = count;
        v->arpSpeedMs = durationMs;
        v->arpIdx = 0;
        v->arpTickCounter = 0; 
        v->arpActive = true;
    }

private:
    struct WavetableEntry { const void* data; uint32_t size; BitDepth depth; };

    Voice voices[MAX_VOICES];
    WavetableEntry wavetables[MAX_WAVETABLES];

    i2s_chan_handle_t tx_handle = NULL;
    static void audioTask(void* param);
    
    void render(int16_t* buffer, int samples);
    void processControl(); 
    
    uint32_t controlSampleCounter = 0; 
    uint16_t controlRateHz = 100;      
    uint32_t controlIntervalSamples = (SYNTH_RATE / 100);

    inline int16_t fetchWavetableSample(uint16_t id, uint32_t phase);

    int16_t sineLUT[256];
};

#endif