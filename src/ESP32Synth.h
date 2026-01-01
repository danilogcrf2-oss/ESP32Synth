/**
 * @file ESP32Synth.h
 * @brief Header corrigido para evitar aviso IRAM_ATTR
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

#define MAX_VOICES 32        
#define I2S_RATE 48000       
#define ENV_MAX 268435456    
#define MAX_WAVETABLES 1000  // maximum number of wavetable slots supported

enum SynthOutputMode { SMODE_PDM, SMODE_I2S }; 
enum WaveType { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_PULSE, WAVE_WAVETABLE, WAVE_NOISE };
enum FilterType { FILTER_NONE, FILTER_LP, FILTER_HP, FILTER_BP };
enum BitDepth { BITS_4, BITS_8, BITS_16 };
enum EnvState { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

// Instrument definition: arrays of volume and wavetable IDs per stage
// Additional waveType arrays allow selecting basic wave forms instead of wavetables per element.
// waveType value: 0 = use wavetable id from waveA/waveD/waveR, 1=SINE, 2=TRI, 3=SAW, 4=PULSE, 5=NOISE
struct Instrument {
    const uint8_t* volA; const uint8_t* volD; uint8_t volS; const uint8_t* volR;
    const uint16_t* waveA; const uint16_t* waveD; uint16_t waveS; const uint16_t* waveR;
    const uint8_t* waveTypeA; const uint8_t* waveTypeD; uint8_t waveTypeS; const uint8_t* waveTypeR;
    uint8_t lenA, lenD, lenR; // sizes for arrays in each stage
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
    uint32_t vibPhase = 0;
    uint32_t vibRateInc = 0;
    uint32_t vibDepthInc = 0;
    EnvState envState = ENV_IDLE;
    uint32_t currEnvVal = 0;   
    uint32_t rateAttack = 0;
    uint32_t rateDecay = 0;
    uint32_t rateRelease = 0;
    uint32_t levelSustain = 0;

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

    // Instrument mode state (optional)
    Instrument* inst = nullptr; // null = legacy ADSR mode
    uint8_t stageIdx = 0;       // index inside current stage array
    uint32_t controlTick = 0;   // remaining control ticks for current element
    uint16_t currWaveId = 0;    // current wavetable ID (if using wavetable)
    uint16_t nextWaveId = 0;    // next wavetable ID for morphing
    uint8_t currWaveIsBasic = 0; // 1 if current element uses basic wave (sine/tri/...)
    uint8_t nextWaveIsBasic = 0; // 1 if next element uses basic wave
    uint8_t currWaveType = 0;   // waveType enum value when basic (1..5)
    uint8_t nextWaveType = 0;   // next waveType when basic
    uint8_t morph = 0;          // morph factor (0..255)

    // Keep stage durations (ms) to compute control ticks
    uint16_t attackMs = 0;
    uint16_t decayMs = 0;
    uint16_t releaseMs = 0;

    // Pitch slide state (performed at control-rate for low CPU usage)
    bool slideActive = false;
    uint32_t slideTicksRemaining = 0; // remaining control ticks
    uint32_t slideTicksTotal = 0;     // total ticks for slide
    int32_t slideDeltaInc = 0;        // phaseInc delta to add each control tick (signed)
    uint32_t slideTargetInc = 0;      // exact final phaseInc to set at end of slide
    uint32_t slideTargetFreqCenti = 0; // exact final frequency in centi-Hz to avoid rounding drift
    int32_t slideRem = 0;              // signed remainder (diff - delta*ticks)
    int32_t slideRemAcc = 0;          // accumulator for remainder distribution
};

class ESP32Synth {
public:
    ESP32Synth();
    ~ESP32Synth();

    bool begin(int dataPin = 5, SynthOutputMode mode = SMODE_PDM, int clkPin = -1, int wsPin = -1);

    void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume);
    void noteOff(uint8_t voice); 
    void setFrequency(uint8_t voice, uint32_t freqCentiHz);
    void setVolume(uint8_t voice, uint8_t volume);
    void setWave(uint8_t voice, WaveType type);
    void setPulseWidth(uint8_t voice, uint8_t width); 
    void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);
    void setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t resonance);
    void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);
    void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth);
    void setWavetable(const void* data, uint32_t size, BitDepth depth);// para todos os voices
    // Instrument / control-rate API
    void setInstrument(uint8_t voice, Instrument* inst); // assign or clear instrument (nullptr to fallback)
    void detachWave(uint8_t voice, WaveType type); // convenience: remove instrument and set basic wave (short name)
    // deprecated wrapper kept for compatibility
    void detachInstrumentAndSetWave(uint8_t voice, WaveType type); // deprecated: use detachWave()
    void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth); // global wavetable registry
    void setControlRateHz(uint16_t hz); // control-rate for processControl

    // Lightweight pitch slide (values in centi-Hz). Runs at control-rate to keep CPU cost minimal.
    void slide(uint8_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs);
    void slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs);

    // Query current logical frequency of a voice (centi-Hz)
    uint32_t getFrequencyCentiHz(uint8_t voice);

    void renderLoop();
    
    bool isVoiceActive(uint8_t voice) { return voices[voice].active; }
    const char* getChipModel() { return SYNTH_CHIP; }
    uint32_t getSampleRate() { return SYNTH_RATE; }

private:
    // Wavetable registry entry
    struct WavetableEntry { const void* data; uint32_t size; BitDepth depth; };

    Voice voices[MAX_VOICES];
    WavetableEntry wavetables[MAX_WAVETABLES];

    i2s_chan_handle_t tx_handle = NULL;
    static void audioTask(void* param);
    
    // IRAM_ATTR removido daqui, mantido apenas no .cpp
    void render(int16_t* buffer, int samples);

    // Control-rate / scheduling
    void processControl(); // called at control-rate to advance instrument stages
    uint32_t controlSampleCounter = 0; // accumulated samples
    uint16_t controlRateHz = 100;      // default control-rate
    uint32_t controlIntervalSamples = (SYNTH_RATE / 100);

    // Fast wavetable access helper (implemented in .cpp and placed in IRAM)
    inline int16_t fetchWavetableSample(uint16_t id, uint32_t phase);

    int16_t sineLUT[256];
};

#endif