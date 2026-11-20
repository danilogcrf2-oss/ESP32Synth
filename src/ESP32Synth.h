/**
 * @file ESP32Synth.h
 * @author Danilo Gabriel
 * @brief Versatile, polyphonic synthesizer library for ESP32 microcontrollers.
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
    #define SYNTH_FILE_WRITE(f, b, s) f.write((const uint8_t*)b, s)

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
    #define SYNTH_FILE_WRITE(f, b, s) fwrite(b, 1, s, f)

#endif

#include "esp_heap_caps.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"

#include "ESP32SynthNotes.h"

#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
#include "driver/dac_continuous.h"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32P4)
    #define SYNTH_CHIP "ESP32-P4"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    #define SYNTH_CHIP "ESP32-S3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
    #define SYNTH_CHIP "ESP32-Standard"
#else
    #define SYNTH_CHIP "Generic ESP32"
#endif

// Please refer to ESP32Synth_Config.hpp for configuration options and notes on performance trade-offs.
#include "ESP32Synth_Config.hpp"
#ifndef PI
#define PI 3.14159265358979323846
#endif

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
    SMODE_PWM,    
    SMODE_CUSTOM,
    SMODE_HEADLESS
};

enum SynthPWMState : uint8_t {
    PWM_STATE_MUTED = 0,    // 0% duty (0V ground, zero coil dissipation)
    PWM_STATE_RAMP_UP = 1,  // S-curve ramp from 0% to 50%
    PWM_STATE_ACTIVE = 2,   // 50% duty baseline + audio
    PWM_STATE_RAMP_DOWN = 3 // S-curve ramp from 50% to 0%
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

enum FXType : uint8_t {
    FX_NONE       = 0,
    FX_CUSTOM     = 1,
    FX_OVERDRIVE  = 2,
    FX_CHORUS     = 3,
    FX_FLANGER    = 4,
    FX_PHASER     = 5,
    FX_DELAY      = 6,
    FX_REVERB     = 7,
    FX_FILTER     = 8,
    FX_COMPRESSOR = 9,
    FX_BITCRUSHER = 10
};

enum FXOverdriveMode : uint8_t {
    FX_OD_SOFT = 0,
    FX_OD_TUBE = 1,
    FX_OD_FUZZ = 2
};

enum FXFilterMode : uint8_t {
    FX_FILTER_LPF   = 0,
    FX_FILTER_HPF   = 1,
    FX_FILTER_BPF   = 2,
    FX_FILTER_NOTCH = 3
};

enum FXOverdriveParam : uint8_t {
    FX_OD_DRIVE = 0,
    FX_OD_TONE  = 1,
    FX_OD_LEVEL = 2,
    FX_OD_MODE  = 3,
    FX_OD_MIX   = 4
};

enum FXChorusParam : uint8_t {
    FX_CHORUS_RATE         = 0,
    FX_CHORUS_DEPTH        = 1,
    FX_CHORUS_DELAY_MS     = 2,
    FX_CHORUS_FEEDBACK     = 3,
    FX_CHORUS_MIX          = 4,
    FX_CHORUS_STEREO_PHASE = 5
};

enum FXFlangerParam : uint8_t {
    FX_FLANGER_RATE         = 0,
    FX_FLANGER_DEPTH        = 1,
    FX_FLANGER_DELAY_MS10   = 2,
    FX_FLANGER_FEEDBACK     = 3,
    FX_FLANGER_MIX          = 4,
    FX_FLANGER_STEREO_PHASE = 5
};

enum FXPhaserParam : uint8_t {
    FX_PHASER_RATE         = 0,
    FX_PHASER_DEPTH        = 1,
    FX_PHASER_FEEDBACK     = 2,
    FX_PHASER_STAGES       = 3,
    FX_PHASER_MIX          = 4,
    FX_PHASER_STEREO_PHASE = 5
};

enum FXDelayParam : uint8_t {
    FX_DELAY_TIME_MS  = 0,
    FX_DELAY_FEEDBACK = 1,
    FX_DELAY_DAMP     = 2,
    FX_DELAY_PINGPONG = 3,
    FX_DELAY_MIX      = 4
};

enum FXReverbParam : uint8_t {
    FX_REVERB_ROOM  = 0,
    FX_REVERB_DAMP  = 1,
    FX_REVERB_WIDTH = 2,
    FX_REVERB_MIX   = 3
};

enum FXFilterParam : uint8_t {
    FX_FILTER_CUTOFF    = 0,
    FX_FILTER_MODE      = 1,
    FX_FILTER_RESONANCE = 2,
    FX_FILTER_BW_MODE   = 3,
    FX_FILTER_MIX       = 4
};

enum FXCompressorParam : uint8_t {
    FX_COMP_THRESHOLD  = 0,
    FX_COMP_RATIO      = 1,
    FX_COMP_ATTACK_MS  = 2,
    FX_COMP_RELEASE_MS = 3,
    FX_COMP_MAKEUP     = 4,
    FX_COMP_MIX        = 5
};

enum FXBitcrushParam : uint8_t {
    FX_CRUSH_BITS       = 0,
    FX_CRUSH_DOWNSAMPLE = 1,
    FX_CRUSH_MIX        = 2
};

enum LFOWave : uint8_t {
    LFO_SINE          = 0,
    LFO_TRIANGLE      = 1,
    LFO_SAW_UP        = 2,
    LFO_SAW_DOWN      = 3,
    LFO_SQUARE        = 4,
    LFO_SAMPLE_HOLD   = 5,
    LFO_SMOOTH_RANDOM = 6
};

enum LFOTarget : uint8_t {
    LFO_TARGET_NONE         = 0,
    LFO_TARGET_PULSE_WIDTH  = 1,
    LFO_TARGET_VOICE_CP     = 2,
    LFO_TARGET_VOICE_PITCH  = 3,
    LFO_TARGET_VOICE_VOL    = 4,
    LFO_TARGET_VOICE_PAN    = 5,
    LFO_TARGET_BUS_FX_PARAM = 6,
    LFO_TARGET_BUS_MIX      = 7,
    LFO_TARGET_BUS_PAN      = 8,
    LFO_TARGET_BUS_WIDTH    = 9,
    LFO_TARGET_DSP_PARAM    = 10,
    LFO_TARGET_MASTER_VOL   = 11,
    LFO_TARGET_MASTER_PAN   = 12,
    LFO_TARGET_MASTER_WIDTH = 13,
    LFO_TARGET_LFO_RATE     = 14,
    LFO_TARGET_LFO_AMP      = 15
};

typedef void (*SynthLFOCallback)(uint8_t lfoId, int16_t value);

struct ModLFO {
    uint32_t         phase;
    uint32_t         rateInc;
    uint32_t         startPhase;
    uint32_t         baseRate;
    uint32_t         effRate;
    uint32_t         delayTicksCfg;
    uint32_t         delayTicksRem;
    uint32_t         fadeGainQ15;
    uint32_t         fadeStepQ15;
    uint32_t         rngState;
    SynthLFOCallback callback;
    uint16_t         delayMsCfg;
    uint16_t         fadeInMsCfg;
    int16_t          curValue;
    int16_t          curRand;
    int16_t          nextRand;
    int16_t          effAmpQ8;
    LFOWave          wave;
    uint8_t          duty;
    bool             oneShot;
    bool             finished;
    volatile bool    active;
};

struct ModLFORoute {
    int32_t       valA;
    int32_t       valB;
    int32_t       lastOffset;
    uint16_t      targetStart;
    uint16_t      targetEnd;
    uint8_t       lfoId;
    LFOTarget     target;
    uint8_t       subId;
    uint8_t       paramIdx;
    bool          rangeMode;
    volatile bool active;
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

enum PanLaw : uint8_t {
    PAN_LAW_CONSTANT_POWER = 0,
    PAN_LAW_LINEAR         = 1,
    PAN_LAW_BALANCE        = 2
};

#if SYNTH_STEREO
enum PanState : uint8_t {
    PAN_CENTER = 0,
    PAN_HARD_L = 1,
    PAN_HARD_R = 2,
    PAN_STEREO = 3
};

struct __attribute__((aligned(4))) StereoSample {
    int16_t l;
    int16_t r;
};
#endif

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
    uint8_t     numChannels;
};
extern SampleData registeredSamples[MAX_SAMPLES];

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

#if ENABLE_MP3
struct FlashMP3Stream {
    const uint8_t* data;
    uint32_t size;
    uint32_t pos;
};
#endif

struct StreamTrack {
    SYNTH_FILE        file;
#if SYNTH_STEREO
    StereoSample*     buffer;
#else
    int16_t*          buffer;
#endif
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
    uint32_t          durationMs;
    volatile bool     seeking;
    #if ENABLE_MP3
    void*             mp3Decoder;
    const uint8_t*    mp3Data;
    uint8_t*          mp3InBuf;
    int               mp3BytesLeft;
    int               mp3ReadOffset;
    uint32_t          mp3Offset;
    uint32_t          mp3Bitrate;
    bool              isMP3;
    bool              isVBR;
    #endif
};

typedef void (*SynthCustomFXCallback)(int32_t* busBuffer, int samples, int16_t* ep, int32_t* es);
#if SYNTH_STEREO
typedef void (*SynthCustomStereoFXCallback)(int32_t* busL, int32_t* busR, int samples, int16_t* ep, int32_t* es);
#endif

struct FXNode {
    int32_t                     es[SYNTH_FX_STATES] __attribute__((aligned(8)));
    int16_t                     ep[SYNTH_FX_PARAMS];
    void*                       fxData;
    SynthCustomFXCallback       customFXFunc;
#if SYNTH_STEREO
    SynthCustomStereoFXCallback customStereoFXFunc;
#endif
    FXType                      type;
    volatile bool               active;
};

struct AudioBus {
    bool             active;
    uint8_t          mixLevel;
    volatile uint8_t fxMask;
#if SYNTH_STEREO
    int16_t  pan;
    uint16_t panGainL;
    uint16_t panGainR;
    uint16_t stereoWidth;
#endif
    FXNode   fxChain[MAX_FX_PER_BUS];
};

struct Voice;
typedef void (*SynthCustomWaveCallback)(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep);
#if SYNTH_STEREO
typedef void (*SynthCustomStereoWaveCallback)(Voice* vo, int32_t* mixBufferL, int32_t* mixBufferR, int samples, int32_t startEnv, int32_t envStep);
#endif

struct Voice {
    int64_t            slideVolCurr;
    int64_t            slideVolInc;

    union {
        struct {
            uint64_t samplePos1616;
            uint32_t sampleInc1616;
            uint32_t sampleLoopStart;
            uint32_t sampleLoopEnd;
            uint32_t streamFracAccum;
        };
        uint32_t cw[SYNTH_CUSTOM_WAVE_STATES];
    };

    const void*        wtData;
    uint32_t           wtSize;

    Instrument*                   inst;
    Instrument_Sample*            instSample;
    SynthCustomWaveCallback       customWaveFunc;
#if SYNTH_STEREO
    SynthCustomStereoWaveCallback customStereoWaveFunc;
#endif

    uint32_t           rateAttack;
    uint32_t           rateDecay;
    uint32_t           rateRelease;
    uint32_t           levelSustain;
    uint32_t           controlTick;

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
#if SYNTH_STEREO
    uint32_t           panLfoPhase;
    uint32_t           panLfoRateInc;
    int32_t            slidePanCurr;
    int32_t            slidePanInc;
    uint32_t           slidePanTicksRemaining;
#endif

    uint32_t           arpNotes[MAX_ARP_NOTES];

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
    int16_t            cp[SYNTH_CUSTOM_PARAMS_PER_VOICE];
#if SYNTH_STEREO
    int16_t            pan;
    int16_t            slidePanTarget;
    uint16_t           panGainL;
    uint16_t           panGainR;
    uint16_t           panLfoDepth;
    uint16_t           stereoWidth;
#endif

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
    uint8_t            targetBus;
#if SYNTH_STEREO
    uint8_t            panState;
    bool               slidePanActive;
#endif
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
    typedef void (*SynthDSPCallback)(int32_t* mixBuffer, int numSamples, int16_t* dp);
    typedef void (*SynthControlCallback)();
    typedef void (*SynthCustomOutputCallback)(int16_t* samples, int numSamples);
#if SYNTH_STEREO
    typedef void (*SynthStereoDSPCallback)(int32_t* mixL, int32_t* mixR, int numSamples, int16_t* dp);
    typedef void (*SynthCustomStereoOutputCallback)(int16_t* samplesLR, int numSamplePairs);
#endif

    // --- Initialization & System ---
    void end();
    bool begin(int dacPin);
    bool begin(int dacPinL, int dacPinR);
    bool begin(int bckPin, int wsPin, int dataPin);
    bool begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth);
    bool begin(int bckPin, int wsPin, int dataPin, int mclkPin, I2S_Depth i2sDepth);
    bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth);
    bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, int mclkPin, I2S_Depth i2sDepth);

    bool beginDAC(int pinL, int pinR = -1);
    bool beginPWM(int pinL, int pinR = -1);
    bool beginPDM_DAC(int pinL, int pinR = -1);

    bool beginCustom(uint32_t sampleRate = 48000, SynthCustomOutputCallback customOutput = nullptr);
#if SYNTH_STEREO
    bool beginCustomStereo(uint32_t sampleRate = 48000, SynthCustomStereoOutputCallback customOutput = nullptr);
#else
    inline bool beginCustomStereo(uint32_t sampleRate = 48000, SynthCustomOutputCallback customOutput = nullptr) {
        return beginCustom(sampleRate, customOutput);
    }
#endif
    bool beginHeadless(uint32_t sampleRate = 48000);

    void setSampleRate(uint32_t rate);
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
#if SYNTH_STEREO
    void setCustomStereoDSP(SynthStereoDSPCallback dspFunc);
    void setCustomStereoOutput(SynthCustomStereoOutputCallback outFunc);
#else
    inline void setCustomStereoDSP(SynthDSPCallback dspFunc) { setCustomDSP(dspFunc); }
    inline void setCustomStereoOutput(SynthCustomOutputCallback outFunc) { setCustomOutput(outFunc); }
#endif

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
    void setCustomParam(uint16_t voice, uint8_t paramId, int16_t value);
    void setDSPParam(uint8_t paramId, int16_t value);
    void setVoiceBus(uint16_t voice, uint8_t busId);
    void setBusFX(uint8_t busId, uint8_t slotId, FXType type, SynthCustomFXCallback cb = nullptr);
#if SYNTH_STEREO
    void setCustomStereoWave(uint16_t voice, SynthCustomStereoWaveCallback cb);
    void setBusFX(uint8_t busId, uint8_t slotId, FXType type, SynthCustomStereoFXCallback cb);
#endif
    void setBusFXParam(uint8_t busId, uint8_t slotId, uint8_t paramId, int16_t value);
    void setBusMix(uint8_t busId, uint8_t volume);

    // --- Built-in Bus FX Shortcuts ---
    void setBusOverdrive(uint8_t busId, uint8_t slotId, uint8_t drive = 128, uint8_t tone = 160, uint8_t level = 180, uint8_t mode = 0, uint8_t mix = 255);
    void setBusChorus(uint8_t busId, uint8_t slotId, uint16_t rateCentiHz = 80, uint8_t depth = 120, uint8_t delayMs = 12, int16_t feedback = 20, uint8_t mix = 128, uint8_t stereoPhase = 180);
    void setBusFlanger(uint8_t busId, uint8_t slotId, uint16_t rateCentiHz = 35, uint8_t depth = 180, uint8_t delayTenthsMs = 15, int16_t feedback = 140, uint8_t mix = 128, uint8_t stereoPhase = 90);
    void setBusPhaser(uint8_t busId, uint8_t slotId, uint16_t rateCentiHz = 50, uint8_t depth = 180, int16_t feedback = 120, uint8_t stages = 4, uint8_t mix = 128, uint8_t stereoPhase = 90);
    void setBusDelay(uint8_t busId, uint8_t slotId, uint16_t delayMs = 250, uint8_t feedback = 120, uint8_t damp = 100, bool pingPong = true, uint8_t mix = 110);
    void setBusReverb(uint8_t busId, uint8_t slotId, uint8_t roomSize = 160, uint8_t damping = 110, uint8_t width = 255, uint8_t mix = 90);
    void setBusFilter(uint8_t busId, uint8_t slotId, uint16_t cutoffHz = 2000, uint16_t resonance = 70, uint8_t mode = 0, uint8_t mix = 255);
    void setBusCompressor(uint8_t busId, uint8_t slotId, uint8_t threshold = 140, uint8_t ratio = 4, uint16_t attackMs = 5, uint16_t releaseMs = 80, uint8_t makeup = 32, uint8_t mix = 255);
    void setBusBitcrush(uint8_t busId, uint8_t slotId, uint8_t bits = 8, uint8_t downsample = 4, uint8_t mix = 255);
    void removeBusFX(uint8_t busId, uint8_t slotId);
    void clearBusFX(uint8_t busId);

    // --- Modular LFO & Routing Matrix ---
    void     setLFO(uint8_t lfoId, LFOWave wave, uint32_t rateCentiHz, uint16_t startPhaseDeg = 0, uint16_t delayMs = 0, uint16_t fadeInMs = 0, bool oneShot = false);
    void     setLFORate(uint8_t lfoId, uint32_t rateCentiHz);
    void     setLFOWave(uint8_t lfoId, LFOWave wave);
    void     setLFOPhase(uint8_t lfoId, uint16_t phaseDeg);
    void     setLFODuty(uint8_t lfoId, uint8_t duty);
    void     setLFOEnabled(uint8_t lfoId, bool enable);
    void     setLFOCallback(uint8_t lfoId, SynthLFOCallback cb);
    void     retriggerLFO(uint8_t lfoId);
    void     retriggerAllLFOs();
    void     removeLFO(uint8_t lfoId);
    int16_t  getLFOValue(uint8_t lfoId);
    uint16_t getLFOValueUnipolar(uint8_t lfoId);
    uint8_t  getLFOValue8Bit(uint8_t lfoId);
    int32_t  getLFOValueScaled(uint8_t lfoId, int32_t minVal, int32_t maxVal);
    bool     isLFOActive(uint8_t lfoId);

    int8_t routeLFOToVoice(uint8_t lfoId, LFOTarget target, uint16_t voiceStart, uint16_t voiceEnd, int32_t depthOrMin, int32_t maxVal = 0, bool rangeMode = false, uint8_t paramIdx = 0);
    int8_t assignLFOToVoice(uint8_t lfoId, LFOTarget target, uint16_t voice, int32_t depth, uint8_t paramIdx = 0);
    int8_t assignLFOToVoiceRange(uint8_t lfoId, LFOTarget target, uint16_t voiceStart, uint16_t voiceEnd, int32_t minVal, int32_t maxVal, uint8_t paramIdx = 0);
    int8_t routeLFOToBusFX(uint8_t lfoId, uint8_t busId, uint8_t slotId, uint8_t paramId, int32_t depthOrMin, int32_t maxVal = 0, bool rangeMode = false);
    int8_t assignLFOToBusFX(uint8_t lfoId, uint8_t busId, uint8_t slotId, uint8_t paramId, int32_t minVal, int32_t maxVal);
    int8_t routeLFOToBus(uint8_t lfoId, LFOTarget target, uint8_t busId, int32_t depthOrMin, int32_t maxVal = 0, bool rangeMode = false);
    int8_t routeLFOToMaster(uint8_t lfoId, LFOTarget target, int32_t depthOrMin, int32_t maxVal = 0, bool rangeMode = false, uint8_t paramIdx = 0);
    int8_t routeLFOToLFO(uint8_t srcLfoId, LFOTarget target, uint8_t dstLfoId, int32_t depthOrMin, int32_t maxVal = 0, bool rangeMode = false);
    void   setLFORouteDepth(uint8_t routeId, int32_t depthOrMin, int32_t maxVal = 0, bool rangeMode = false);
    void   setLFORouteActive(uint8_t routeId, bool active);
    void   removeLFORoute(uint8_t routeId);
    void   clearLFORoutes(uint8_t lfoId);
    void   clearAllLFORoutes();

    // --- Stereo & Panning ---
#if SYNTH_STEREO
    void     setPanLaw(PanLaw law);
    PanLaw   getPanLaw();
    void     setPanBitDepth(uint8_t bits);
    void     setPan(uint16_t voice, int32_t pan);
    void     setPanMIDI(uint16_t voice, uint8_t midiPan);
    int32_t  getPan(uint16_t voice);
    void     slidePan(uint16_t voice, int32_t startPan, int32_t endPan, uint32_t durationMs);
    void     slidePanTo(uint16_t voice, int32_t endPan, uint32_t durationMs);
    void     setAutoPan(uint16_t voice, uint32_t rateCentiHz, uint16_t depth);
    void     setAutoPanPhase(uint16_t voice, uint16_t phaseDegrees);
    void     setStereoWidth(uint16_t voice, uint16_t width);
    uint16_t getStereoWidth(uint16_t voice);
    void     setBusPan(uint8_t busId, int32_t pan);
    void     setBusWidth(uint8_t busId, uint16_t width);
    void     setMasterPan(int32_t pan);
    int32_t  getMasterPan();
    void     setMasterStereoWidth(uint16_t width);
    uint16_t getMasterStereoWidth();
#else
    inline void     setPanLaw(PanLaw) {}
    inline PanLaw   getPanLaw() { return (PanLaw)SYNTH_PAN_LAW; }
    inline void     setPanBitDepth(uint8_t) {}
    inline void     setPan(uint16_t, int32_t) {}
    inline void     setPanMIDI(uint16_t, uint8_t) {}
    inline int32_t  getPan(uint16_t) { return 0; }
    inline void     slidePan(uint16_t, int32_t, int32_t, uint32_t) {}
    inline void     slidePanTo(uint16_t, int32_t, uint32_t) {}
    inline void     setAutoPan(uint16_t, uint32_t, uint16_t) {}
    inline void     setAutoPanPhase(uint16_t, uint16_t) {}
    inline void     setStereoWidth(uint16_t, uint16_t) {}
    inline uint16_t getStereoWidth(uint16_t) { return 256; }
    inline void     setBusPan(uint8_t, int32_t) {}
    inline void     setBusWidth(uint8_t, uint16_t) {}
    inline void     setMasterPan(int32_t) {}
    inline int32_t  getMasterPan() { return 0; }
    inline void     setMasterStereoWidth(uint16_t) {}
    inline uint16_t getMasterStereoWidth() { return 256; }
#endif

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
    bool registerSample(uint16_t sampleId, const void* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz, BitDepth depth = BITS_16, uint8_t numChannels = 1);
    void setSample(uint16_t voice, uint16_t sampleId, LoopMode loopMode = LOOP_OFF, uint32_t loopStart = 0, uint32_t loopEnd = 0);
    void setSampleLoop(uint16_t voice, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd);

    // --- Arpeggiator ---
    template <typename... Args>
    void setArpeggio(uint16_t voice, uint16_t durationMs, Args... freqs);
    void detachArpeggio(uint16_t voice);

    // --- Streaming ---
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
    uint32_t getStreamSampleRate(uint16_t voice);
    uint16_t getStreamChannels(uint16_t voice);
    uint16_t getStreamBitsPerSample(uint16_t voice);
    uint32_t getStreamBitrate(uint16_t voice, bool snapToStandard = true);
    bool     isStreamVBR(uint16_t voice);
    bool     isStreamMP3(uint16_t voice);
#if ENABLE_MP3
    /**
     * @brief Streams an MP3 directly from Flash memory (PROGMEM) on Core 0.
     * Decodes in chunks into the circular buffer. Zero RAM waste, Zero DSP loss.
     */
    int8_t playMP3StreamFromFlash(uint16_t voice, const uint8_t* mp3Data, uint32_t mp3Size, uint16_t volume, uint32_t rootFreqCentiHz = 4800000, bool loop = false);
    
#ifdef ARDUINO
    int8_t   setupMP3Stream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz = 4800000, bool loop = false);
    int8_t   playMP3Stream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume = 255, uint32_t rootFreqCentiHz = 4800000, bool loop = false);
#else
    int8_t   setupMP3Stream(uint16_t voice, const char* path, uint32_t rootFreqCentiHz = 4800000, bool loop = false);
    int8_t   playMP3Stream(uint16_t voice, const char* path, uint16_t volume = 255, uint32_t rootFreqCentiHz = 4800000, bool loop = false);
#endif

#endif
    
    // --- SD Recording ---
#ifdef ARDUINO
    bool startRecording(fs::FS &fs, const char* path, bool stereo = (SYNTH_STEREO != 0));
#else
    bool startRecording(const char* path, bool stereo = (SYNTH_STEREO != 0));
#endif
    void stopRecording();
    bool isRecordingActive();

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
    float getStreamLoad(); // Returns 0.0 to 100.0%
    
    // --- PWM ---
    volatile bool _running = false;
    int16_t* pwm_ping_pong_buf[2] = {nullptr, nullptr};
    volatile int pwm_active_buf = 0;
    volatile int pwm_read_idx = 0;
    uint32_t pwm_block_samples = 0;
    SemaphoreHandle_t pwm_sema = NULL;
    void* pwm_timer = NULL;
    volatile SynthPWMState pwm_state = PWM_STATE_ACTIVE;
    volatile uint32_t pwm_ramp_phase = 0;
    volatile uint32_t pwm_ramp_inc = 0;
    #if SYNTH_STEREO
    volatile bool pwm_is_stereo = false;
    #endif

    // --- PWM Thermal & Click Protection ---
    void armPWM(uint16_t rampMs = 20, bool wait = true);
    void unArmPWM(uint16_t rampMs = 20, bool wait = true);
    bool isPWMArmed();

private:
    struct WavetableEntry {
        const void* data;
        uint32_t    size;
        BitDepth    depth;
    };

    AudioBus busesConfig[MAX_BUSES];

    volatile uint32_t _renderSeq = 0;
    void releaseFXNode(uint8_t busId, uint8_t slotId);
    void clearAllBusFXBuffers();
    void freeAllBusFX();
    bool initBuiltInFXNode(uint8_t busId, uint8_t slotId, FXType type, uint32_t extraMs = 0);

    ModLFO            lfos[MAX_LFOS];
    ModLFORoute       lfoRoutes[MAX_LFO_ROUTES];
    volatile uint32_t _activeLfoMask  = 0;
    volatile uint8_t  _maxActiveRoute = 0;
    int8_t allocLFORoute(uint8_t lfoId, LFOTarget target, uint16_t targetStart, uint16_t targetEnd, uint8_t subId, uint8_t paramIdx, int32_t depthOrMin, int32_t maxVal, bool rangeMode);
    void   resetLFORoute(ModLFORoute* rt);
    void   processModularLFOs();

    StreamTrack   streams[MAX_STREAMS];
    TaskHandle_t  streamTaskHandle = NULL;
    TaskHandle_t  audioTaskHandle = NULL;
    static void   streamLoaderTask(void* param);
    bool parseWavHeader(SYNTH_FILE_REF file, uint32_t& outSampleRate, uint32_t& outDataPos, uint32_t& outDataSize, uint16_t& outChannels, uint16_t& outBits);

    // --- SD Recording Internals ---
    volatile bool     _isRecording = false;
    SYNTH_FILE        _recordFile;
    uint32_t          _recordedDataSize = 0;
    TaskHandle_t      recordTaskHandle = NULL;
    int16_t*          _recBuffer = nullptr;
    volatile uint32_t _recHead = 0;
    volatile uint32_t _recTail = 0;
    bool              _recStereo = false;

    static void sdWriterTask(void* param);
#ifdef ARDUINO
    void writeWavHeader(fs::FS &fs, const char* path, uint32_t sampleRate, uint32_t dataSize);
#else
    void writeWavHeader(const char* path, uint32_t sampleRate, uint32_t dataSize);
#endif

    Voice          voices[MAX_VOICES];
    WavetableEntry wavetables[MAX_WAVETABLES];

    uint32_t      _sampleRate = 48000;
    I2S_Depth     _i2sDepth = I2S_16BIT;
    uint16_t      _masterVolume = 65280;
    uint8_t       _volShift = 8;
    uint8_t       _pwShift = 24;
    bool          _customSampleRate = false;
    uint16_t      controlRateHz = 100;
    uint32_t      controlIntervalSamples;
    volatile uint32_t controlSampleCounter = 0;
    uint8_t       _bitcrush = 0;
    int16_t       dp[SYNTH_CUSTOM_PARAMS_GLOBAL]; // Custom DSP parameters (Indices 0 to 3)
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
    int _pinR    = -1;

    static void audioTask(void* param);

    #if SYNTH_STEREO
    PanLaw   _panLaw       = (PanLaw)SYNTH_PAN_LAW;
    uint8_t  _panShift     = 8;
    int16_t  _masterPan    = 0;
    uint16_t _masterPanGainL = 32768;
    uint16_t _masterPanGainR = 32768;
    uint16_t _masterWidth  = 256;
    uint16_t _centerGainQ15 = (SYNTH_PAN_LAW == 0) ? 23170 : (SYNTH_PAN_LAW == 1) ? 16384 : 32768;

    int32_t* _pullBusL[MAX_BUSES] = {nullptr};
    int32_t* _pullBusR[MAX_BUSES] = {nullptr};
    int32_t* _pullBusC[MAX_BUSES] = {nullptr};
    int32_t* _pullVoiceScratch = nullptr;
    int16_t* _pullStereoOut = nullptr;

    void updateVoicePanGains(Voice* vo, int32_t effPan16);
    void computePanLawGains(int32_t pan16, bool stereoSource, uint16_t& outL, uint16_t& outR);
    bool ensurePullBuffers();
    void freePullBuffers();
    void render(void* buffer, int32_t** busL, int32_t** busR, int32_t** busC, int32_t* voiceScratch, int samples);
#else
    void render(void* buffer, int32_t** busBuffers, int samples);
#endif
    void renderLoop();
    void processControl();
    int16_t fetchWavetableSample(uint16_t id, uint32_t phase);

    SynthDSPCallback                _customDSP       = nullptr;
    SynthControlCallback            _customControl   = nullptr;
    SynthCustomOutputCallback       _customOutput    = nullptr;
#if SYNTH_STEREO
    SynthStereoDSPCallback          _customStereoDSP    = nullptr;
    SynthCustomStereoOutputCallback _customStereoOutput = nullptr;
#endif

    // --- Performance Measurement ---
    volatile float _dspLoad = 0.0f;
    volatile float _streamLoad = 0.0f;
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

// yes, this is a random comment lol.