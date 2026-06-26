/**
 * @file ESP32Synth.h
 * @author Danilo Gabriel
 * @brief Versatile, polyphonic synthesizer library for ESP32 microcontrollers.
 * @version 2.4.2
 */

#pragma once
#ifndef ESP32_SYNTH_H
#define ESP32_SYNTH_H

#if defined(ARDUINO)
    #include <Arduino.h>
    #include <math.h>
    #include <FS.h>

    // System Abstraction - Arduino
    #define SYNTH_GET_CPU_FREQ_MHZ() ESP.getCpuFreqMHz()
    #define SYNTH_MICROS()           micros()
    #define SYNTH_DELAY_US(us)       delayMicroseconds(us)

    // Filesystem Abstraction - Arduino
    #define SYNTH_FILE               fs::File
    #define SYNTH_FILE_REF           fs::File&
    #define SYNTH_STREAM_OPEN()      fs.open(path, "r")
    #define SYNTH_FILE_CLOSE(f)      if(f) { f.close(); }
    #define SYNTH_FILE_SEEK(f, pos)  f.seek(pos)
    #define SYNTH_FILE_READ(f, b, s) f.read(b, s)
    #define SYNTH_FILE_POS(f)        f.position()
    #define SYNTH_FILE_AVAILABLE(f)  f.available()
    #define SYNTH_FILE_SIZE(f)       f.size()
    #define SYNTH_FILE_VALID(f)      (f)

#else
    #include <stdint.h>
    #include <stddef.h>
    #include <string.h>
    #include <math.h>
    #include <stdio.h>
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/semphr.h"
    #include "esp_timer.h"
    #include "esp_rom_sys.h"
    #include "esp_system.h"
    #include "esp_cpu.h"
    #include "esp_clk.h"
    #include "esp_attr.h"

    // System Abstraction - Pure ESP-IDF
    #define SYNTH_GET_CPU_FREQ_MHZ() (esp_clk_cpu_freq() / 1000000)
    #define SYNTH_MICROS()           (uint32_t)esp_timer_get_time()
    #define SYNTH_DELAY_US(us)       esp_rom_delay_us(us)

    // Filesystem Abstraction - Pure ESP-IDF C
    #define SYNTH_FILE               FILE*
    #define SYNTH_FILE_REF           FILE*
    #define SYNTH_STREAM_OPEN()      fopen(path, "rb")
    #define SYNTH_FILE_CLOSE(f)      if(f) { fclose(f); f = NULL; }
    #define SYNTH_FILE_SEEK(f, pos)  fseek(f, pos, SEEK_SET)
    #define SYNTH_FILE_READ(f, b, s) fread(b, 1, s, f)
    #define SYNTH_FILE_POS(f)        ftell(f)
    inline size_t esp_synth_file_size(FILE* f) { long pos = ftell(f); fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, pos, SEEK_SET); return size; }
    inline size_t esp_synth_file_avail(FILE* f) { long pos = ftell(f); fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, pos, SEEK_SET); return size - pos; }
    #define SYNTH_FILE_AVAILABLE(f)  esp_synth_file_avail(f)
    #define SYNTH_FILE_SIZE(f)       esp_synth_file_size(f)
    #define SYNTH_FILE_VALID(f)      (f != NULL)
#endif

#include "esp_heap_caps.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"

#include "ESP32SynthNotes.h"

#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
#include "driver/dac_continuous.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #define SYNTH_CHIP "ESP32-S3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
    #define SYNTH_CHIP "ESP32-Standard"
#else
    #define SYNTH_CHIP "Generic ESP32"
#endif

// Please refer to ESP32Synth_Config.hpp for configuration options and notes on performance trade-offs.
#include "ESP32Synth_Config.hpp"

/*
    About SMODEs (Audio Output Modes):

    - SMODE_I2S: Standard I2S output. Best for overall audio quality and lowest CPU usage,
      but requires an external DAC module (like the PCM5102A) and multiple pins (BCK, WS, DATA).
      Supports both 16-bit and 32-bit depth.

    - SMODE_DAC: Uses the built-in DAC channels. Only available on the classic ESP32
      (specifically GPIO25 and GPIO26) and is limited to 8-bit output. It is the simplest
      option for single-pin audio on older boards, but lacks high fidelity and is not
      available on newer chips like the ESP32-S3.

    - SMODE_PDM: Pulse-Density Modulation. Outputs a high-frequency 16-bit bitstream to a
      single pin (requires a simple RC filter). Highly recommended for the ESP32-S3 and newer chips,
      but not recommended for the classic ESP32 due to high noise and distortion.

    - SMODE_PWM: Uses the LEDC hardware to generate a high-frequency PWM signal on a single pin.
      Powered by a bare-metal, auto-synchronized hardware interrupt, it delivers jitter-free,
      clear 10-bit audio at 48kHz. It works flawlessly on both classic ESP32 and ESP32-S3,
      making it the absolute best single-pin alternative when you don't have an external I2S DAC.
      Just add a simple RC filter (Resistor + Capacitor) to the output pin!
      (i personally don't use the RC filter... it's good enough for me without it, but for best results you should add it).
*/

enum SynthOutputMode : uint8_t {
    SMODE_PDM,
    SMODE_I2S,
    SMODE_DAC,
    SMODE_PWM,    // <--- New! Alternative for PDM.
    SMODE_CUSTOM
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
    SYNTH_FILE        file;
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
    // 64-bit (Alinhamento de 8 bytes para performance bruta no Xtensa)
    int64_t            slideVolCurr;
    int64_t            slideVolInc;

    // ====================================================================
    // UNION EXTREMA: Motores de Síntese Mutuamente Exclusivos (24 bytes)
    // Agrupa buffers de Sample, Wavetables, Tracker Tick e Custom States.
    // Mantém o motor ADSR isolado para 100% de segurança de envelope.
    // ====================================================================
    union {
        // Modo: WAVE_SAMPLE & WAVE_STREAM
        struct {
            uint64_t samplePos1616;
            uint32_t sampleInc1616;
            uint32_t sampleLoopStart;
            uint32_t sampleLoopEnd;
            uint32_t streamFracAccum;
        };
        // Modo: WAVE_WAVETABLE
        struct {
            const void* wtData;
            uint32_t    wtSize;
        };
        // Modo: Tracker Instrument (Sequenciador de passos)
        struct {
            uint32_t    controlTick;
        };
        // Modo: WAVE_CUSTOM (Seu parquinho de diversões O(1))
        uint32_t cw[6]; 
    };
    // ====================================================================

    // Pointers (32-bit: 4 bytes)
    Instrument*        inst;
    Instrument_Sample* instSample;
    SynthCustomWaveCallback customWaveFunc;

    // 32-bit (4 bytes) - ADSR isolado e seguro contra corrupção
    uint32_t           rateAttack;
    uint32_t           rateDecay;
    uint32_t           rateRelease;
    uint32_t           levelSustain;

    // Outros campos de 32-bit (4 bytes)
    uint32_t           phase;
    uint32_t           phaseInc;
    uint32_t           currEnvVal;
    uint32_t           freqVal;
    uint32_t           pulseWidth;
    uint32_t           rngState;
    uint32_t           vibPhase;
    uint32_t           vibRateInc;
    uint32_t           vibDepthInc;
    uint32_t           trmPhase;
    uint32_t           trmRateInc;
    uint32_t           slideFreqTicksRemaining;
    uint32_t           slideFreqTicksTotal;
    uint32_t           slideFreqTargetInc;
    uint32_t           slideFreqTargetCenti;
    uint32_t           slideVolTicksRemaining;
    uint32_t           arpTickCounter;
    int32_t            vibOffset;
    int32_t            slideFreqDeltaInc;
    int32_t            slideFreqRem;
    int32_t            slideFreqRemAcc;

    // Large arrays
    uint32_t           arpNotes[MAX_ARP_NOTES];

    // 16-bit (2 bytes)
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
    uint16_t           vol;
    uint16_t           slideVolTarget;
    int16_t            noiseSample;
    int16_t            streamTrackId;
    int16_t            lastStreamSample;

    // 8-bit, Bools e Enums (1 byte)
    EnvState           envState;
    WaveType           type;
    WaveType           currWaveType;
    WaveType           nextWaveType;
    BitDepth           depth;
    LoopMode           sampleLoopMode;
    uint8_t            stageIdx;
    uint8_t            currWaveIsBasic;
    uint8_t            nextWaveIsBasic;
    uint8_t            morph;
    uint8_t            arpLen;
    uint8_t            arpIdx;
    bool               active;
    bool               slideFreqActive;
    bool               slideVolActive;
    bool               arpActive;
    bool               sampleDirection;
    bool               sampleFinished;
    bool               smoothEnv;
};

class ESP32Synth {
public:
    ESP32Synth();
    ~ESP32Synth();

    // --- Custom Hooks Definitions ---
    typedef void (*SynthDSPCallback)(int32_t* mixBuffer, int numSamples);
    typedef void (*SynthControlCallback)();
    typedef void (*SynthCustomOutputCallback)(int16_t* samples, int numSamples);

    // --- Initialization & System ---
    void end();
    bool begin(int dacPin);
    bool begin(int bckPin, int wsPin, int dataPin);
    bool begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth);
    bool begin(int bckPin, int wsPin, int dataPin, int mclkPin, I2S_Depth i2sDepth);
    bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth);
    bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, int mclkPin, I2S_Depth i2sDepth);

    bool beginCustom(uint32_t sampleRate = 48000, SynthCustomOutputCallback customOutput = nullptr);

    void setSampleRate(uint32_t rate); // EXPERIMENTAL, Use at your own risk. Changing sample rate may cause instability.
    void setControlRateHz(uint16_t hz);
    void setMasterVolume(uint16_t volume);
    void setVolDepthBase(uint8_t bits);
    void setMasterBitcrush(uint8_t bits);
    uint16_t getMasterVolume();
    const char* getChipModel();
    int32_t getSampleRate();

    // --- Custom Hooks ---
    void setCustomDSP(SynthDSPCallback dspFunc);
    void setCustomControl(SynthControlCallback ctrlFunc);
    void setCustomOutput(SynthCustomOutputCallback outFunc);

    // --- Custom Output ---
    void generateSamples(int16_t* outBuffer, int numSamples);
    void generateSamplesStereo(int16_t* outBufferLR, int numSamplePairs);

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
    void setSmoothEnv(uint16_t voice, bool enable);

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
#ifdef ARDUINO
    int8_t   setupStream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz = 26163, bool loop = false);
    int8_t   playStream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume = 255, uint32_t rootFreqCentiHz = 26163, bool loop = false);
#else
    int8_t   setupStream(uint16_t voice, const char* path, uint32_t rootFreqCentiHz = 26163, bool loop = false);
    int8_t   playStream(uint16_t voice, const char* path, uint16_t volume = 255, uint32_t rootFreqCentiHz = 26163, bool loop = false);
#endif
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

    // --- Performance & Debug ---
    float getCPULoad(); // Returns 0.0 to 100.0%

    // --- PWM ---
    volatile bool _running = false;
    int16_t* pwm_ping_pong_buf[2] = {nullptr, nullptr};
    volatile int pwm_active_buf = 0;
    volatile int pwm_read_idx = 0;
    uint32_t pwm_block_samples = 0;
    SemaphoreHandle_t pwm_sema = NULL;
    void* pwm_timer = NULL;


private:
    struct WavetableEntry {
        const void* data;
        uint32_t    size;
        BitDepth    depth;
    };

    StreamTrack   streams[MAX_STREAMS];
    TaskHandle_t  streamTaskHandle = NULL;
    TaskHandle_t  audioTaskHandle = NULL;
    static void   sdLoaderTask(void* param);
    bool parseWavHeader(SYNTH_FILE_REF file, uint32_t& outSampleRate, uint32_t& outDataPos, uint32_t& outDataSize, uint16_t& outChannels, uint16_t& outBits);

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
    uint8_t       _bitcrush = 0;
    void slideVolAbsolute(uint16_t voice, uint16_t startVol16, uint16_t endVol16, uint32_t durationMs);

    i2s_chan_handle_t tx_handle = NULL;
    #if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
    dac_continuous_handle_t dac_handle = NULL;
    #endif
    SynthOutputMode currentMode = SMODE_PDM;

    // Pin memory for reset.
    int _dataPin = -1;
    int _bckPin  = -1;
    int _wsPin   = -1;
    int _mclkPin = -1;

    static void audioTask(void* param);
    void render(void* buffer, int32_t* mixBuffer, int samples);
    void renderLoop();
    void processControl();
    int16_t fetchWavetableSample(uint16_t id, uint32_t phase);

    SynthDSPCallback          _customDSP     = nullptr;
    SynthControlCallback      _customControl = nullptr;
    SynthCustomOutputCallback _customOutput  = nullptr;

    // --- Performance Measurement ---
    volatile float _dspLoad = 0.0f;
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
    v->arpLen         = count;
    v->arpSpeedMs     = durationMs;
    v->arpIdx         = 0;
    v->arpTickCounter = 0;
    v->arpActive      = true;
}

#endif // ESP32_SYNTH_H