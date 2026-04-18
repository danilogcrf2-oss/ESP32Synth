// ESP32Synth.cpp - Implementation
// Se não fosse por Deus, o ESP32Synth nunca teria dado certo, então agradeçam a Ele por essa maravilha de código que é o ESP32Synth. Amém!
#pragma GCC optimize ("O3,unroll-loops")
#include "ESP32Synth.h"

// ====================================================================================
//    SINE WAVE LOOK-UP TABLE
// ====================================================================================
#define SINE_LUT_SIZE 4096
#define SINE_LUT_MASK (SINE_LUT_SIZE - 1)
#define SINE_SHIFT    20

// Shared sine LUT
int16_t sineLUT[SINE_LUT_SIZE];
// Sample storage
SampleData registeredSamples[MAX_SAMPLES];

// ====================================================================================
// == STATIC (PRIVATE) RENDER FUNCTIONS
// ====================================================================================
// Critical path: IRAM_ATTR ensures these run from RAM for speed.
// Pointers are restricted for compiler optimization.
// ====================================================================================

// Macro de alta performance para evitar repetição de código e garantir inlining no loop crítico de samples.
#define ADVANCE_SAMPLE_POS \
    if (dir) { \
        pos += inc; \
        if ((pos >> 16) >= lEnd) { \
            switch (vo->sampleLoopMode) { \
                case LOOP_FORWARD: pos -= ((uint64_t)(lEnd - lStart) << 16); break; \
                case LOOP_PINGPONG: dir = false; pos = ((uint64_t)lEnd << 16) - (pos - ((uint64_t)lEnd << 16)); break; \
                case LOOP_OFF: vo->sampleFinished = true; break; \
                default: break; \
            } \
            if (vo->sampleFinished) break; \
        } \
    } else { \
        if (pos >= inc) pos -= inc; else pos = 0; \
        if ((pos >> 16) <= lStart) { \
            switch (vo->sampleLoopMode) { \
                case LOOP_PINGPONG: dir = true; pos = ((uint64_t)lStart << 16) + (((uint64_t)lStart << 16) - pos); break; \
                case LOOP_REVERSE: pos += ((uint64_t)(lEnd - lStart) << 16); break; \
                case LOOP_OFF: vo->sampleFinished = true; break; \
                default: break; \
            } \
            if (vo->sampleFinished) break; \
        } \
    }

// Render: PCM Sample (Agora suporta 16, 8 e 4 bits com zero overhead no loop interno)
static void IRAM_ATTR renderBlockSample(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->sampleFinished) return;
    const SampleData* sData = &registeredSamples[vo->curSampleId];
    if (!sData->data) return;

    const uint32_t len = sData->length;
    uint64_t pos = vo->samplePos1616;
    const uint32_t inc = vo->sampleInc1616;
    const uint32_t lStart = vo->sampleLoopStart;
    const uint32_t lEnd = (vo->sampleLoopEnd > 0 && vo->sampleLoopEnd <= len) ? vo->sampleLoopEnd : len;
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    bool dir = vo->sampleDirection;

    if (envStep == 0) {
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        
        switch (sData->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    mixBuffer[i] += (data[(uint32_t)(pos >> 16)] * finalVol) >> 16;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    mixBuffer[i] += ((((int16_t)data[(uint32_t)(pos >> 16)] - 128) << 8) * finalVol) >> 16;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx = (uint32_t)(pos >> 16);
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
        }
    } else {
        switch (sData->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += (data[(uint32_t)(pos >> 16)] * finalVol) >> 16;
                    currentEnv += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += ((((int16_t)data[(uint32_t)(pos >> 16)] - 128) << 8) * finalVol) >> 16;
                    currentEnv += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)sData->data;
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    uint32_t idx = (uint32_t)(pos >> 16);
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    currentEnv += envStep;
                    ADVANCE_SAMPLE_POS
                }
                break;
            }
        }
    }
    vo->samplePos1616 = pos;
    vo->sampleDirection = dir;
}

// Render: Wavetable
static void IRAM_ATTR renderBlockWavetable(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (!vo->wtData) return;

    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    const uint32_t size = vo->wtSize;

    if (envStep == 0) {
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        if (finalVol == 0) { vo->phase += inc * samples; return; } // Economia bruta em vozes silenciosas!

        switch (vo->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    mixBuffer[i] += (data[(uint32_t)(((uint64_t) ph * size) >> 32)] * finalVol) >> 16;
                    ph += inc;
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    mixBuffer[i] += ((((int16_t)data[(uint32_t)(((uint64_t) ph * size) >> 32)] - 128) << 8) * finalVol) >> 16;
                    ph += inc;
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx = (uint32_t)(((uint64_t) ph * size) >> 32);
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ph += inc;
                }
                break;
            }
        }
    } else {
        switch (vo->depth) {
            case BITS_16: {
                const int16_t* data = (const int16_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += (data[(uint32_t)(((uint64_t) ph * size) >> 32)] * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            }
            case BITS_8: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += ((((int16_t)data[(uint32_t)(((uint64_t) ph * size) >> 32)] - 128) << 8) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            }
            case BITS_4: {
                const uint8_t* data = (const uint8_t*)vo->wtData;
                for (int i = 0; i < samples; i++) {
                    uint32_t idx = (uint32_t)(((uint64_t) ph * size) >> 32);
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += ((((int16_t)((data[idx >> 1] >> ((idx & 1) << 2)) & 0x0F) - 8) * 4096) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            }
        }
    }
    vo->phase = ph;
}

// Render: Basic Oscillators (Saw, Sine, Pulse, Tri)
static void IRAM_ATTR renderBlockBasic(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    const WaveType type = vo->type;
    const uint32_t pw = vo->pulseWidth;

    if (envStep == 0) {
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        if (finalVol == 0) { vo->phase += inc * samples; return; }

        switch (type) {
            case WAVE_SAW:
                for (int i = 0; i < samples; i++) { mixBuffer[i] += ((int16_t)(ph >> 16) * finalVol) >> 16; ph += inc; }
                break;
            case WAVE_SINE:
                for (int i = 0; i < samples; i++) { mixBuffer[i] += (sineLUT[ph >> SINE_SHIFT] * finalVol) >> 16; ph += inc; }
                break;
            case WAVE_PULSE:
                for (int i = 0; i < samples; i++) { mixBuffer[i] += (((ph < pw) ? 32767 : -32767) * finalVol) >> 16; ph += inc; }
                break;
            case WAVE_TRIANGLE:
                for (int i = 0; i < samples; i++) {
                    int16_t saw = (int16_t)(ph >> 16);
                    mixBuffer[i] += ((int16_t)(((saw ^ (saw >> 15)) * 2) - 32767) * finalVol) >> 16;
                    ph += inc;
                }
                break;
            default: break;
        }
    } else {
        switch (type) {
            case WAVE_SAW:
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += ((int16_t)(ph >> 16) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            case WAVE_SINE:
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += (sineLUT[ph >> SINE_SHIFT] * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            case WAVE_PULSE:
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    mixBuffer[i] += (((ph < pw) ? 32767 : -32767) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            case WAVE_TRIANGLE:
                for (int i = 0; i < samples; i++) {
                    int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
                    int16_t saw = (int16_t)(ph >> 16);
                    mixBuffer[i] += ((int16_t)(((saw ^ (saw >> 15)) * 2) - 32767) * finalVol) >> 16;
                    ph += inc; currentEnv += envStep;
                }
                break;
            default: break;
        }
    }
    vo->phase = ph;
}

// Render: Noise
static void IRAM_ATTR renderBlockNoise(Voice* __restrict vo, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t rng = vo->rngState;
    uint32_t ph = vo->phase;
    uint32_t inc = (vo->phaseInc + vo->vibOffset) << 4;
    int16_t currentSample = vo->noiseSample;

    if (envStep == 0) {
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        if (finalVol == 0) { vo->phase += inc * samples; return; }

        for (int i = 0; i < samples; i++) {
            uint32_t nextPh = ph + inc;
            if (nextPh < ph) { rng = (rng * 1664525) + 1013904223; currentSample = (int16_t)(rng >> 16); }
            ph = nextPh;
            mixBuffer[i] += (currentSample * finalVol) >> 16;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            uint32_t nextPh = ph + inc;
            if (nextPh < ph) { rng = (rng * 1664525) + 1013904223; currentSample = (int16_t)(rng >> 16); }
            ph = nextPh;
            int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
            mixBuffer[i] += (currentSample * finalVol) >> 16;
            currentEnv += envStep;
        }
    }
    vo->rngState = rng;
    vo->phase = ph;
    vo->noiseSample = currentSample;
}

// Render: Stream from RAM Buffer
static void IRAM_ATTR renderBlockStream(Voice* __restrict vo, StreamTrack* __restrict streamsArr, int32_t* __restrict mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->streamTrackId < 0 || vo->streamTrackId >= MAX_STREAMS) return;
    StreamTrack* trk = &streamsArr[vo->streamTrackId];
    if (!trk->playing) return;

    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t inc = vo->sampleInc1616;
    uint32_t accum = vo->streamFracAccum;
    uint16_t tail = trk->tail;
    uint16_t head = trk->head;

    if (envStep == 0) {
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        for (int i = 0; i < samples; i++) {
            accum += inc;
            uint32_t stepsToConsume = accum >> 16;
            accum &= 0xFFFF;

            if (stepsToConsume > 0) {
                uint16_t available = (STREAM_BUF_SAMPLES + head - tail) & STREAM_BUF_MASK;
                if (stepsToConsume > available) stepsToConsume = available;
                tail = (tail + stepsToConsume) & STREAM_BUF_MASK;
                trk->samplesPlayed += stepsToConsume;
            }

            int16_t val1 = trk->buffer[tail];
            int16_t val2 = trk->buffer[(tail + 1) & STREAM_BUF_MASK];
            int32_t interp = val1 + (((val2 - val1) * (int32_t)(accum >> 1)) >> 15);
            mixBuffer[i] += (interp * finalVol) >> 16;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            accum += inc;
            uint32_t stepsToConsume = accum >> 16;
            accum &= 0xFFFF;

            if (stepsToConsume > 0) {
                uint16_t available = (STREAM_BUF_SAMPLES + head - tail) & STREAM_BUF_MASK;
                if (stepsToConsume > available) stepsToConsume = available;
                tail = (tail + stepsToConsume) & STREAM_BUF_MASK;
                trk->samplesPlayed += stepsToConsume;
            }

            int16_t val1 = trk->buffer[tail];
            int16_t val2 = trk->buffer[(tail + 1) & STREAM_BUF_MASK];
            int32_t interp = val1 + (((val2 - val1) * (int32_t)(accum >> 1)) >> 15);

            int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
            mixBuffer[i] += (interp * finalVol) >> 16;
            currentEnv += envStep;
        }
    }
    vo->streamFracAccum = accum;
    trk->tail = tail;
}

// Envelope Logic (ADSR)
static inline void IRAM_ATTR updateAdsrBlock(Voice* vo, int samples, int32_t& startEnv, int32_t& envStep) {
    // Instruments handle their own envelope
    if (vo->inst) {
        startEnv = ENV_MAX;
        vo->currEnvVal = ENV_MAX;
        envStep = 0;
        return;
    }

    startEnv = vo->currEnvVal;

    if (vo->envState == ENV_IDLE || vo->envState == ENV_SUSTAIN) {
        envStep = 0;
        return;
    }

    uint32_t target = vo->currEnvVal;
    uint32_t steps = (uint32_t)samples;
    uint8_t shift = 31 - __builtin_clz(steps); // O Segredo: Fast log2 instantâneo (só funciona com potência de 2)

    switch (vo->envState) {
    case ENV_ATTACK:
        if (vo->rateAttack >= ENV_MAX) { // Instant attack
            target = ENV_MAX;
            vo->envState = ENV_DECAY;
            startEnv = ENV_MAX;
            envStep = 0;
        } else if (ENV_MAX - target > vo->rateAttack * steps) {
            target += vo->rateAttack * steps;
            envStep = ((int32_t)target - (int32_t)startEnv) >> shift;
        } else {
            target = ENV_MAX;
            vo->envState = ENV_DECAY;
            envStep = ((int32_t)target - (int32_t)startEnv) >> shift;
        }
        break;
    case ENV_DECAY:
        if (vo->rateDecay >= ENV_MAX) { // Instant decay
            target = vo->levelSustain;
            vo->envState = ENV_SUSTAIN;
            startEnv = target;
            envStep = 0;
        } else if (target > vo->levelSustain && (target - vo->levelSustain) > vo->rateDecay * steps) {
            target -= vo->rateDecay * steps;
            envStep = -((int32_t)vo->rateDecay);
        } else {
            target = vo->levelSustain;
            vo->envState = ENV_SUSTAIN;
            envStep = ((int32_t)target - (int32_t)startEnv) >> shift;
        }
        break;
    case ENV_RELEASE:
        if (vo->rateRelease >= ENV_MAX) { // Instant release
            target = 0;
            vo->active = false;
            vo->envState = ENV_IDLE;
            startEnv = 0;
            envStep = 0;
        } else if (target > vo->rateRelease * steps) {
            target -= vo->rateRelease * steps;
            envStep = -((int32_t)vo->rateRelease);
        } else {
            target = 0;
            vo->active = false;
            vo->envState = ENV_IDLE;
            envStep = -(startEnv >> shift);
            if (envStep == 0 && startEnv > 0) envStep = -1; // Ensure it reaches zero
        }
        break;
    default:
        envStep = 0;
        break;
    }

    vo->currEnvVal = target;
}


// ====================================================================================
// == PUBLIC METHODS
// ====================================================================================

// --- Constructor & Destructor ---

ESP32Synth::ESP32Synth() {
    _sampleRate = 48000;

    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i] = {}; 
        voices[i].type = WAVE_SINE;
        voices[i].envState = ENV_IDLE;
        voices[i].rngState = 12345 + (i * 999); // Unique RNG seed per voice
        voices[i].rateAttack = ENV_MAX;
        voices[i].levelSustain = ENV_MAX;
        voices[i].rateRelease = ENV_MAX;
        voices[i].pulseWidth = 0x80000000;
        voices[i].streamTrackId = -1;
        voices[i].customWaveFunc = nullptr;
    }

    for (int i = 0; i < MAX_STREAMS; i++) {
        streams[i] = {};
        streams[i].seekTarget = -1;
    }

    for (uint16_t i = 0; i < MAX_WAVETABLES; i++) {
        wavetables[i] = {};
        wavetables[i].depth = BITS_8;
    }

    controlRateHz = 100;
}

ESP32Synth::~ESP32Synth() {
    end();
}

void ESP32Synth::end() {
    // 1. Sinaliza para as Tasks abortarem
    this->_running = false;

    // 2. Silencia vozes (evita estalos)
    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active = false;
        voices[i].envState = ENV_IDLE;
        voices[i].streamTrackId = -1;
    }

    // 3. Aguarda as tasks saírem do hardware.
    // Isso evita o maldito "Mutex Deadlock" do ESP-IDF!
    while (audioTaskHandle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    while (streamTaskHandle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    // 4. Limpa streams
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (streams[i].active) {
            streams[i].playing = false;
            streams[i].active = false;
            if (streams[i].file) streams[i].file.close();
        }
    }

    // 5. Tasks morreram -> Desliga hardware com segurança bruta!
    if (tx_handle != NULL) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }
    #if defined(CONFIG_IDF_TARGET_ESP32)
    if (dac_handle != NULL) {
        dac_continuous_disable(dac_handle);
        dac_continuous_del_channels(dac_handle);
        dac_handle = NULL;
    }
    #endif

    // 6. Resetar fisicamente os pinos!
    // Sem isso, o pino continuava roteado ao periférico anterior (ex: vazando I2S no DAC).
    if (_dataPin >= 0) gpio_reset_pin((gpio_num_t)_dataPin);
    if (_bckPin >= 0) gpio_reset_pin((gpio_num_t)_bckPin);
    if (_wsPin >= 0) gpio_reset_pin((gpio_num_t)_wsPin);
    
    // Zera a memória de pinos para o próximo ciclo
    _dataPin = -1;
    _bckPin = -1;
    _wsPin = -1;
}

// --- Initialization & System ---

bool ESP32Synth::begin(int dacPin) {
    return begin(dacPin, SMODE_DAC, -1, -1, I2S_16BIT);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin) {
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, I2S_16BIT);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth) {
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, i2sDepth);
}

bool ESP32Synth::begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth) {
    end(); // Ensures a clean state and prevents resource leaks
    
    this->currentMode = mode;
    this->_i2sDepth = i2sDepth;
    this->_dataPin = dataPin;
    this->_bckPin = clkPin;
    this->_wsPin = wsPin;

    // --- Proteção de hardware extrema. DAC e PDM têm limitação de árvore de clock interno. ---
    if (mode == SMODE_DAC) {
        _sampleRate = 48000;
        _customSampleRate = false; // Força ignorar qualquer gambiarra pra não explodir o DAC
    } else if (mode == SMODE_PDM) {
        #if defined(CONFIG_IDF_TARGET_ESP32S3)
        _sampleRate = 52036; // PDM no S3 tem restrição física de divisor de clock
        #else
        _sampleRate = 48000;
        #endif
        _customSampleRate = false; // Força ignorar também
    } else if (!_customSampleRate) {
        _sampleRate = 48000; // Padrão I2S caso usuário não tenha mexido
    }
    // resumindo, só podemos mudar o sample rate real no modo I2S. O SMODE_DAC e SMODE_PDM são só 48khz.

    controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;

    // Init sine LUT
    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        sineLUT[i] = (int16_t)(sin(i * 2.0 * PI / (double) SINE_LUT_SIZE) * 32767.0);
    }

    if (mode == SMODE_DAC) {
        #if !defined(CONFIG_IDF_TARGET_ESP32)
        return false; // DAC output is only supported on the original ESP32
        #else
        dac_continuous_config_t cont_cfg = {
            .chan_mask = (dataPin == 26) ? DAC_CHANNEL_MASK_CH1 : DAC_CHANNEL_MASK_CH0,
            .desc_num = 8, .buf_size = 2048, 
            // O Segredo: Pedimos 96kHz para o ESP-IDF nos dar 48kHz reais no modo SIMUL!
            .freq_hz = _sampleRate * 2, 
            .offset = 0, .clk_src = DAC_DIGI_CLK_SRC_DEFAULT, .chan_mode = DAC_CHANNEL_MODE_SIMUL,
        };
        if (dac_continuous_new_channels(&cont_cfg, &dac_handle) != ESP_OK) return false;
        if (dac_continuous_enable(dac_handle) != ESP_OK) return false;
        #endif
    } else { // I2S or PDM
        i2s_port_t requested_port = (mode == SMODE_PDM) ? I2S_NUM_0 : I2S_NUM_AUTO;
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(requested_port, I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num = 8;
        chan_cfg.dma_frame_num = 512;

        if (i2s_new_channel(&chan_cfg, &tx_handle, NULL) != ESP_OK) return false;

        if (mode == SMODE_PDM) {
            i2s_pdm_tx_config_t pdm_cfg = {
                .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(_sampleRate),
                .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
                .gpio_cfg = { .clk = (gpio_num_t) clkPin, .dout = (gpio_num_t) dataPin }
            };
            if (i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_cfg) != ESP_OK) return false;
        } else { // I2S
            i2s_data_bit_width_t width = (i2sDepth == I2S_32BIT) ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT;
            i2s_std_config_t std_cfg = {
                .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sampleRate),
                .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, I2S_SLOT_MODE_STEREO),
                .gpio_cfg = { .mclk = I2S_GPIO_UNUSED, .bclk = (gpio_num_t) clkPin, .ws = (gpio_num_t) wsPin, .dout = (gpio_num_t) dataPin }
            };
            if (i2s_channel_init_std_mode(tx_handle, &std_cfg) != ESP_OK) return false;
        }

        if (i2s_channel_enable(tx_handle) != ESP_OK) return false;
    }

    this->_running = true; 

    // Task de Áudio (Core 1)
    if (xTaskCreatePinnedToCore(audioTask, "SynthTask", 4096, this, configMAX_PRIORITIES - 1, &audioTaskHandle, 1) != pdPASS) {
        return false;
    }
    
    return true;
}
void ESP32Synth::setSampleRate(uint32_t rate) {
    // AVISO: O ESP32Synth foi matematicamente projetado, altamente otimizado 
    // e exaustivamente testado em 48kHz. Alterar isso pode causar jitter ou aliasing.
    // Mude por sua conta e risco.
    if (rate > 0 && rate != _sampleRate) {
        _sampleRate = rate;
        _customSampleRate = true;
        controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;
        
        // Recalcula o step do motor de fase de todas as notas que já estão tocando!
        for (int v = 0; v < MAX_VOICES; v++) {
            if (voices[v].active) {
                setFrequency(v, voices[v].freqVal); 
            }
        }
    }
}
void ESP32Synth::setControlRateHz(uint16_t hz) {
    if (hz > 0) {
        controlRateHz = hz;
        controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;
    }
}

void ESP32Synth::setMasterBitcrush(uint8_t bits) {
    // Limita entre 0 (desligado) e 32 bits
    if (bits > 32) bits = 32;
    _bitcrush = bits;
}

void ESP32Synth::setVolDepthBase(uint8_t bits) {
    if (bits < 1) bits = 1;
    if (bits > 16) bits = 16;
    _volShift = 16 - bits; // Calcula automaticamente o Shift necessário para alcançar 16-bits internamente.
}

void ESP32Synth::setMasterVolume(uint16_t volume) {
    _masterVolume = volume << _volShift; // Já armazena mastigado em 16-bits
}

void ESP32Synth::setCustomDSP(SynthDSPCallback dspFunc) {
    _customDSP = dspFunc;
}

void ESP32Synth::setCustomControl(SynthControlCallback ctrlFunc) {
    _customControl = ctrlFunc;
}

const char* ESP32Synth::getChipModel() {
    return SYNTH_CHIP;
}

int32_t ESP32Synth::getSampleRate() {
    return _sampleRate;
}

// --- Core Voice Control ---

void ESP32Synth::noteOn(uint16_t voice, uint32_t freqCentiHz, uint16_t volume) {
    if (voice >= MAX_VOICES) return;
    Voice* vo = &voices[voice];

    vo->freqVal = freqCentiHz;
    vo->vol = volume << _volShift;
    vo->active = true;

    // Calc phase increment
    vo->phaseInc = (uint32_t)(((uint64_t) freqCentiHz << 32) / (_sampleRate * 100));

    // Update logic immediately
    controlSampleCounter = controlIntervalSamples;

    if (vo->inst) { // Tracker Instrument
        vo->envState = ENV_ATTACK;
        vo->currEnvVal = ENV_MAX; 
        vo->stageIdx = 0;
        vo->controlTick = 0;
        vo->phase = (uint32_t)vo->startPhase * 11930465UL; 
        processControl(); 
    } else if (vo->instSample) { // Sample-based Instrument
        vo->type = WAVE_SAMPLE;
        vo->envState = ENV_ATTACK;
        vo->currEnvVal = ENV_MAX;
        vo->sampleFinished = false;
        vo->sampleLoopMode = vo->instSample->loopMode;
        vo->sampleLoopStart = vo->instSample->loopStart;
        vo->sampleDirection = (vo->sampleLoopMode != LOOP_REVERSE);

        const SampleData* sData = nullptr;
        uint32_t root = 0;
        // Find sample zone
        for (int i = 0; i < vo->instSample->numZones; i++) {
            const SampleZone* z = &vo->instSample->zones[i];
            if (freqCentiHz >= z->lowFreq && freqCentiHz <= z->highFreq) {
                if (z->sampleId < MAX_SAMPLES) {
                    sData = &registeredSamples[z->sampleId];
                    vo->curSampleId = z->sampleId;
                    root = (z->rootOverride > 0) ? z->rootOverride : sData->rootFreqCentiHz;
                }
                break;
            }
        }

        if (sData) {
            vo->sampleLoopEnd = (vo->instSample->loopEnd == 0 || vo->instSample->loopEnd > sData->length) ? sData->length : vo->instSample->loopEnd;
            uint32_t startOffset = (sData->length * vo->startPhase) / 360;
            vo->samplePos1616 = (vo->sampleDirection) ? ((uint64_t)startOffset << 16) : ((uint64_t)(sData->length - startOffset) << 16);
        } else {
            vo->samplePos1616 = 0;
        }
        
        // Calc increment
        if (sData && sData->data && root > 0) {
            float pitchRatio = (float) freqCentiHz / (float) root;
            float rateRatio = (float)sData->sampleRate / (float)_sampleRate;
            vo->sampleInc1616 = (uint32_t)(pitchRatio * rateRatio * 65536.0f);
        } else {
            vo->sampleInc1616 = 0;
        }

    } else { // Standard voice types
        if (vo->type == WAVE_NOISE) {
            vo->rngState += micros(); // Re-seed
        } else if (vo->type == WAVE_SAMPLE) {
            vo->sampleFinished = false;
            const SampleData* sData = &registeredSamples[vo->curSampleId];
            vo->sampleDirection = (vo->sampleLoopMode != LOOP_REVERSE);

            if (sData->data && sData->length > 0) {
                uint32_t startOffset = (sData->length * vo->startPhase) / 360;
                vo->samplePos1616 = (vo->sampleDirection) ? ((uint64_t)startOffset << 16) : ((uint64_t)(sData->length - startOffset) << 16);
                if (sData->rootFreqCentiHz > 0) {
                    uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / sData->rootFreqCentiHz;
                    vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
                } else vo->sampleInc1616 = 0;
            } else {
                vo->samplePos1616 = 0;
                vo->sampleInc1616 = 0;
            }
        } else if (vo->type == WAVE_STREAM && vo->streamTrackId >= 0) {
            StreamTrack* trk = &this->streams[vo->streamTrackId];
            trk->seekTarget = 0; // Seek to beginning
            trk->playing = true;
            vo->streamFracAccum = 0;
            
            if (trk->rootFreqCentiHz > 0) {
                uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / trk->rootFreqCentiHz;
                vo->sampleInc1616 = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);
            } else vo->sampleInc1616 = 65536; // 1.0x playback speed
            
        } else { // Basic waveforms
            vo->phase = (uint32_t)vo->startPhase * 11930465UL;
        }

        if (vo->arpActive) {
            vo->arpIdx = 0;
            vo->arpTickCounter = 0;
        }
        
        // Trigger ADSR
        if (vo->rateAttack >= ENV_MAX) { // Zero attack time
            vo->currEnvVal = ENV_MAX;
            vo->envState = ENV_DECAY;
        } else {
            vo->currEnvVal = 0;
            vo->envState = ENV_ATTACK;
        }
    }
}

void ESP32Synth::noteOff(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].active) {
        voices[voice].envState = ENV_RELEASE;
        voices[voice].stageIdx = 0; // Reset instrument stage index
        voices[voice].controlTick = 0;
    }
}

void ESP32Synth::setFrequency(uint16_t voice, uint32_t freqCentiHz) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->freqVal = freqCentiHz;

    // Recalc increment
    if (v->type == WAVE_SAMPLE && v->instSample == nullptr) {
        const SampleData* sData = &registeredSamples[v->curSampleId];
        if (sData->data && sData->rootFreqCentiHz > 0) {
            uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / sData->rootFreqCentiHz;
            v->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
        }
    } else if (v->type == WAVE_STREAM && v->streamTrackId >= 0) {
        StreamTrack* trk = &streams[v->streamTrackId];
        if (trk->rootFreqCentiHz > 0) {
            uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / trk->rootFreqCentiHz;
            v->sampleInc1616 = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);
        }
    } else {
        v->phaseInc = (uint32_t)(((uint64_t)freqCentiHz * 4294967296ULL) / (_sampleRate * 100ULL));
    }
}

void ESP32Synth::setVolume(uint16_t voice, uint16_t volume) {
    if (voice < MAX_VOICES) voices[voice].vol = volume << _volShift;
}

void ESP32Synth::setWave(uint16_t voice, WaveType type) {
    if (voice < MAX_VOICES) voices[voice].type = type;
}

void ESP32Synth::setPulseWidthBitDepth(uint8_t bits) {
    if (bits < 1) bits = 1;
    if (bits > 32) bits = 32;
    _pwShift = 32 - bits; // Calcula automaticamente o Shift necessário
}

void ESP32Synth::setPulseWidth(uint16_t voice, uint32_t width) {
    // Shift bit-a-bit para converter pra escala 32-bit de fase do motor! O(1) de processamento.
    if (voice < MAX_VOICES) voices[voice].pulseWidth = width << _pwShift;
}

void ESP32Synth::setCustomWave(uint16_t voice, SynthCustomWaveCallback cb) {
    if (voice < MAX_VOICES) {
        voices[voice].customWaveFunc = cb;
        voices[voice].type = WAVE_CUSTOM; // Força automaticamente o tipo!
    }
}

// --- Envelope ---

void ESP32Synth::setEnv(uint16_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->levelSustain = (uint32_t)s * (ENV_MAX / 255);
    uint32_t samplesPerMs = _sampleRate / 1000;
    
    // Calculate rate of change; ENV_MAX means "instant"
    v->rateAttack = (a == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)a * samplesPerMs);
    v->rateDecay = (d == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)d * samplesPerMs);
    v->rateRelease = (r == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)r * samplesPerMs);
    v->attackMs = a;
    v->decayMs = d;
    v->releaseMs = r;
}

// --- Phase Control ---

void ESP32Synth::setStartPhase(uint16_t voice, uint16_t phaseDegrees) {
    if (voice < MAX_VOICES) {
        voices[voice].startPhase = phaseDegrees % 360;
    }
}

void ESP32Synth::setCurrentPhase(uint16_t voice, uint16_t phaseDegrees) {
    if (voice >= MAX_VOICES) return;
    Voice* vo = &voices[voice];
    uint32_t deg = phaseDegrees % 360;

    if (vo->type == WAVE_SAMPLE) {
        const SampleData* sData = &registeredSamples[vo->curSampleId];
        if (sData && sData->length > 0) {
            uint32_t offset = (sData->length * deg) / 360;
            vo->samplePos1616 = (uint64_t)offset << 16;
        }
    } else if (vo->type != WAVE_NOISE) {
        vo->phase = (uint32_t)deg * 11930465UL; // 11930465 ~= (2^32)/360
    }
}

// --- Modulation & Slides ---

void ESP32Synth::setVibrato(uint16_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz) {
    if (voice >= MAX_VOICES) return;
    // Cálculo preciso que se adapta 100% ao Sample Rate atual!
    voices[voice].vibRateInc = (uint32_t)(((uint64_t)rateCentiHz * 4294967296ULL) / ((uint64_t)_sampleRate * 100ULL));
    voices[voice].vibDepthInc = (uint32_t)(((uint64_t)depthCentiHz * 4294967296ULL) / ((uint64_t)_sampleRate * 100ULL));
}

void ESP32Synth::setVibratoPhase(uint16_t voice, uint16_t phaseDegrees) {
    if (voice < MAX_VOICES) {
        voices[voice].vibPhase = (uint32_t)(phaseDegrees % 360) * 11930465UL;
    }
}

void ESP32Synth::setTremolo(uint16_t voice, uint32_t rateCentiHz, uint16_t depth) {
    if (voice >= MAX_VOICES) return;
    voices[voice].trmRateInc = (uint32_t)(((uint64_t)rateCentiHz * 4294967296ULL) / ((uint64_t)_sampleRate * 100ULL));
    voices[voice].trmDepth = depth;
}

void ESP32Synth::setTremoloPhase(uint16_t voice, uint16_t phaseDegrees) {
    if (voice < MAX_VOICES) {
        voices[voice].trmPhase = (uint32_t)(phaseDegrees % 360) * 11930465UL;
    }
}

void ESP32Synth::slideFreq(uint16_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    uint32_t ticks = (durationMs == 0) ? 0 : ((durationMs * controlRateHz + 999) / 1000);

    uint32_t endInc = (uint32_t)(((uint64_t) endFreqCentiHz << 32) / (_sampleRate * 100));

    if (ticks == 0) {
        v->phaseInc = endInc;
        v->freqVal = endFreqCentiHz;
        v->slideFreqActive = false;
        return;
    }

    uint32_t startInc = (uint32_t)(((uint64_t) startFreqCentiHz << 32) / (_sampleRate * 100));
    // Bresenham's algo for integer slides
    int64_t diff = (int64_t) endInc - (int64_t) startInc;
    int32_t delta = (int32_t)(diff / (int64_t)ticks);
    int64_t rem = diff - (int64_t)delta * (int64_t)ticks;

    v->phaseInc = startInc;
    v->slideFreqDeltaInc = delta;
    v->slideFreqRem = (int32_t)rem;
    v->slideFreqRemAcc = 0;
    v->slideFreqTicksTotal = ticks;
    v->slideFreqTicksRemaining = ticks;
    v->slideFreqTargetInc = endInc;
    v->slideFreqTargetCenti = endFreqCentiHz;
    v->slideFreqActive = true;
    v->freqVal = startFreqCentiHz;
}

void ESP32Synth::slideFreqTo(uint16_t voice, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    uint32_t start = voices[voice].freqVal;
    if (start == 0) { // If current frequency is 0, calculate from phaseInc
        start = (voices[voice].phaseInc != 0) ? (uint32_t)(((uint64_t)voices[voice].phaseInc * _sampleRate * 100) >> 32) : endFreqCentiHz;
    }
    slideFreq(voice, start, endFreqCentiHz, durationMs);
}

void ESP32Synth::slideVolAbsolute(uint16_t voice, uint16_t startVol16, uint16_t endVol16, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    uint32_t ticks = (durationMs == 0) ? 0 : ((durationMs * controlRateHz + 999) / 1000);

    if (ticks == 0) {
        v->vol = endVol16;
        v->slideVolActive = false;
        return;
    }

    v->vol = startVol16;
    v->slideVolCurr = (int64_t)startVol16 << 16; // 16 bits de precisão fracionária!
    v->slideVolTarget = endVol16;
    
    int64_t diff = ((int64_t)endVol16 << 16) - v->slideVolCurr;
    v->slideVolInc = diff / (int64_t)ticks;
    
    v->slideVolTicksRemaining = ticks;
    v->slideVolActive = true;
}

void ESP32Synth::slideVol(uint16_t voice, uint16_t startVol, uint16_t endVol, uint32_t durationMs) {
    // Converte a resolução externa escolhida pelo usuário e joga pra engine absoluta
    slideVolAbsolute(voice, startVol << _volShift, endVol << _volShift, durationMs);
}

void ESP32Synth::slideVolTo(uint16_t voice, uint16_t endVol, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    slideVolAbsolute(voice, voices[voice].vol, endVol << _volShift, durationMs);
}

// --- Wavetable & Instruments ---

void ESP32Synth::setWavetable(uint16_t voice, const void* data, uint32_t size, BitDepth depth) {
    if (voice < MAX_VOICES) {
        voices[voice].wtData = data;
        voices[voice].wtSize = size;
        voices[voice].depth = depth;
    }
}

void ESP32Synth::registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth) {
    if (id < MAX_WAVETABLES) {
        wavetables[id].data = data;
        wavetables[id].size = size;
        wavetables[id].depth = depth;
    }
}

void ESP32Synth::setInstrument(uint16_t voice, Instrument* inst) {
    if (voice >= MAX_VOICES) return;
    voices[voice].inst = inst;
    voices[voice].instSample = nullptr;
    voices[voice].stageIdx = 0;
    voices[voice].controlTick = 0;
    voices[voice].currEnvVal = (inst == nullptr) ? 0 : ENV_MAX;
    voices[voice].envState = (inst == nullptr) ? ENV_IDLE : ENV_ATTACK;
}

void ESP32Synth::setInstrument(uint16_t voice, Instrument_Sample* inst) {
    if (voice >= MAX_VOICES) return;
    voices[voice].instSample = inst;
    voices[voice].inst = nullptr;
    voices[voice].currEnvVal = 0;
    voices[voice].envState = ENV_IDLE;
}

void ESP32Synth::detachInstrument(uint16_t voice, WaveType newWaveType) {
    if (voice >= MAX_VOICES) return;
    setInstrument(voice, (Instrument*)nullptr);
    setWave(voice, newWaveType);
}

// --- Sample Control ---

bool ESP32Synth::registerSample(uint16_t sampleId, const void* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz, BitDepth depth) {
    if (sampleId >= MAX_SAMPLES || data == nullptr) return false;

    registeredSamples[sampleId].data = data;
    registeredSamples[sampleId].length = length;
    registeredSamples[sampleId].sampleRate = sampleRate;
    registeredSamples[sampleId].rootFreqCentiHz = rootFreqCentiHz;
    registeredSamples[sampleId].depth = depth;
    return true;
}

void ESP32Synth::setSample(uint16_t voice, uint16_t sampleId, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd) {
    if (voice >= MAX_VOICES || sampleId >= MAX_SAMPLES) return;
    Voice* v = &voices[voice];
    v->type = WAVE_SAMPLE;
    v->curSampleId = sampleId;
    v->sampleLoopMode = loopMode;
    v->sampleLoopStart = loopStart;
    v->sampleLoopEnd = loopEnd;
    v->instSample = nullptr; // Detach sample instrument if any
}

void ESP32Synth::setSampleLoop(uint16_t voice, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd) {
    if (voice < MAX_VOICES) {
        Voice* v = &voices[voice];
        v->sampleLoopMode = loopMode;
        v->sampleLoopStart = loopStart;
        v->sampleLoopEnd = loopEnd;
    }
}

// --- Arpeggiator ---

void ESP32Synth::detachArpeggio(uint16_t voice) {
    if (voice < MAX_VOICES) voices[voice].arpActive = false;
}

// --- SD Streaming ---

int8_t ESP32Synth::setupStream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz, bool loop) {
    if (voice >= MAX_VOICES) return -1;

    // Start SD Task on-demand
    if (streamTaskHandle == NULL) {
        xTaskCreatePinnedToCore(sdLoaderTask, "SynthSDTask", 4096, this, 1, &streamTaskHandle, 0);
    }

    // Clear existing
    if (voices[voice].streamTrackId >= 0) {
        stopStream(voice);
    }
    
    // Find free stream
    int8_t streamId = -1;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!streams[i].active) { streamId = i; break; }
    }
    if (streamId == -1) return -1; // No free stream tracks

    fs::File file = fs.open(path, "r");
    if (!file) return -1;

    uint32_t sRate, dPos, dSize;
    uint16_t channels, bits;
    
    if (!parseWavHeader(file, sRate, dPos, dSize, channels, bits)) {
        file.close();
        return -1;
    }
    file.seek(dPos);

    StreamTrack* trk = &streams[streamId];
    *trk = {}; // Clear stream track before use
    
    trk->file = file;
    trk->sampleRate = sRate;
    trk->dataStartPos = dPos;
    trk->dataSize = dSize;
    trk->numChannels = channels;
    trk->bitsPerSample = bits;
    trk->loop = loop;
    trk->seekTarget = -1;
    trk->loopStartBytes = dPos;
    trk->loopEndBytes = dPos + dSize;
    trk->rootFreqCentiHz = rootFreqCentiHz;
    trk->active = true;

    Voice* vo = &voices[voice];
    vo->type = WAVE_STREAM;
    vo->streamTrackId = streamId;
    vo->active = false;
    vo->envState = ENV_IDLE;
    vo->currEnvVal = 0;

    return streamId;
}

int8_t ESP32Synth::playStream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
    if (voice >= MAX_VOICES) return -1;
    
    int8_t streamId = setupStream(voice, fs, path, rootFreqCentiHz, loop);
    if(streamId < 0) return -1;
    
    StreamTrack* trk = &streams[streamId];
    trk->playing = true;
    
    // Wait a moment for the buffer to pre-fill
    int timeout = 100;
    while (trk->head == trk->tail && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    Voice* vo = &voices[voice];
    vo->vol = volume << _volShift; 
    
    if (vo->freqVal == 0) vo->freqVal = rootFreqCentiHz;
    uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / rootFreqCentiHz;
    vo->sampleInc1616 = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);

    // Start ADSR envelope
    if (vo->rateAttack >= ENV_MAX) {
        vo->currEnvVal = ENV_MAX;
        vo->envState = ENV_DECAY;
    } else {
        vo->currEnvVal = 0;
        vo->envState = ENV_ATTACK;
    }
    vo->active = true;

    return streamId;
}

void ESP32Synth::pauseStream(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        streams[voices[voice].streamTrackId].playing = false;
    }
}

void ESP32Synth::resumeStream(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        streams[voices[voice].streamTrackId].playing = true;
    }
}

void ESP32Synth::stopStream(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        
        trk->playing = false;
        trk->active = false;
        
        if (trk->file) trk->file.close();
        voices[voice].streamTrackId = -1;
    }
}

void ESP32Synth::seekStreamMs(uint16_t voice, uint32_t ms) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        uint32_t sampleTarget = (uint32_t)(((uint64_t)ms * trk->sampleRate) / 1000ULL); 
        trk->seekTarget = sampleTarget;
    }
}

void ESP32Synth::setStreamLoopPointsMs(uint16_t voice, uint32_t startMs, uint32_t endMs) {
    if (voice >= MAX_VOICES || voices[voice].streamTrackId < 0) return;
    StreamTrack* trk = &streams[voices[voice].streamTrackId];
    
    uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
    if (bytesPerSample == 0) bytesPerSample = 2;

    uint32_t startBytes = trk->dataStartPos + (uint32_t)((((uint64_t)startMs * trk->sampleRate) / 1000ULL) * bytesPerSample);
    uint32_t endBytes = trk->dataStartPos + (uint32_t)((((uint64_t)endMs * trk->sampleRate) / 1000ULL) * bytesPerSample);

    if (startBytes < trk->dataStartPos) startBytes = trk->dataStartPos;
    if (endBytes > trk->dataStartPos + trk->dataSize || endMs == 0) endBytes = trk->dataStartPos + trk->dataSize;

    trk->loopStartBytes = startBytes;
    trk->loopEndBytes = endBytes;
    trk->loop = true; 
}

uint32_t ESP32Synth::getStreamPositionMs(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        return (uint32_t)(((uint64_t)trk->samplesPlayed * 1000ULL) / trk->sampleRate);
    }
    return 0;
}

uint32_t ESP32Synth::getStreamDurationMs(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
        if (bytesPerSample == 0) bytesPerSample = 2;
        uint32_t totalSamples = trk->dataSize / bytesPerSample;
        return (uint32_t)(((uint64_t)totalSamples * 1000ULL) / trk->sampleRate);
    }
    return 0;
}

bool ESP32Synth::isStreamPlaying(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].playing;
    }
    return false;
}

// --- Getters & Status ---

uint16_t ESP32Synth::getMasterVolume() {
    return _masterVolume >> _volShift; // Retorna na exata resolução que o usuário configurou
}

uint32_t ESP32Synth::getFrequencyCentiHz(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].freqVal : 0;
}

uint16_t ESP32Synth::getVolume(uint16_t voice) {
    return (voice < MAX_VOICES) ? (voices[voice].vol >> _volShift) : 0;
}

uint8_t ESP32Synth::getVolume8Bit(uint16_t voice) {
    return (voice < MAX_VOICES) ? (uint8_t)(voices[voice].vol >> 8) : 0;
}

uint8_t ESP32Synth::getEnv8Bit(uint16_t voice) {
    return (voice < MAX_VOICES) ? (uint8_t)(voices[voice].currEnvVal >> 20) : 0;
}

uint8_t ESP32Synth::getOutput8Bit(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    // Otimizado: Combina Env de 8-bits com Vol realocado para 8-bits
    return (uint8_t)(((voices[voice].currEnvVal >> 20) * (voices[voice].vol >> 8)) >> 8);
}

uint32_t ESP32Synth::getVolumeRaw(uint16_t voice) {
    return (voice < MAX_VOICES) ? (uint32_t)voices[voice].vol : 0;
}

uint32_t ESP32Synth::getEnvRaw(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].currEnvVal : 0;
}

uint32_t ESP32Synth::getOutputRaw(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    // 64-bits previne overflow na multiplicação do envelope máximo (32b) com volume interno (16b)
    return (uint32_t)(((uint64_t)voices[voice].currEnvVal * voices[voice].vol) >> 16);
}

bool ESP32Synth::isVoiceActive(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].active : false;
}

EnvState ESP32Synth::getEnvState(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].envState : ENV_IDLE;
}

WaveType ESP32Synth::getWaveType(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].type : WAVE_SINE;
}

uint32_t ESP32Synth::getPhase(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].phase : 0;
}

uint32_t ESP32Synth::getPulseWidth(uint16_t voice) {
    return (voice < MAX_VOICES) ? (voices[voice].pulseWidth >> _pwShift) : 0;
}

// ====================================================================================
// == PRIVATE / BACKGROUND TASKS
// ====================================================================================

// Audio Task wrapper
void ESP32Synth::audioTask(void* param) {
    ((ESP32Synth*)param)->renderLoop();
}

/**
 * @brief High-priority render loop.
 * It continuously generates audio blocks and sends them to the configured output.
 */
void IRAM_ATTR ESP32Synth::renderLoop() {
    // Define o tamanho de bloco ideal baseado no Sample Rate para preservar a precisão de modulação (ADSR, LFO).
    // Fundamental: Garante que o tamanho seja sempre potência de 2 para o Fast log2 bitwise funcionar cravado.
    int blockSamples = 512;
    if (_sampleRate <= 8000) blockSamples = 64;
    else if (_sampleRate <= 16000) blockSamples = 128;
    else if (_sampleRate <= 32000) blockSamples = 256;

    int32_t* buf = (int32_t*)malloc(512 * sizeof(int32_t));
    void* stereoBuf = malloc(512 * 2 * sizeof(int32_t)); // Serve tanto pra 16-bit quanto 32-bit

    if (!buf || !stereoBuf) {
        _running = false;
        if(buf) free(buf);
        if(stereoBuf) free(stereoBuf);
        audioTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    size_t written;

    while (_running) {
        render(buf, blockSamples); // Roda liso e entrega fragmentos processados e diretos

        // --- Hardware Output ---
        if (currentMode == SMODE_DAC) {
            #if defined(CONFIG_IDF_TARGET_ESP32)
            int16_t* buf16 = (int16_t*)buf;
            uint16_t* dacBuf = (uint16_t*)stereoBuf; 
            for (int i = 0; i < blockSamples; i++) {
                uint32_t smp = (uint32_t)(buf16[i] + 32768) >> 8;
                dacBuf[i] = (uint16_t)((smp << 8) | smp); 
            }
            dac_continuous_write(dac_handle, (uint8_t*)dacBuf, blockSamples * 2, &written, portMAX_DELAY);
            #endif
        } else if (currentMode == SMODE_I2S) {
            if (_i2sDepth == I2S_32BIT) { 
                int32_t* out32 = (int32_t*)stereoBuf;
                // Despeja frames estéreo diretos pra memória - zero overhead!
                for (int i = 0; i < blockSamples; i++) {
                    int32_t s = buf[i];
                    out32[i * 2]     = s;    
                    out32[i * 2 + 1] = s; 
                }
                i2s_channel_write(tx_handle, out32, blockSamples * 2 * sizeof(int32_t), &written, portMAX_DELAY);
            } else { // 16-bit
                int16_t* buf16 = (int16_t*)buf;
                int16_t* out16 = (int16_t*)stereoBuf;
                for (int i = 0; i < blockSamples; i++) {
                    int16_t s = buf16[i];
                    out16[i * 2]     = s;
                    out16[i * 2 + 1] = s;
                }
                i2s_channel_write(tx_handle, out16, blockSamples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
            }
        } else { // PDM
            i2s_channel_write(tx_handle, buf, blockSamples * sizeof(int16_t), &written, portMAX_DELAY);
        }
    }

    // Cleanup
    free(buf);
    free(stereoBuf);
    audioTaskHandle = NULL;
    vTaskDelete(NULL); 
}

// Core mixer
void IRAM_ATTR ESP32Synth::render(void* buffer, int samples) {
    // Local mixing buffer
    static int32_t mix[512];
    memset(mix, 0, samples * sizeof(int32_t));

    // Control Logic (LFO, Env) at slower rate
    controlSampleCounter += (uint32_t) samples;
    while (controlSampleCounter >= controlIntervalSamples) {
        processControl();
        controlSampleCounter -= controlIntervalSamples;
    }

    // --- Render each active voice ---
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];
        if (!vo->active) continue;

        int32_t startEnv, envStep;
        updateAdsrBlock(vo, samples, startEnv, envStep);
        if (startEnv == 0 && vo->currEnvVal == 0 && vo->envState != ENV_ATTACK) continue; // Skip silent voices

        if (!vo->inst) { // Standard voice
            // OTIMIZAÇÃO: Jump Table nativa ($O(1)$) ao invés de cadeia de IFs ($O(N)$)
            switch (vo->type) {
                case WAVE_SAMPLE:    renderBlockSample(vo, mix, samples, startEnv, envStep); break;
                case WAVE_STREAM:    renderBlockStream(vo, this->streams, mix, samples, startEnv, envStep); break;
                case WAVE_WAVETABLE: renderBlockWavetable(vo, mix, samples, startEnv, envStep); break;
                case WAVE_NOISE:     renderBlockNoise(vo, mix, samples, startEnv, envStep); break;
                case WAVE_CUSTOM:    if (vo->customWaveFunc) vo->customWaveFunc(vo, mix, samples, startEnv, envStep); break;
                default:             renderBlockBasic(vo, mix, samples, startEnv, envStep); break; // SINE, SAW, PULSE, TRIANGLE
            }
        } else { // Instrument voice
            if (vo->currWaveIsBasic) {
                WaveType dynamicType = (WaveType)vo->currWaveType;
                if (dynamicType == WAVE_NOISE) {
                    renderBlockNoise(vo, mix, samples, startEnv, envStep);
                } else {
                    WaveType original = vo->type;
                    vo->type = dynamicType; 
                    renderBlockBasic(vo, mix, samples, startEnv, envStep);
                    vo->type = original; // Restore original type
                }
            } else {
                renderBlockWavetable(vo, mix, samples, startEnv, envStep);
            }
        }
    }
    
    if (_customDSP) {
        // Passamos o buffer 32-bit de alta resolução ANTES de virar 16-bit
        _customDSP(mix, samples); 
    }

    // --- Final Mix & Output Format ---
    int32_t mVol = _masterVolume; // Vai de 0 a 65535
    uint32_t mask32 = 0xFFFFFFFFUL; 
    uint32_t mask16 = 0xFFFFFFFFUL; 

    if (_bitcrush > 0) {
        if (_bitcrush < 32) mask32 = 0xFFFFFFFFUL << (32 - _bitcrush);
        if (_bitcrush < 16) mask16 = 0xFFFFFFFFUL << (16 - _bitcrush);
    }
    
    if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
        int32_t* buf32 = (int32_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int64_t val = (int64_t)mix[i] * mVol; 
            
            if (val > 2147483647LL) val = 2147483647LL; 
            else if (val < -2147483648LL) val = -2147483648LL;
            
            buf32[i] = (int32_t)val & mask32;
        }
    } else { // 16-bit default
        int16_t* buf16 = (int16_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int32_t val = (int32_t)(((int64_t)mix[i] * mVol) >> 16);
            
            if (val > 32767) val = 32767; 
            else if (val < -32768) val = -32768;
            
            buf16[i] = (int16_t)(val & mask16);
        }
    }
}

// WAV Header Parser
bool ESP32Synth::parseWavHeader(fs::File& file, uint32_t& outSampleRate, uint32_t& outDataPos, uint32_t& outDataSize, uint16_t& outChannels, uint16_t& outBits) {
    file.seek(0);
    uint8_t riff[12];
    if (file.read(riff, 12) != 12) return false;
    
    // Check RIFF + fallback search
    if (strncmp((char*)riff, "RIFF", 4) != 0) {
        file.seek(0);
        bool foundRiff = false;
        for(int i = 0; i < 8192; i++) {
            if (file.read() == 'R' && file.peek() == 'I') {
                file.seek(file.position() - 1);
                if (file.read(riff, 12) == 12 && strncmp((char*)riff, "RIFF", 4) == 0) {
                    foundRiff = true; break;
                }
            }
        }
        if(!foundRiff) return false;
    }

    uint32_t tempSampleRate = 48000;
    uint16_t tempChannels = 1;
    uint16_t tempBits = 16;
    uint32_t tempDataPos = 44;
    uint32_t tempDataSize = file.size() - 44;
    bool foundData = false;

    // Parse chunks
    while (file.available() >= 8) {
        uint8_t chunkId[4]; file.read(chunkId, 4);
        uint8_t szBuf[4];   file.read(szBuf, 4);
        uint32_t chunkSize = szBuf[0] | (szBuf[1] << 8) | (szBuf[2] << 16) | (szBuf[3] << 24);
        uint32_t nextChunkPos = file.position() + chunkSize + (chunkSize & 1); // Add padding byte if chunk size is odd

        if (strncmp((char*)chunkId, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            int readLen = (chunkSize < 16) ? chunkSize : 16;
            file.read(fmt, readLen);
            tempChannels = fmt[2] | (fmt[3] << 8);
            tempSampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            tempBits = fmt[14] | (fmt[15] << 8);
            file.seek(nextChunkPos);
        } 
        else if (strncmp((char*)chunkId, "data", 4) == 0) {
            tempDataPos = file.position();
            tempDataSize = chunkSize;
            foundData = true;
            break; 
        } 
        else { // Skip other chunks like LIST, etc.
            if (nextChunkPos >= file.size() || chunkSize == 0) break; 
            file.seek(nextChunkPos);
        }
    }

    if (foundData) {
        outSampleRate = tempSampleRate;
        outChannels = tempChannels;
        outBits = tempBits;
        outDataPos = tempDataPos;
        outDataSize = tempDataSize;
        return true;
    }
    return false;
}

// SD Background Loader
void ESP32Synth::sdLoaderTask(void* param) {
    ESP32Synth* synth = (ESP32Synth*)param;
    uint8_t tempBuf[2048] __attribute__((aligned(4)));
    
   while(synth->_running) {
        bool needMoreYield = true;

        for (int i = 0; i < MAX_STREAMS; i++) {
            StreamTrack* trk = &synth->streams[i];
            if (!trk->active || !trk->file) continue;
            
            // Seek
            if (trk->seekTarget >= 0) {
                uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
                if (bytesPerSample == 0) bytesPerSample = 2;
                uint32_t targetByte = trk->dataStartPos + (trk->seekTarget * bytesPerSample);
                if (targetByte < trk->dataStartPos + trk->dataSize) {
                    trk->file.seek(targetByte);
                    trk->samplesPlayed = trk->seekTarget;
                }
                trk->head = 0; trk->tail = 0; // Flush buffer
                trk->seekTarget = -1;
            }

            if (!trk->playing) continue; 
            
            uint8_t bytesPerCh = trk->bitsPerSample / 8;
            if (bytesPerCh == 0) bytesPerCh = 2; 
            uint16_t frameSize = bytesPerCh * trk->numChannels;
            if (frameSize == 0) frameSize = 2;
            
            // Check Buffer Space
            uint16_t freeSpace = (STREAM_BUF_SAMPLES + trk->tail - trk->head - 1) & STREAM_BUF_MASK;
            uint16_t maxFramesBuffer = 2048 / frameSize; 
            
            if (freeSpace < maxFramesBuffer / 2) {
                continue; // Not enough space to be worth a read
            }
            needMoreYield = false;
            
            uint16_t framesToRead = (freeSpace > maxFramesBuffer) ? maxFramesBuffer : freeSpace;
            size_t bytesToRead = framesToRead * frameSize;
            
            // Loop / End check
            uint32_t absoluteEnd = trk->loop ? trk->loopEndBytes : (trk->dataStartPos + trk->dataSize);
            if (trk->file.position() + bytesToRead > absoluteEnd) {
                bytesToRead = absoluteEnd - trk->file.position();
                if(bytesToRead == 0) {
                    if(trk->loop) {
                        trk->file.seek(trk->loopStartBytes);
                        trk->samplesPlayed = (trk->loopStartBytes - trk->dataStartPos) / frameSize;
                    } else {
                        trk->playing = false;
                    }
                    continue;
                }
            }

            size_t bytesRead = (bytesToRead > 0) ? trk->file.read(tempBuf, bytesToRead) : 0;
            
            if (bytesRead > 0) {
                uint16_t framesRead = bytesRead / frameSize;
                uint8_t* ptr = tempBuf;
                uint16_t head = trk->head;
                
                // Decode & Downmix to 16-bit Mono
                if (trk->bitsPerSample == 16) {
                    if (trk->numChannels == 2) { // Stereo 16-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=4) {
                            trk->buffer[head] = (int16_t)(((int32_t)((int16_t*)ptr)[0] + (int32_t)((int16_t*)ptr)[1]) >> 1); // Mix to mono
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else { // Mono 16-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=2) {
                            trk->buffer[head] = ((int16_t*)ptr)[0];
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                } else if (trk->bitsPerSample == 24) {
                    if (trk->numChannels == 2) { // Stereo 24-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=6) {
                            int32_t l = (ptr[1] | ((int8_t)ptr[2] << 8));
                            int32_t r = (ptr[4] | ((int8_t)ptr[5] << 8));
                            trk->buffer[head] = (int16_t)((l + r) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else { // Mono 24-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=3) {
                            trk->buffer[head] = (int16_t)(ptr[1] | ((int8_t)ptr[2] << 8)); // Grab top 16 bits
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                } else if (trk->bitsPerSample == 32) {
                    if (trk->numChannels == 2) { // Stereo 32-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=8) {
                            int32_t l = ((int32_t*)ptr)[0] >> 16;
                            int32_t r = ((int32_t*)ptr)[1] >> 16;
                            trk->buffer[head] = (int16_t)((l + r) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else { // Mono 32-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=4) {
                            trk->buffer[head] = (int16_t)(((int32_t*)ptr)[0] >> 16);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                } else if (trk->bitsPerSample == 8) {
                    if (trk->numChannels == 2) { // Stereo 8-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr+=2) {
                            int32_t l = ((int16_t)ptr[0] - 128) << 8;
                            int32_t r = ((int16_t)ptr[1] - 128) << 8;
                            trk->buffer[head] = (int16_t)((l + r) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else { // Mono 8-bit
                        for (uint16_t f = 0; f < framesRead; f++, ptr++) {
                            trk->buffer[head] = (int16_t)(((int16_t)*ptr - 128) << 8);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                }
                trk->head = head; // Update head position
            }
        }
        
        // Yield (longer if idle)
        vTaskDelay(pdMS_TO_TICKS(needMoreYield ? 5 : 1));
    }
    
    // Shutdown
    synth->streamTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Fetch sample from wavetable
IRAM_ATTR int16_t ESP32Synth::fetchWavetableSample(uint16_t id, uint32_t phase) {
    if (id >= MAX_WAVETABLES) return 0;
    const auto& e = wavetables[id];
    if (!e.data || e.size == 0) return 0;

    uint32_t idx = (uint32_t)(((uint64_t) phase * e.size) >> 32);

    if (e.depth == BITS_8) {
        return (((uint8_t*)e.data)[idx] - 128) << 8;
    } else if (e.depth == BITS_4) {
        const uint8_t* p = (const uint8_t*)e.data;
        uint8_t val = p[idx / 2];
        val = ((idx & 1) == 0) ? (val & 0x0F) : (val >> 4);
        return ((int16_t) val - 8) * 4096;
    }
    
    // Default to 16-bit
    return ((int16_t*)e.data)[idx];
}

// Control Rate Logic (LFOs, Envelopes, Arps). Runs at ~100Hz.
void IRAM_ATTR ESP32Synth::processControl() {
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];

        // --- LFOs (Vibrato and Tremolo) ---
        if (vo->vibDepthInc > 0) {
            vo->vibPhase += (vo->vibRateInc * controlIntervalSamples);
            int16_t lfoVal = sineLUT[vo->vibPhase >> SINE_SHIFT];
            vo->vibOffset = ((int64_t)vo->vibDepthInc * lfoVal) >> 15; 
        } else {
            vo->vibOffset = 0;
        }

        if (vo->trmDepth > 0) {
            vo->trmPhase += (vo->trmRateInc * controlIntervalSamples);
            int32_t lfo = sineLUT[vo->trmPhase >> SINE_SHIFT] + 32768; // Vai de 0 a 65535 (lisinho)
            int32_t depth = vo->trmDepth >> 8; // Converte a profundidade bruta para o ganho de mixer (0-255)
            int32_t reduction = (lfo * depth) >> 16; 
            vo->trmModGain = 255 - reduction;
        } else {
            vo->trmModGain = 255;
        }

        // --- Arpeggiator ---
        if (vo->arpActive && vo->arpLen > 0) {
            if (vo->arpTickCounter == 0) {
                setFrequency(v, vo->arpNotes[vo->arpIdx]);
                vo->arpIdx = (vo->arpIdx + 1) % vo->arpLen;
                vo->arpTickCounter = ((uint32_t)vo->arpSpeedMs * controlRateHz + 999) / 1000;
                if (vo->arpTickCounter == 0) vo->arpTickCounter = 1;
            }
            vo->arpTickCounter--;
        }

        // --- Frequency Portamento (Slide) ---
        if (vo->slideFreqActive && vo->slideFreqTicksRemaining > 0) {
            vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + vo->slideFreqDeltaInc);
            // Remainder for precision
            if (vo->slideFreqRem != 0 && vo->slideFreqTicksTotal > 0) {
                vo->slideFreqRemAcc += vo->slideFreqRem;
                int32_t sign = (vo->slideFreqRem > 0) ? 1 : -1;
                if (abs(vo->slideFreqRemAcc) >= (int32_t)vo->slideFreqTicksTotal) {
                    vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + sign);
                    vo->slideFreqRemAcc -= sign * (int32_t)vo->slideFreqTicksTotal;
                }
            }
            vo->slideFreqTicksRemaining--;
            
            // Update the 'freqVal' for getters
            vo->freqVal = (uint32_t)(((uint64_t)vo->phaseInc * _sampleRate * 100) >> 32);
            
            if (vo->slideFreqTicksRemaining == 0) { // Slide finished
                vo->phaseInc = vo->slideFreqTargetInc;
                vo->freqVal = vo->slideFreqTargetCenti;
                vo->slideFreqActive = false;
            }
            
            // Re-calculate increment for sample/stream if needed
            if (vo->type == WAVE_STREAM && vo->streamTrackId >= 0) {
                StreamTrack* trk = &streams[vo->streamTrackId];
                if (trk->rootFreqCentiHz > 0) {
                    uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / trk->rootFreqCentiHz;
                    vo->sampleInc1616 = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);
                }
            } else if (vo->type == WAVE_SAMPLE && vo->instSample == nullptr) {
                const SampleData* sData = &registeredSamples[vo->curSampleId];
                if (sData->data && sData->rootFreqCentiHz > 0) {
                    uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / sData->rootFreqCentiHz;
                    vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
                }
            }
        }

        // --- Volume Portamento (Slide) ---
        if (vo->slideVolActive && vo->slideVolTicksRemaining > 0) {
            vo->slideVolCurr += vo->slideVolInc;
            vo->vol = (uint16_t)(vo->slideVolCurr >> 16);
            
            vo->slideVolTicksRemaining--;
            if (vo->slideVolTicksRemaining == 0) {
                vo->vol = vo->slideVolTarget;
                vo->slideVolActive = false;
            }
        }

        // --- Tracker Instrument Logic ---
        if (!vo->active || !vo->inst) continue;
        Instrument* inst = vo->inst;
        int16_t wVal = 0;
        uint8_t nextVol = 0;
        bool stageStarted = false; 
        uint8_t len;
        uint32_t ms = 0;
        uint8_t idx;

        switch (vo->envState) {
        case ENV_ATTACK:
            len = inst->seqLen;
            if (len == 0) { // No attack sequence, go to sustain
                vo->envState = ENV_SUSTAIN;
                vo->controlTick = 0;
                break;
            }
            if (vo->controlTick == 0) {
                ms = inst->seqSpeedMs;
                vo->controlTick = (ms == 0) ? 1 : (ms * controlRateHz + 999) / 1000;
                stageStarted = true;
            }
            
            if (stageStarted) {
                idx = vo->stageIdx;
                if (idx >= len) idx = len - 1;
                wVal = inst->seqWaves[idx];
                nextVol = inst->seqVolumes[idx];
            }
            
            if (vo->controlTick > 0) vo->controlTick--;
            if (vo->controlTick == 0) {
                vo->stageIdx++;
                if (vo->stageIdx >= len) {
                    vo->envState = ENV_SUSTAIN;
                    vo->controlTick = 0;
                }
            }
            break;

        case ENV_SUSTAIN:
            if (vo->controlTick == 0) {
                stageStarted = true;
                vo->controlTick = 1; // Prevent re-triggering
            }
            if (stageStarted) {
                wVal = inst->susWave;
                nextVol = inst->susVol;
            }
            break;

        case ENV_RELEASE:
            len = inst->relLen;
            if (len == 0) {
                vo->active = false; // No release sequence, just deactivate
                break;
            }
            if (vo->controlTick == 0) {
                ms = inst->relSpeedMs;
                vo->controlTick = (ms == 0) ? 1 : (ms * controlRateHz + 999) / 1000;
                stageStarted = true;
            }
            
            if (stageStarted) {
                idx = vo->stageIdx;
                if (idx >= len) idx = len - 1;
                wVal = inst->relWaves[idx];
                nextVol = inst->relVolumes[idx];
            }
            
            if (vo->controlTick > 0) vo->controlTick--;
            if (vo->controlTick == 0) {
                vo->stageIdx++;
                if (vo->stageIdx >= len) vo->active = false; // End of release sequence
            }
            break;
        default: break;
        }

        if (stageStarted) {
            // Os instrumentos NATIVAMENTE ficam em 8-bits na memória para salvar RAM. 
            // O código joga para 16-bits dinamicamente (<< 8). Sem quebrar código antigo seu!
            if (inst->smoothVolume && ms > 0) {
                slideVolAbsolute(v, vo->vol, (uint16_t)nextVol << 8, ms); 
            } else {
                vo->vol = (uint16_t)nextVol << 8;
                vo->slideVolActive = false;
            }

            // Apply wave change
            if (wVal < 0) { // Negative values are basic waveforms
                vo->currWaveIsBasic = 1;
                vo->currWaveType = (WaveType)wVal; 
            } else { // Positive values are wavetable IDs
                vo->currWaveIsBasic = 0;
                vo->currWaveType = WAVE_WAVETABLE;
                vo->currWaveId = (uint16_t)wVal;

                if (vo->currWaveId < MAX_WAVETABLES) {
                    vo->wtData = wavetables[vo->currWaveId].data;
                    vo->wtSize = wavetables[vo->currWaveId].size;
                    vo->depth = wavetables[vo->currWaveId].depth;
                    if (vo->depth == 0) vo->depth = BITS_8;
                }
            }
        }
    }

    if (_customControl) {
        _customControl();
    }
}
