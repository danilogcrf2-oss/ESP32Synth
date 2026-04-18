/**
 * @file ESP32Synth.h //queria colocar um nome mais unico, mas quando postei pela primeira vez nem pensei.
 * @author Danilo Gabriel
 * @brief Versatile, polyphonic synthesizer library for ESP32 microcontrollers.
 * @version 2.4.0
 */
 
#ifndef ESP32_SYNTH_H
#define ESP32_SYNTH_H

#include <Arduino.h>
#include <math.h>
#include <FS.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "ESP32SynthNotes.h"

#if defined(CONFIG_IDF_TARGET_ESP32)
#include "driver/dac_continuous.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define SYNTH_CHIP "ESP32-S3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
    #define SYNTH_CHIP "ESP32-Standard"
#else
    #define SYNTH_CHIP "Generic ESP32"
#endif


// Core Synth Limits

// /* // default:
#define MAX_VOICES 80 // ram safe max 140, default 80, max 350, jitter and FreeRTOS no time for tasks 364
#define MAX_WAVETABLES 100 // default 100
#define MAX_SAMPLES 100 // default 100
#define MAX_ARP_NOTES 16 // default 16
#define MAX_STREAMS 4 // Max concurrent SD streams (RAM/CPU limited).
#define STREAM_BUF_SAMPLES 0 // Ring buffer size (Must be power of 2).
// */ 

 /* // Low ram usage (for LVGL, hi ram usage libs):
#define MAX_VOICES 1 
#define MAX_WAVETABLES 1 
#define MAX_SAMPLES 1
#define MAX_ARP_NOTES 5 
#define MAX_STREAMS 1 
#define STREAM_BUF_SAMPLES 2048 
// */
// ====================================================================================
//    SINE WAVE LOOK-UP TABLE
// ====================================================================================
#define SINE_LUT_SIZE 4096
#define SINE_LUT_MASK (SINE_LUT_SIZE - 1)
#define SINE_SHIFT    20

// Shared sine LUT
extern int16_t sineLUT[SINE_LUT_SIZE];

#define STREAM_BUF_MASK (STREAM_BUF_SAMPLES - 1)
#define ENV_MAX 268435456 

enum SynthOutputMode : uint8_t {
    SMODE_PDM, // PDM is not recomended on ESP32 due to high noise and distortion. Recomended for ESP32-S3 and newer.
    SMODE_I2S,
    SMODE_DAC  
};

enum WaveType : int8_t {
    WAVE_SINE      = -1,
    WAVE_TRIANGLE  = -2,
    WAVE_SAW       = -3,
    WAVE_PULSE     = -4,
    WAVE_NOISE     = -5,
    WAVE_WAVETABLE = 1,
    WAVE_SAMPLE    = 2,
    WAVE_STREAM    = 3,
    WAVE_CUSTOM    = 4,
};

enum BitDepth : uint8_t {
    BITS_4,
    BITS_8,
    BITS_16,
};

enum EnvState : uint8_t {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
};

enum I2S_Depth : uint8_t {
    I2S_16BIT,
    I2S_32BIT
};

enum LoopMode : uint8_t {
    LOOP_OFF,
    LOOP_FORWARD,
    LOOP_PINGPONG,
    LOOP_REVERSE
};

struct Instrument {
    const uint8_t* seqVolumes;
    const int16_t* seqWaves;
    const uint8_t* relVolumes;
    const int16_t* relWaves;
    uint16_t       seqSpeedMs;
    int16_t        susWave;
    uint16_t       relSpeedMs;
    uint8_t        seqLen;
    uint8_t        susVol;
    uint8_t        relLen;
    bool           smoothMorph;
    bool           smoothVolume;
};

struct SampleData {
    const void* data;
    uint32_t    length;
    uint32_t    sampleRate;
    uint32_t    rootFreqCentiHz;
    BitDepth    depth;
};

struct SampleZone {
    uint32_t lowFreq;
    uint32_t highFreq;
    uint16_t sampleId;
    uint32_t rootOverride;
};

struct Instrument_Sample {
    const SampleZone* zones;
    uint8_t           numZones;
    LoopMode          loopMode;
    uint32_t          loopStart;
    uint32_t          loopEnd;
};

struct StreamTrack {
    fs::File          file;
    int16_t           buffer[STREAM_BUF_SAMPLES];
    volatile uint16_t head;
    volatile uint16_t tail;
    uint32_t          sampleRate;
    uint32_t          dataStartPos;
    uint32_t          dataSize;
    uint16_t          numChannels;
    uint16_t          bitsPerSample;
    volatile int32_t  seekTarget;
    volatile uint32_t samplesPlayed;
    uint32_t          loopStartBytes;
    uint32_t          loopEndBytes;
    uint32_t          rootFreqCentiHz;
    bool              active;
    volatile bool     playing;
    bool              loop;
};

struct Voice;
typedef void (*SynthCustomWaveCallback)(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep);

struct Voice {
    uint64_t           samplePos1616;
    const void*        wtData;
    Instrument*        inst;
    Instrument_Sample* instSample;
    SynthCustomWaveCallback customWaveFunc;
    uint32_t           phase;
    uint32_t           phaseInc;
    uint32_t           currEnvVal;
    uint32_t           sampleInc1616;
    uint32_t           freqVal;
    uint32_t           pulseWidth;
    uint32_t           rngState;
    uint32_t           vibPhase;
    uint32_t           vibRateInc;
    uint32_t           vibDepthInc;
    uint32_t           trmPhase;
    uint32_t           trmRateInc;
    uint32_t           rateAttack;
    uint32_t           rateDecay;
    uint32_t           rateRelease;
    uint32_t           levelSustain;
    uint32_t           wtSize;
    uint32_t           controlTick;
    uint32_t           slideFreqTicksRemaining;
    uint32_t           slideFreqTicksTotal;
    uint32_t           slideFreqTargetInc;
    uint32_t           slideFreqTargetCenti;
    uint32_t           slideVolTicksRemaining;
    int64_t            slideVolCurr;
    uint32_t           arpTickCounter;
    uint32_t           sampleLoopStart;
    uint32_t           sampleLoopEnd;
    uint32_t           streamFracAccum;
    uint32_t           arpNotes[MAX_ARP_NOTES];
    int32_t            vibOffset;
    int32_t            slideFreqDeltaInc;
    int32_t            slideFreqRem;
    int32_t            slideFreqRemAcc;
    int64_t            slideVolInc;
    uint16_t           trmDepth;
    uint16_t           trmModGain;
    uint16_t           currWaveId;
    uint16_t           nextWaveId;
    uint16_t           attackMs;
    uint16_t           decayMs;
    uint16_t           releaseMs;
    uint16_t           arpSpeedMs;
    uint16_t           curSampleId;
    uint16_t           startPhase;  
    int16_t            noiseSample;
    int16_t            streamTrackId;
    int16_t            lastStreamSample;
    uint16_t           vol;
    uint16_t           slideVolTarget;
    EnvState           envState;
    WaveType           type;
    BitDepth           depth;
    uint8_t            stageIdx;
    uint8_t            currWaveIsBasic;
    uint8_t            nextWaveIsBasic;
    WaveType           currWaveType;
    WaveType           nextWaveType;
    uint8_t            morph;
    uint8_t            arpLen;
    uint8_t            arpIdx;
    LoopMode           sampleLoopMode;
    bool               active;
    bool               slideFreqActive;
    bool               slideVolActive;
    bool               arpActive;
    bool               sampleDirection;
    bool               sampleFinished;
};

class ESP32Synth {
public:
    ESP32Synth();
    ~ESP32Synth();

    // --- Initialization & System ---
    void end();
    bool begin(int dacPin);
    bool begin(int bckPin, int wsPin, int dataPin);
    bool begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth);
    bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth);
    void setSampleRate(uint32_t rate);
    void setControlRateHz(uint16_t hz);
    void setMasterVolume(uint16_t volume);
    void setVolDepthBase(uint8_t bits);
    void setMasterBitcrush(uint8_t bits);
    uint16_t getMasterVolume();
    const char* getChipModel();
    int32_t getSampleRate();

    // --- Custom Hooks ---
    typedef void (*SynthDSPCallback)(int32_t* mixBuffer, int numSamples);
    typedef void (*SynthControlCallback)();
    
    void setCustomDSP(SynthDSPCallback dspFunc);
    void setCustomControl(SynthControlCallback ctrlFunc);

    // --- Core Voice Control ---
    void noteOn(uint16_t voice, uint32_t freqCentiHz, uint16_t volume);
    void noteOff(uint16_t voice);
    void setFrequency(uint16_t voice, uint32_t freqCentiHz);
    void setVolume(uint16_t voice, uint16_t volume);
    void setWave(uint16_t voice, WaveType type);
    void setPulseWidthBitDepth(uint8_t bits);
    void setPulseWidth(uint16_t voice, uint32_t width);
    void setCustomWave(uint16_t voice, SynthCustomWaveCallback cb);

    // --- Envelope ---
    void setEnv(uint16_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);

    // --- Phase Control ---
    void setStartPhase(uint16_t voice, uint16_t phaseDegrees);
    void setCurrentPhase(uint16_t voice, uint16_t phaseDegrees);

    // --- Modulation & Slides ---
    void setVibrato(uint16_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);
    void setVibratoPhase(uint16_t voice, uint16_t phaseDegrees);
    void setTremolo(uint16_t voice, uint32_t rateCentiHz, uint16_t depth);
    void setTremoloPhase(uint16_t voice, uint16_t phaseDegrees);
    void slideFreq(uint16_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs);
    void slideFreqTo(uint16_t voice, uint32_t endFreqCentiHz, uint32_t durationMs);
    void slideVol(uint16_t voice, uint16_t startVol, uint16_t endVol, uint32_t durationMs);
    void slideVolTo(uint16_t voice, uint16_t endVol, uint32_t durationMs);
    
    // --- Wavetable & Instruments ---
    void setWavetable(uint16_t voice, const void* data, uint32_t size, BitDepth depth);
    void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth);
    void setInstrument(uint16_t voice, Instrument* inst);
    void setInstrument(uint16_t voice, Instrument_Sample* inst);
    void detachInstrument(uint16_t voice, WaveType newWaveType);

    // --- Sample Control ---
    bool registerSample(uint16_t sampleId, const void* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz, BitDepth depth = BITS_16);
    void setSample(uint16_t voice, uint16_t sampleId, LoopMode loopMode = LOOP_OFF, uint32_t loopStart = 0, uint32_t loopEnd = 0);
    void setSampleLoop(uint16_t voice, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd);

    // --- Arpeggiator ---
    template <typename... Args>
    void setArpeggio(uint16_t voice, uint16_t durationMs, Args... freqs);
    void detachArpeggio(uint16_t voice);

    // --- SD Streaming ---
    int8_t   setupStream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz = 26163, bool loop = false);
    int8_t playStream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume = 255, uint32_t rootFreqCentiHz = 26163, bool loop = false);
    void     pauseStream(uint16_t voice);
    void     resumeStream(uint16_t voice);
    void     stopStream(uint16_t voice);
    void     seekStreamMs(uint16_t voice, uint32_t ms);
    void     setStreamLoopPointsMs(uint16_t voice, uint32_t startMs, uint32_t endMs);
    uint32_t getStreamPositionMs(uint16_t voice);
    uint32_t getStreamDurationMs(uint16_t voice);
    bool     isStreamPlaying(uint16_t voice);

    // --- Getters & Status ---
    uint32_t getFrequencyCentiHz(uint16_t voice);
    uint16_t getVolume(uint16_t voice);
    uint8_t  getVolume8Bit(uint16_t voice);
    uint8_t  getEnv8Bit(uint16_t voice);
    uint8_t  getOutput8Bit(uint16_t voice);
    uint32_t getVolumeRaw(uint16_t voice);
    uint32_t getEnvRaw(uint16_t voice);
    uint32_t getOutputRaw(uint16_t voice);
    bool     isVoiceActive(uint16_t voice);
    EnvState getEnvState(uint16_t voice);
    WaveType getWaveType(uint16_t voice);
    uint32_t getPhase(uint16_t voice);
    uint32_t getPulseWidth(uint16_t voice);

private:
    struct WavetableEntry {
        const void* data;
        uint32_t    size;
        BitDepth    depth;
    };

    StreamTrack   streams[MAX_STREAMS];
    TaskHandle_t  streamTaskHandle = NULL;
    TaskHandle_t  audioTaskHandle = NULL;
    volatile bool _running = false;
    static void   sdLoaderTask(void* param);
    bool parseWavHeader(fs::File& file, uint32_t& outSampleRate, uint32_t& outDataPos, uint32_t& outDataSize, uint16_t& outChannels, uint16_t& outBits);
    
    Voice          voices[MAX_VOICES];
    WavetableEntry wavetables[MAX_WAVETABLES];
    SampleData     samples[MAX_SAMPLES];

    uint32_t      _sampleRate = 48000;
    I2S_Depth     _i2sDepth = I2S_16BIT;
    uint16_t      _masterVolume = 65280;
    uint8_t       _volShift = 8;
    uint8_t       _pwShift = 24;
    bool          _customSampleRate = false;
    uint16_t      controlRateHz = 100;
    uint32_t      controlIntervalSamples;
    uint32_t      controlSampleCounter = 0;
    uint8_t _bitcrush = 0;
    void slideVolAbsolute(uint16_t voice, uint16_t startVol16, uint16_t endVol16, uint32_t durationMs);

    i2s_chan_handle_t tx_handle = NULL;
   #if defined(CONFIG_IDF_TARGET_ESP32)
    dac_continuous_handle_t dac_handle = NULL;
    #endif
    SynthOutputMode currentMode = SMODE_PDM;

    // Pin memory for reset.
    int _dataPin = -1;
    int _bckPin = -1;
    int _wsPin = -1;

    static void audioTask(void* param);
    void render(void* buffer, int samples);
    void renderLoop();
    void processControl();
    int16_t fetchWavetableSample(uint16_t id, uint32_t phase);

    SynthDSPCallback     _customDSP = nullptr;
    SynthControlCallback _customControl = nullptr;
    
};

template <typename... Args>
void ESP32Synth::setArpeggio(uint16_t voice, uint16_t durationMs, Args... freqs) {
    if (voice >= MAX_VOICES) return;

    uint32_t tempNotes[] = { (uint32_t)freqs... };
    size_t count = sizeof...(freqs);
    if (count > MAX_ARP_NOTES) count = MAX_ARP_NOTES;

    Voice* v = &voices[voice];
    for (size_t i = 0; i < count; i++) {
        v->arpNotes[i] = tempNotes[i];
    }
    v->arpLen = count;
    v->arpSpeedMs = durationMs;
    v->arpIdx = 0;
    v->arpTickCounter = 0;
    v->arpActive = true;
}

#endif // ESP32_SYNTH_H
