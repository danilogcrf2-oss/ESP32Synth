/**
 * @file ESP32Synth.h
 * @author Danilo Gabriel
 * @brief Main header for the ESP32Synth library, a high-performance polyphonic synthesizer.
 *        (Cabeçalho principal da biblioteca ESP32Synth, um sintetizador polifônico de alta performance.)
 * @version 2.3.5-beta
 */

#ifndef ESP32_SYNTH_H
#define ESP32_SYNTH_H

// =================================================================================
// Includes
// =================================================================================
#include <Arduino.h>
#include <math.h>
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "ESP32SynthNotes.h"

// DAC driver is only for classic ESP32
// (Driver DAC apenas para ESP32 Clássico)
#if defined(CONFIG_IDF_TARGET_ESP32)
#include "driver/dac_continuous.h"
#endif


// =================================================================================
// Global Defines & Constants
// =================================================================================

// --- Chip Identification (Identificação do Chip) ---
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define SYNTH_CHIP "ESP32-S3"
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define SYNTH_CHIP "ESP32-Standard"
#else
#define SYNTH_CHIP "Generic ESP32"
#endif

// --- Legacy Compatibility (Compatibilidade Legada) ---
// For new projects, use getSampleRate().
// (Para novos projetos, use getSampleRate().)
#ifndef SYNTH_RATE
#define SYNTH_RATE 48000
#endif

// --- Core Synth Limits (Limites do Sintetizador) ---
#define MAX_VOICES 80           // Maximum simultaneous voices (Máximo de vozes simultâneas)
#define MAX_WAVETABLES 1000     // Maximum registered wavetables (Máximo de wavetables registradas)
#define MAX_SAMPLES 100         // Maximum registered samples (Máximo de samples registrados)
#define MAX_ARP_NOTES 16        // Maximum notes in arpeggio (Máximo de notas no arpejo)

// --- Internal Engine Constants (Constantes Internas do Motor) ---
#define ENV_MAX 268435456       // Maximum envelope value (Valor máximo do envelope)

// Definitions for basic waves (using negative numbers for efficiency).
// (Definições para ondas básicas (usando números negativos para eficiência).)
#define W_SINE     -1
#define W_TRI      -2
#define W_SAW      -3
#define W_PULSE    -4
#define W_NOISE    -5


// =================================================================================
// Enums
// =================================================================================

// --- Output Modes (Modos de Saída) ---
enum SynthOutputMode {
    SMODE_PDM,
    SMODE_I2S,
    SMODE_DAC
};

// --- Waveform Types (Tipos de Onda) ---
enum WaveType {
    WAVE_SINE,
    WAVE_TRIANGLE,
    WAVE_SAW,
    WAVE_PULSE,
    WAVE_WAVETABLE,
    WAVE_NOISE,
    WAVE_SAMPLE
};

// --- Bit Depths (Profundidade de Bits) ---
enum BitDepth {
    BITS_4,
    BITS_8,
    BITS_16
};

// --- Envelope States (Estados do Envelope) ---
enum EnvState {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
};

// --- I2S Output Bit Depths (Profundidade de Bits da Saída I2S) ---
enum I2S_Depth {
    I2S_8BIT,   // Lo-Fi Bitcrusher (Hardware 16b, Software Mask)
    I2S_16BIT,  // Standard (Padrão)
    I2S_32BIT   // Hi-Fi (Hardware 32b)
};

// --- Sample Loop Modes (Modos de Loop para Samples) ---
enum LoopMode {
    LOOP_OFF,
    LOOP_FORWARD,   // Left -> Right (Esquerda -> Direita)
    LOOP_PINGPONG,  // Back and Forth (Vai e Volta)
    LOOP_REVERSE    // Right -> Left (Direita -> Esquerda)
};


// =================================================================================
// Structs
// =================================================================================

// --- Sequenced Instrument (Instrumento Sequenciado) ---
struct Instrument {
    // Pointers (4 bytes each on ESP32)
    const uint8_t* seqVolumes;
    const int16_t* seqWaves;
    const uint8_t* relVolumes;
    const int16_t* relWaves;

    // 16-bit values
    uint16_t       seqSpeedMs;
    int16_t        susWave;
    uint16_t       relSpeedMs;
    
    // 8-bit values
    uint8_t        seqLen;
    uint8_t        susVol;
    uint8_t        relLen;

    // Flags
    bool           smoothMorph;
};

// --- Sample Data (Dados do Sample) ---
struct SampleData {
    const int16_t* data;
    uint32_t length;
    uint32_t sampleRate;
    uint32_t rootFreqCentiHz;
};

// --- Sample Zone (Zona de Sample) ---
// Defines a region where a specific sample is used.
// (Define uma região onde um sample específico é usado.)
struct SampleZone {
    uint32_t lowFreq;       // Lower frequency boundary (Limite de frequência inferior)
    uint32_t highFreq;      // Upper frequency boundary (Limite de frequência superior)
    uint16_t sampleId;      // ID of the sample to use (ID do sample a ser usado)
    uint32_t rootOverride;  // Optional root frequency override (Sobrescrita opcional da frequência raiz)
};

// --- Sample-based Instrument (Instrumento Baseado em Sample) ---
struct Instrument_Sample {
    const SampleZone* zones;
    uint8_t           numZones;
    LoopMode          loopMode;
    uint32_t          loopStart;
    uint32_t          loopEnd;    // 0 = end of file (0 = fim do arquivo)
};

// --- Synthesis Voice (Voz de Síntese) ---
// This struct holds the state for a single synthesizer voice.
// (Esta estrutura armazena o estado de uma única voz do sintetizador.)
struct Voice {
    // --- Hot Data (Accessed frequently in render loop) ---
    uint64_t           samplePos1616;
    const void*        wtData;
    Instrument*        inst;
    Instrument_Sample* instSample;
    uint32_t           phase;
    uint32_t           phaseInc;
    uint32_t           currEnvVal;
    int32_t            vibOffset;
    uint32_t           sampleInc1616;

    // --- Control & State Data (Accessed in control loop) ---
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
    uint32_t           slideTicksRemaining;
    uint32_t           slideTicksTotal;
    uint32_t           slideTargetInc;
    uint32_t           slideTargetFreqCenti;
    uint32_t           arpTickCounter;
    uint32_t           sampleLoopStart;
    uint32_t           sampleLoopEnd;

    int32_t            slideDeltaInc;
    int32_t            slideRem;
    int32_t            slideRemAcc;

    uint32_t           arpNotes[MAX_ARP_NOTES];

    int16_t            noiseSample;
    uint16_t           trmDepth;
    uint16_t           trmModGain;
    uint16_t           currWaveId;
    uint16_t           nextWaveId;
    uint16_t           attackMs;
    uint16_t           decayMs;
    uint16_t           releaseMs;
    uint16_t           arpSpeedMs;
    uint16_t           curSampleId;

    uint8_t            vol;
    EnvState           envState;
    WaveType           type;
    BitDepth           depth;
    uint8_t            stageIdx;
    uint8_t            currWaveIsBasic;
    uint8_t            nextWaveIsBasic;
    uint8_t            currWaveType;
    uint8_t            nextWaveType;
    uint8_t            morph;
    uint8_t            arpLen;
    uint8_t            arpIdx;
    LoopMode           sampleLoopMode;

    bool               active;
    bool               slideActive;
    bool               arpActive;
    bool               sampleDirection;
    bool               sampleFinished;
};


// =================================================================================
// ESP32Synth Class Definition
// =================================================================================
class ESP32Synth {
public:
    ESP32Synth();
    ~ESP32Synth();

    // --- Initialization (Inicialização) ---
    bool begin(int dacPin);
    bool begin(int bckPin, int wsPin, int dataPin);
    bool begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth);
    bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth);

    // --- Core Voice Control (Controle Principal da Voz) ---
    void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume);
    void noteOff(uint8_t voice);
    void setFrequency(uint8_t voice, uint32_t freqCentiHz);
    void setVolume(uint8_t voice, uint8_t volume);
    void setWave(uint8_t voice, WaveType type);
    void setPulseWidth(uint8_t voice, uint8_t width);
    void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);

    // --- Modulation (Modulação) ---
    void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);
    void setTremolo(uint8_t voice, uint32_t rateCentiHz, uint16_t depth);
    void slide(uint8_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs);
    void slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs);

    // --- Wavetable Management (Gerenciamento de Wavetable) ---
    void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth);
    void setWavetable(const void* data, uint32_t size, BitDepth depth); // Legacy
    void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth);

    // --- Instrument Control (Controle de Instrumento) ---
    void setInstrument(uint8_t voice, Instrument* inst);
    void setInstrument(uint8_t voice, Instrument_Sample* inst);
    void detachInstrument(uint8_t voice, WaveType newWaveType);

    // --- Sample Control (Controle de Sample) ---
    bool registerSample(uint16_t sampleId, const int16_t* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz);
    void setSample(uint8_t voice, uint16_t sampleId, LoopMode loopMode = LOOP_OFF, uint32_t loopStart = 0, uint32_t loopEnd = 0);
    void setSampleLoop(uint8_t voice, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd);

    // --- Arpeggiator (Arpejador) ---
    template <typename... Args>
    void setArpeggio(uint8_t voice, uint16_t durationMs, Args... freqs);
    void detachArpeggio(uint8_t voice);

    // --- Getters & Status (Obtenção de Informações e Status) ---
    uint32_t getFrequencyCentiHz(uint8_t voice);
    uint8_t  getVolume8Bit(uint8_t voice);
    uint8_t  getEnv8Bit(uint8_t voice);
    uint8_t  getOutput8Bit(uint8_t voice);
    uint32_t getVolumeRaw(uint8_t voice);
    uint32_t getEnvRaw(uint8_t voice);
    uint32_t getOutputRaw(uint8_t voice);
    bool     isVoiceActive(uint8_t voice);
    const char* getChipModel();
    int32_t  getSampleRate();

    // --- System & Miscellaneous (Sistema e Diversos) ---
    void    renderLoop();
    void    setControlRateHz(uint16_t hz);
    void    setMasterVolume(uint8_t volume);
    uint8_t getMasterVolume();


private:
    // --- Private Structs & Data ---
    struct WavetableEntry {
        const void* data;
        uint32_t    size;
        BitDepth    depth;
    };

    Voice          voices[MAX_VOICES];
    WavetableEntry wavetables[MAX_WAVETABLES];
    SampleData     samples[MAX_SAMPLES];

    // --- System State ---
    uint32_t      _sampleRate = 48000;
    I2S_Depth     _i2sDepth = I2S_16BIT;
    uint8_t       _masterVolume = 255;
    uint16_t      controlRateHz = 100;
    uint32_t      controlIntervalSamples;
    uint32_t      controlSampleCounter = 0;

    // --- Hardware Handles ---
    i2s_chan_handle_t tx_handle = NULL;
    #if defined(CONFIG_IDF_TARGET_ESP32)
    dac_continuous_handle_t dac_handle = NULL;
    #endif
    SynthOutputMode currentMode = SMODE_PDM;

    // --- Private Methods ---
    static void audioTask(void* param);
    void render(void* buffer, int samples);
    void processControl();
    int16_t fetchWavetableSample(uint16_t id, uint32_t phase);
};


// =================================================================================
// Template Implementations
// =================================================================================

/**
 * @brief Sets or updates the arpeggiator for a voice.
 *        (Define ou atualiza o arpejador para uma voz.)
 * @tparam ...Args Variable number of frequency arguments. (Número variável de argumentos de frequência.)
 * @param voice The voice to apply the arpeggio to. (A voz para aplicar o arpejo.)
 * @param durationMs Duration of each note in milliseconds. (Duração de cada nota em milissegundos.)
 * @param ...freqs The sequence of frequencies in centi-Hertz. (A sequência de frequências em centi-Hertz.)
 */
template <typename... Args>
void ESP32Synth::setArpeggio(uint8_t voice, uint16_t durationMs, Args... freqs) {
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