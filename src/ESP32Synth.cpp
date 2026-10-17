// ESP32Synth.cpp - Implementation
// If it weren't for God, ESP32Synth would have never worked, so thank Him for this wonderful piece of code that is ESP32Synth. Amen!
#pragma GCC optimize("O3,unroll-loops")
#include "ESP32Synth.h"
#if ENABLE_MP3
#include "helix/mp3dec.h"
#endif

// ====================================================================================
// SIMD Definitions and assembly for ESP32-S3 (Raw Performance Xtensa LX7 128-bit)
// ====================================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
typedef int32_t v4i32 __attribute__((vector_size(16)));
typedef uint32_t v4u32 __attribute__((vector_size(16)));
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32)
    // Hardware Clamping: Saturates to 16-bit signed in exactly 1 clock cycle.
    static inline __attribute__((always_inline)) int32_t hw_clamps16(int32_t val) {
        int32_t res;
        asm volatile ("CLAMPS %[r], %[v], 15" : [r] "=r" (res) : [v] "r" (val));
        return res;
    }
#else
    // Fallback for non-Xtensa architectures
    static inline __attribute__((always_inline)) int32_t hw_clamps16(int32_t val) {
        return (val > 32767) ? 32767 : (val < -32768) ? -32768 : val;
    }
#endif
// ====================================================================================

// ====================================================================================
//    SINE WAVE LOOK-UP TABLE
// ====================================================================================

// Shared sine LUT (+1 guard sample to eliminate wrapping logic)
int16_t sineLUT[SINE_LUT_SIZE + 1] __attribute__((aligned(16)));

// Sample storage
SampleData registeredSamples[MAX_SAMPLES];

// --- Other Modules includes ---
#include "ESP32Synth_Begins.hpp"
#include "ESP32Synth_Core.hpp"
#include "ESP32Synth_Core_Getters.hpp"
#include "ESP32Synth_Renders.hpp"
#include "ESP32Synth_SDStream.hpp"
// -------------------------------

// --- Constructor & Destructor ---

ESP32Synth::ESP32Synth() {
    _sampleRate = 48000;

    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i] = {};
        voices[i].type = WAVE_SINE;
        voices[i].envState = ENV_IDLE;
        voices[i].rngState = 12345 + (i * 999); // Unique RNG seed per voice
        voices[i].rateAttack = ENV_MAX;
        voices[i].rateDecay = ENV_MAX;
        voices[i].levelSustain = ENV_MAX;
        voices[i].rateRelease = ENV_MAX;
        voices[i].pulseWidth = 0x80000000;
        voices[i].streamTrackId = -1;
        voices[i].customWaveFunc = nullptr;
        voices[i].smoothEnv = false; // Default to false for standard notes. Set to true dynamically or via instruments.
        voices[i].targetBus = 0;
        voices[i].trmModGain = 256;
        memset(voices[i].cp, 0, sizeof(voices[i].cp));
    }

    for (int i = 0; i < MAX_STREAMS; i++) {
        streams[i].active = false;
        streams[i].playing = false;
        streams[i].seekTarget = -1;
        streams[i].head = 0;
        streams[i].tail = 0;
        streams[i].sampleRate = 0;
        #if ENABLE_MP3
        streams[i].isMP3 = false;
        #endif
    }

    for (uint16_t i = 0; i < MAX_WAVETABLES; i++) {
        wavetables[i] = {};
        wavetables[i].depth = BITS_8;
    }

    for (int i = 0; i < MAX_BUSES; i++) {
        busesConfig[i] = {};
        busesConfig[i].mixLevel = 255;
    }
    
    controlRateHz = 100;
    memset(dp, 0, sizeof(dp));
}

ESP32Synth::~ESP32Synth() {
    end();
}

// --- Other Methods ---

void ESP32Synth::setSampleRate(uint32_t rate) {
    // WARNING: ESP32Synth was mathematically designed, highly optimized,
    // and exhaustively tested at 48kHz. Changing this may cause jitter or aliasing.
    // Change at your own risk.
    if (rate > 0 && rate != _sampleRate) {
        _sampleRate = rate;
        _customSampleRate = true;
        controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;

        // Recalculates the phase motor step of all notes that are already playing!
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
    // Limits between 0 (disabled) and 32 bits
    if (bits > 32) bits = 32;
    _bitcrush = bits;
}

void ESP32Synth::setVolDepthBase(uint8_t bits) {
    if (bits < 1) bits = 1;
    if (bits > 16) bits = 16;
    _volShift = 16 - bits; // Automatically calculates the Shift required to achieve 16-bit internally.
}

void ESP32Synth::setMasterVolume(uint16_t volume) {
    uint32_t calc = (uint32_t)volume << _volShift;
    _masterVolume = (calc > 65535) ? 65535 : (uint16_t)calc;
}

void ESP32Synth::setCustomDSP(SynthDSPCallback dspFunc) {
    _customDSP = dspFunc;
}

void ESP32Synth::setCustomControl(SynthControlCallback ctrlFunc) {
    _customControl = ctrlFunc;
}

void ESP32Synth::setCustomOutput(SynthCustomOutputCallback outFunc) {
    _customOutput = outFunc;
}

void ESP32Synth::setVoiceBus(uint16_t voice, uint8_t busId) {
    if (voice < MAX_VOICES && busId < MAX_BUSES) {
        voices[voice].targetBus = busId;
        if (busId > 0) busesConfig[busId].active = true;
    }
}

void ESP32Synth::setBusFX(uint8_t busId, uint8_t slotId, FXType type, SynthCustomFXCallback cb) {
    if (busId < MAX_BUSES && slotId < MAX_FX_PER_BUS) {
        FXNode* fx = &busesConfig[busId].fxChain[slotId];
        fx->type = type;
        fx->customFXFunc = cb;
        fx->active = (type != FX_NONE);
        memset(fx->ep, 0, sizeof(fx->ep));
        memset(fx->es, 0, sizeof(fx->es));
        busesConfig[busId].active = true;
    }
}

void ESP32Synth::setBusFXParam(uint8_t busId, uint8_t slotId, uint8_t paramId, int16_t value) {
    if (busId < MAX_BUSES && slotId < MAX_FX_PER_BUS && paramId < SYNTH_FX_PARAMS) {
        busesConfig[busId].fxChain[slotId].ep[paramId] = value;
    }
}

void ESP32Synth::setBusMix(uint8_t busId, uint8_t volume) {
    if (busId < MAX_BUSES) {
        busesConfig[busId].mixLevel = volume;
        busesConfig[busId].active = true;
    }
}

const char* ESP32Synth::getChipModel() {
    return SYNTH_CHIP;
}

int32_t ESP32Synth::getSampleRate() {
    return _sampleRate;
}

float ESP32Synth::getCPULoad() {
    return _dspLoad;
}

float ESP32Synth::getStreamLoad() {
    return _streamLoad;
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
void ESP32Synth::renderLoop() {
    int blockSamples = SYNTH_DMA_BUF_LEN;
    if (_sampleRate <= 8000) {
        blockSamples = SYNTH_DMA_BUF_LEN / 8;
    } else if (_sampleRate <= 16000) {
        blockSamples = SYNTH_DMA_BUF_LEN / 4;
    } else if (_sampleRate <= 32000) {
        blockSamples = SYNTH_DMA_BUF_LEN / 2;
    }

    if (blockSamples < 16) blockSamples = 16;

    // SIMD SHIELDING: Ensures the block is ALWAYS a multiple of 4.
    blockSamples = (blockSamples + 3) & ~3;

    int32_t* buf = (int32_t*)heap_caps_aligned_alloc(16, blockSamples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* stereoBuf = heap_caps_aligned_alloc(16, blockSamples * 2 * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    // Multi-Bus Aligned Buffers Allocation
    int32_t* busBuffers[MAX_BUSES] = {nullptr};
    bool busAllocFailed = false;
    for (int i = 0; i < MAX_BUSES; i++) {
        busBuffers[i] = (int32_t*)heap_caps_aligned_alloc(16, blockSamples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!busBuffers[i]) busAllocFailed = true;
    }

    if (!buf || !stereoBuf || busAllocFailed) {
        _running = false;
        if (buf) heap_caps_free(buf);
        if (stereoBuf) heap_caps_free(stereoBuf);
        for (int i = 0; i < MAX_BUSES; i++) {
            if (busBuffers[i]) heap_caps_free(busBuffers[i]);
        }
        audioTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    size_t written;

    if (currentMode == SMODE_PWM) {
        pwm_block_samples = blockSamples;
        pwm_ping_pong_buf[0] = (int16_t*)heap_caps_aligned_alloc(16, blockSamples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        pwm_ping_pong_buf[1] = (int16_t*)heap_caps_aligned_alloc(16, blockSamples * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        pwm_active_buf = 0;
        pwm_read_idx = 0;
        if (pwm_ping_pong_buf[0]) memset(pwm_ping_pong_buf[0], 0, blockSamples * sizeof(int16_t));
        if (pwm_ping_pong_buf[1]) memset(pwm_ping_pong_buf[1], 0, blockSamples * sizeof(int16_t));
        xSemaphoreGive(pwm_sema);
    }

    uint32_t cpu_freq = SYNTH_GET_CPU_FREQ_MHZ() * 1000000;
    uint32_t max_cycles_per_block = (cpu_freq / _sampleRate) * blockSamples;

    while (_running) {
        uint32_t start_cycles = esp_cpu_get_cycle_count();

        render(buf, busBuffers, blockSamples);

        // --- SD RECORDING HOOK ---
        if (UNLIKELY(_isRecording && _recBuffer)) {
            uint32_t h = _recHead;
            uint32_t space = (RECORD_BUF_SAMPLES + _recTail - h - 1) & RECORD_BUF_MASK;
            
            if (space >= (uint32_t)blockSamples) {
                if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
                    int32_t* b32 = (int32_t*)buf;
                    for (int i = 0; i < blockSamples; i++) {
                        _recBuffer[h] = (int16_t)(b32[i] >> 16);
                        h = (h + 1) & RECORD_BUF_MASK;
                    }
                    _recHead = h;
                } else {
                    int16_t* b16 = (int16_t*)buf;
                    uint32_t tillEnd = RECORD_BUF_SAMPLES - h;
                    if ((uint32_t)blockSamples <= tillEnd) {
                        memcpy((void*)&_recBuffer[h], b16, blockSamples * 2);
                    } else {
                        memcpy((void*)&_recBuffer[h], b16, tillEnd * 2);
                        memcpy((void*)&_recBuffer[0], b16 + tillEnd, (blockSamples - tillEnd) * 2);
                    }
                    _recHead = (h + blockSamples) & RECORD_BUF_MASK;
                }
            }
        }

        uint32_t end_cycles = esp_cpu_get_cycle_count();
        uint32_t used_cycles = end_cycles - start_cycles;

        _dspLoad = ((float)used_cycles / (float)max_cycles_per_block) * 100.0f;

        if (currentMode == SMODE_DAC) {
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
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
                uint64_t* out64 = (uint64_t*)stereoBuf;
                int i = 0;
                for (; i <= blockSamples - 4; i += 4) {
                    out64[i]     = ((uint64_t)(uint32_t)buf[i] << 32)     | (uint32_t)buf[i];
                    out64[i + 1] = ((uint64_t)(uint32_t)buf[i + 1] << 32) | (uint32_t)buf[i + 1];
                    out64[i + 2] = ((uint64_t)(uint32_t)buf[i + 2] << 32) | (uint32_t)buf[i + 2];
                    out64[i + 3] = ((uint64_t)(uint32_t)buf[i + 3] << 32) | (uint32_t)buf[i + 3];
                }
                for (; i < blockSamples; i++) {
                    uint64_t s = (uint32_t)buf[i];
                    out64[i] = (s << 32) | s;
                }
                i2s_channel_write(tx_handle, stereoBuf, blockSamples * 2 * sizeof(int32_t), &written, portMAX_DELAY);
            } else {
                int16_t* buf16 = (int16_t*)buf;
                uint32_t* out32 = (uint32_t*)stereoBuf;
                int i = 0;
                for (; i <= blockSamples - 4; i += 4) {
                    uint32_t s0 = (uint16_t)buf16[i];
                    uint32_t s1 = (uint16_t)buf16[i + 1];
                    uint32_t s2 = (uint16_t)buf16[i + 2];
                    uint32_t s3 = (uint16_t)buf16[i + 3];
                    out32[i]     = (s0 << 16) | s0;
                    out32[i + 1] = (s1 << 16) | s1;
                    out32[i + 2] = (s2 << 16) | s2;
                    out32[i + 3] = (s3 << 16) | s3;
                }
                for (; i < blockSamples; i++) {
                    uint32_t s = (uint16_t)buf16[i];
                    out32[i] = (s << 16) | s;
                }
                i2s_channel_write(tx_handle, stereoBuf, blockSamples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
            }
        } else if (currentMode == SMODE_PWM) {
            if (xSemaphoreTake(pwm_sema, pdMS_TO_TICKS(20)) != pdTRUE) continue;
            int render_target = 1 - pwm_active_buf;
            int16_t* buf16 = (int16_t*)buf;
            if (pwm_ping_pong_buf[render_target] != nullptr) {
                memcpy(pwm_ping_pong_buf[render_target], buf16, blockSamples * sizeof(int16_t));
            }
        } else if (currentMode == SMODE_PDM) {
            i2s_channel_write(tx_handle, buf, blockSamples * sizeof(int16_t), &written, portMAX_DELAY);
        } else if (currentMode == SMODE_CUSTOM) {
            if (_customOutput) {
                int16_t* buf16 = (int16_t*)buf;
                _customOutput(buf16, blockSamples);

                uint32_t full_cycles = esp_cpu_get_cycle_count() - start_cycles;
                if (full_cycles < max_cycles_per_block) {
                    uint32_t wait_us = (max_cycles_per_block - full_cycles) / SYNTH_GET_CPU_FREQ_MHZ();
                    if (wait_us > 2000) {
                        vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
                    } else {
                        SYNTH_DELAY_US(wait_us);
                    }
                }
            }
        } else if (currentMode == SMODE_HEADLESS) {
            uint32_t full_cycles = esp_cpu_get_cycle_count() - start_cycles;
            if (full_cycles < max_cycles_per_block) {
                uint32_t wait_us = (max_cycles_per_block - full_cycles) / SYNTH_GET_CPU_FREQ_MHZ();
                if (wait_us > 2000) {
                    vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
                } else {
                    SYNTH_DELAY_US(wait_us);
                }
            }
        }
    }

    // Cleanup when task ends
    if (buf) heap_caps_free(buf);
    if (stereoBuf) heap_caps_free(stereoBuf);
    for (int i = 0; i < MAX_BUSES; i++) {
        if (busBuffers[i]) heap_caps_free(busBuffers[i]);
    }
    audioTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Core mixer
void IRAM_ATTR ESP32Synth::render(void* buffer, int32_t** busBuffers, int samples) {
    // 1. Zero only Master and active buses efficiently
    memset(busBuffers[0], 0, samples * sizeof(int32_t));
    for (int b = 1; b < MAX_BUSES; b++) {
        if (busesConfig[b].active) {
            memset(busBuffers[b], 0, samples * sizeof(int32_t));
        }
    }

    controlSampleCounter += (uint32_t)samples;
    while (controlSampleCounter >= controlIntervalSamples) {
        processControl();
        controlSampleCounter -= controlIntervalSamples;
    }

    // 2. Render voices to their target buses (CPU Cost: O(1) direct pointer access)
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];
        if (!vo->active) continue;

        int samplesRem = samples;
        int bufOffset = 0;

        while (samplesRem > 0 && vo->active) {
            int32_t startEnv, envStep;
            int stepSamples = updateAdsrBlock(vo, samplesRem, startEnv, envStep);

            if (startEnv == 0 && vo->currEnvVal == 0 && vo->envState != ENV_ATTACK) break;

            int32_t* currentMixBuf = busBuffers[vo->targetBus] + bufOffset;

            if (!vo->inst) {
                switch (vo->type) {
                    case WAVE_SAMPLE:    renderBlockSample(vo, currentMixBuf, stepSamples, startEnv, envStep); break;
                    case WAVE_STREAM:    renderBlockStream(vo, this->streams, currentMixBuf, stepSamples, startEnv, envStep); break;
                    case WAVE_WAVETABLE: renderBlockWavetable(vo, currentMixBuf, stepSamples, startEnv, envStep); break;
                    case WAVE_NOISE:     renderBlockNoise(vo, currentMixBuf, stepSamples, startEnv, envStep); break;
                    case WAVE_CUSTOM:    if (vo->customWaveFunc) vo->customWaveFunc(vo, currentMixBuf, stepSamples, startEnv, envStep); break;
                    default:             renderBlockBasic(vo, currentMixBuf, stepSamples, startEnv, envStep); break;
                }
            } else {
                if (vo->currWaveIsBasic) {
                    WaveType dynamicType = (WaveType)vo->currWaveType;
                    if (dynamicType == WAVE_NOISE) { renderBlockNoise(vo, currentMixBuf, stepSamples, startEnv, envStep); } 
                    else {
                        WaveType original = vo->type; vo->type = dynamicType;
                        renderBlockBasic(vo, currentMixBuf, stepSamples, startEnv, envStep);
                        vo->type = original;
                    }
                } else {
                    renderBlockWavetable(vo, currentMixBuf, stepSamples, startEnv, envStep);
                }
            }
            bufOffset += stepSamples;
            samplesRem -= stepSamples;
        }
    }

    // 3. Process FX Chains on isolated buses, then mix down to Master (Bus 0)
    for (int b = 1; b < MAX_BUSES; b++) {
        AudioBus* bus = &busesConfig[b];
        if (!bus->active || bus->mixLevel == 0) continue;

        // Run FX Chain In-Place
        for (int f = 0; f < MAX_FX_PER_BUS; f++) {
            FXNode* fx = &bus->fxChain[f];
            if (UNLIKELY(fx->active)) {
                if (fx->type == FX_CUSTOM && fx->customFXFunc) {
                    fx->customFXFunc(busBuffers[b], samples, fx->ep, fx->es);
                }
            }
        }

        // Mixdown to Bus 0 (Master) utilizing Vectorized processing if S3
        if (bus->mixLevel > 0) {
            int32_t mVol = bus->mixLevel;
            int32_t* src = busBuffers[b];
            int32_t* dst = busBuffers[0];
            
            #if defined(CONFIG_IDF_TARGET_ESP32S3)
            v4i32 vVol = {mVol, mVol, mVol, mVol};
            int vCount = samples >> 2;
            v4i32* vSrc = (v4i32*)src;
            v4i32* vDst = (v4i32*)dst;
            for (int i = 0; i < vCount; i++) vDst[i] += (vSrc[i] * vVol) >> 8;
            #else
            for (int i = 0; i < samples; i++) dst[i] += (src[i] * mVol) >> 8;
            #endif
        }
    }

    // 4. Global Master DSP (Always applied to Bus 0)
    if (_customDSP) {
        _customDSP(busBuffers[0], samples, this->dp);
    }

    // 5. Hardware Output Clipping & Bitcrush (Using Bus 0)
    int32_t mVol = _masterVolume;
    uint32_t mask32 = 0xFFFFFFFFUL;
    uint32_t mask16 = 0xFFFFFFFFUL;
    if (_bitcrush > 0) {
        mask32 = (_bitcrush >= 32) ? 0xFFFFFFFFUL : (0xFFFFFFFFUL << (32 - _bitcrush));
        mask16 = (_bitcrush >= 16) ? 0xFFFFUL     : (0xFFFFUL << (16 - _bitcrush));
    }

    if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
        int32_t* buf32 = (int32_t*)buffer;
        int i = 0;
        for (; i <= samples - 4; i += 4) {
            int64_t val0 = ((int64_t)busBuffers[0][i]     * mVol);
            int64_t val1 = ((int64_t)busBuffers[0][i + 1] * mVol);
            int64_t val2 = ((int64_t)busBuffers[0][i + 2] * mVol);
            int64_t val3 = ((int64_t)busBuffers[0][i + 3] * mVol);
            val0 = (val0 > 2147483647LL) ? 2147483647LL : (val0 < -2147483648LL) ? -2147483648LL : val0;
            val1 = (val1 > 2147483647LL) ? 2147483647LL : (val1 < -2147483648LL) ? -2147483648LL : val1;
            val2 = (val2 > 2147483647LL) ? 2147483647LL : (val2 < -2147483648LL) ? -2147483648LL : val2;
            val3 = (val3 > 2147483647LL) ? 2147483647LL : (val3 < -2147483648LL) ? -2147483648LL : val3;
            buf32[i]     = (int32_t)val0 & mask32;
            buf32[i + 1] = (int32_t)val1 & mask32;
            buf32[i + 2] = (int32_t)val2 & mask32;
            buf32[i + 3] = (int32_t)val3 & mask32;
        }
        for (; i < samples; i++) {
            int64_t val = (int64_t)busBuffers[0][i] * mVol;
            val = (val > 2147483647LL) ? 2147483647LL : (val < -2147483648LL) ? -2147483648LL : val;
            buf32[i] = (int32_t)val & mask32;
        }
    } else {
        int16_t* buf16 = (int16_t*)buffer;
        int i = 0;
        for (; i <= samples - 4; i += 4) {
            int64_t v0 = ((int64_t)busBuffers[0][i]     * mVol) >> 16;
            int64_t v1 = ((int64_t)busBuffers[0][i + 1] * mVol) >> 16;
            int64_t v2 = ((int64_t)busBuffers[0][i + 2] * mVol) >> 16;
            int64_t v3 = ((int64_t)busBuffers[0][i + 3] * mVol) >> 16;
            buf16[i]     = (int16_t)(hw_clamps16((int32_t)v0) & mask16);
            buf16[i + 1] = (int16_t)(hw_clamps16((int32_t)v1) & mask16);
            buf16[i + 2] = (int16_t)(hw_clamps16((int32_t)v2) & mask16);
            buf16[i + 3] = (int16_t)(hw_clamps16((int32_t)v3) & mask16);
        }
        for (; i < samples; i++) {
            int64_t v = ((int64_t)busBuffers[0][i] * mVol) >> 16;
            buf16[i] = (int16_t)(hw_clamps16((int32_t)v) & mask16);
        }
    }
}

// WAV Header Parser
bool ESP32Synth::parseWavHeader(SYNTH_FILE_REF file, uint32_t& outSampleRate, uint32_t& outDataPos, uint32_t& outDataSize, uint16_t& outChannels, uint16_t& outBits) {
    SYNTH_FILE_SEEK(file, 0);
    uint8_t riff[12];
    if (SYNTH_FILE_READ(file, riff, 12) != 12) return false;

    if (strncmp((char*)riff, "RIFF", 4) != 0) {
        SYNTH_FILE_SEEK(file, 0);
        bool foundRiff = false;
        uint8_t buf[512];
        int offset = 0;

        while (offset < 8192 && !foundRiff && SYNTH_FILE_AVAILABLE(file)) {
            SYNTH_FILE_SEEK(file, offset);
            int bytesRead = SYNTH_FILE_READ(file, buf, 512);
            if (bytesRead < 4) break;

            for (int i = 0; i < bytesRead - 3; i++) {
                if (buf[i] == 'R' && buf[i + 1] == 'I' && buf[i + 2] == 'F' && buf[i + 3] == 'F') {
                    SYNTH_FILE_SEEK(file, offset + i);
                    if (SYNTH_FILE_READ(file, riff, 12) == 12) foundRiff = true;
                    break;
                }
            }
            offset += 512 - 3;
        }
        if (!foundRiff) return false;
    }

    uint32_t tempSampleRate = 48000;
    uint16_t tempChannels = 1;
    uint16_t tempBits = 16;
    uint32_t tempDataPos = 44;
    uint32_t tempDataSize = SYNTH_FILE_SIZE(file) - 44;
    bool foundData = false;

    // Parse chunks
    while (SYNTH_FILE_AVAILABLE(file) >= 8) {
        uint8_t chunkId[4];
        SYNTH_FILE_READ(file, chunkId, 4);
        uint8_t szBuf[4];
        SYNTH_FILE_READ(file, szBuf, 4);
        uint32_t chunkSize = szBuf[0] | (szBuf[1] << 8) | (szBuf[2] << 16) | (szBuf[3] << 24);
        uint32_t nextChunkPos = SYNTH_FILE_POS(file) + chunkSize + (chunkSize & 1);

        if (strncmp((char*)chunkId, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            int readLen = (chunkSize < 16) ? chunkSize : 16;
            SYNTH_FILE_READ(file, fmt, readLen);
            tempChannels = fmt[2] | (fmt[3] << 8);
            tempSampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
            tempBits = fmt[14] | (fmt[15] << 8);
            SYNTH_FILE_SEEK(file, nextChunkPos);
        } else if (strncmp((char*)chunkId, "data", 4) == 0) {
            tempDataPos = SYNTH_FILE_POS(file);
            tempDataSize = chunkSize;
            foundData = true;
            break;
        } else {
            if (nextChunkPos >= SYNTH_FILE_SIZE(file) || chunkSize == 0) break;
            SYNTH_FILE_SEEK(file, nextChunkPos);
        }
    }

    if (foundData) {
        outSampleRate = (tempSampleRate > 0) ? tempSampleRate : 44100;
        outChannels   = (tempChannels > 0)   ? tempChannels   : 2;
        outBits       = (tempBits > 0)       ? tempBits       : 16;
        outDataPos    = tempDataPos;
        outDataSize   = tempDataSize;
        return true;
    }
    return false;
}

// Background Streaming & Decoding Loader (Core 0)
void ESP32Synth::streamLoaderTask(void* param) {
    ESP32Synth* synth = (ESP32Synth*)param;
    uint8_t tempBuf[2048] __attribute__((aligned(4)));

    #if ENABLE_MP3
    int16_t* mp3FrameBuf = (int16_t*)heap_caps_malloc(2304 * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!mp3FrameBuf) {
        synth->streamTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }
    #endif

    // Core 0 CPU Load Tracking (~200ms evaluation window)
    uint32_t windowStart = esp_cpu_get_cycle_count();
    uint32_t activeCycles = 0;
    uint32_t windowTargetCycles = (SYNTH_GET_CPU_FREQ_MHZ() * 1000000) / 5;

    while (synth->_running) {
        uint32_t loopStart = esp_cpu_get_cycle_count();
        bool needMoreYield = true;

        for (int i = 0; i < MAX_STREAMS; i++) {
            StreamTrack* trk = &synth->streams[i];
            #if ENABLE_MP3
            if (!trk->active || (!trk->isMP3 && !SYNTH_FILE_VALID(trk->file))) continue;

            if (trk->isMP3) {
                if (trk->seekTarget >= 0) {
                trk->seeking = true;
                uint32_t sRate = (trk->sampleRate > 0) ? trk->sampleRate : 44100;
                if (trk->mp3Data != nullptr) {
                    if (trk->seekTarget == 0) {
                        trk->mp3Offset = 0;
                    } else if (trk->mp3Bitrate > 0) {
                        uint32_t targetMs = (uint32_t)(((uint64_t)trk->seekTarget * 1000ULL) / sRate);
                        uint32_t targetByte = (uint32_t)(((uint64_t)targetMs * trk->mp3Bitrate) / 8000ULL);
                        trk->mp3Offset = (targetByte < trk->dataSize) ? targetByte : (trk->dataSize ? trk->dataSize - 1 : 0);
                    }
                } else {
                    if (trk->seekTarget == 0) {
                        SYNTH_FILE_SEEK(trk->file, trk->dataStartPos);
                    } else if (trk->mp3Bitrate > 0) {
                        uint32_t targetMs = (uint32_t)(((uint64_t)trk->seekTarget * 1000ULL) / sRate);
                        uint32_t targetByte = trk->dataStartPos + (uint32_t)(((uint64_t)targetMs * trk->mp3Bitrate) / 8000ULL);
                        SYNTH_FILE_SEEK(trk->file, targetByte);
                    }
                    trk->mp3BytesLeft = 0;
                    trk->mp3ReadOffset = 0;
                }

                // Reset ring buffer
                trk->head = 0;
                trk->tail = 0;

                // Reset Helix decoder
                if (trk->mp3Decoder) {
                    MP3FreeDecoder((HMP3Decoder)trk->mp3Decoder);
                    trk->mp3Decoder = (void*)MP3InitDecoder();
                }

                trk->samplesPlayed = trk->seekTarget;
                trk->seekTarget = -1;
            }

                if (!trk->playing) continue;

                uint16_t freeSpace = (STREAM_BUF_SAMPLES + trk->tail - trk->head - 1) & STREAM_BUF_MASK;
                
                while (freeSpace >= 1152 && trk->playing) {
                    needMoreYield = false;

                    unsigned char* readPtr = nullptr;
                    int bytesLeft = 0;

                    if (trk->mp3Data != nullptr) {
                        bytesLeft = (int)(trk->dataSize - trk->mp3Offset);
                        if (bytesLeft <= 0) {
                            if (trk->loop) {
                                trk->mp3Offset = 0;
                                trk->samplesPlayed = 0;
                                bytesLeft = (int)trk->dataSize;
                            } else {
                                if (trk->head == trk->tail) trk->playing = false;
                                break;
                            }
                        }
                        readPtr = (unsigned char*)(trk->mp3Data + trk->mp3Offset);
                    } else {
                        if (trk->mp3BytesLeft < 1500 && SYNTH_FILE_VALID(trk->file)) {
                            if (trk->mp3BytesLeft > 0 && trk->mp3ReadOffset > 0) {
                                memmove(trk->mp3InBuf, trk->mp3InBuf + trk->mp3ReadOffset, trk->mp3BytesLeft);
                            }
                            trk->mp3ReadOffset = 0;

                            int toRead = SYNTH_MP3_IN_BUF_SIZE - trk->mp3BytesLeft;
                            if (toRead > 0 && SYNTH_FILE_AVAILABLE(trk->file)) {
                                size_t n = SYNTH_FILE_READ(trk->file, trk->mp3InBuf + trk->mp3BytesLeft, toRead);
                                trk->mp3BytesLeft += n;
                            }
                        }

                        if (trk->mp3BytesLeft <= 0) {
                            if (trk->loop) {
                                SYNTH_FILE_SEEK(trk->file, trk->dataStartPos);
                                trk->samplesPlayed = 0;
                                trk->mp3ReadOffset = 0;
                                trk->mp3BytesLeft = SYNTH_FILE_READ(trk->file, trk->mp3InBuf, SYNTH_MP3_IN_BUF_SIZE);
                                if (trk->mp3BytesLeft <= 0) {
                                    if (trk->head == trk->tail) trk->playing = false;
                                    break;
                                }
                            } else {
                                if (trk->head == trk->tail) trk->playing = false;
                                break;
                            }
                        }

                        bytesLeft = trk->mp3BytesLeft;
                        readPtr = trk->mp3InBuf + trk->mp3ReadOffset;
                    }

                    int syncOffset = MP3FindSyncWord(readPtr, bytesLeft);
                    if (syncOffset < 0) {
                        if (trk->mp3Data != nullptr) {
                            if (trk->loop) {
                                trk->mp3Offset = 0;
                                trk->samplesPlayed = 0;
                            } else {
                                if (trk->head == trk->tail) trk->playing = false;
                            }
                        } else {
                            if (trk->loop) {
                                SYNTH_FILE_SEEK(trk->file, trk->dataStartPos);
                                trk->samplesPlayed = 0;
                                trk->mp3BytesLeft = 0;
                                trk->mp3ReadOffset = 0;
                            } else {
                                if (trk->head == trk->tail) trk->playing = false;
                            }
                        }
                        break;
                    }

                    readPtr += syncOffset;
                    bytesLeft -= syncOffset;

                    if (trk->mp3Data != nullptr) {
                        trk->mp3Offset += syncOffset;
                    } else {
                        trk->mp3BytesLeft -= syncOffset;
                        trk->mp3ReadOffset += syncOffset;
                    }

                    unsigned char* decPtr = readPtr;
                    int decBytesLeft = bytesLeft;

                    int err = MP3Decode((HMP3Decoder)trk->mp3Decoder, &decPtr, &decBytesLeft, mp3FrameBuf, 0);

                    if (err == ERR_MP3_NONE || err == ERR_MP3_MAINDATA_UNDERFLOW) {
                        int bytesConsumed = (int)(decPtr - readPtr);
                        if (bytesConsumed > 0) {
                            if (trk->mp3Data != nullptr) {
                                trk->mp3Offset += bytesConsumed;
                            } else {
                                trk->mp3BytesLeft -= bytesConsumed;
                                trk->mp3ReadOffset += bytesConsumed;
                            }
                        }

                        if (err == ERR_MP3_NONE) {
                            MP3FrameInfo frameInfo;
                            MP3GetLastFrameInfo((HMP3Decoder)trk->mp3Decoder, &frameInfo);

                            if (frameInfo.bitrate > 0 && trk->mp3Bitrate != (uint32_t)frameInfo.bitrate) {
                                trk->isVBR = true;
                            }

                            if (UNLIKELY(trk->durationMs == 0 && frameInfo.bitrate > 0 && trk->dataSize > 0)) {
                                trk->durationMs = (uint32_t)(((uint64_t)trk->dataSize * 8000ULL) / (uint32_t)frameInfo.bitrate);
                                trk->mp3Bitrate = (uint32_t)frameInfo.bitrate;
                            }

                            int nCh = (frameInfo.nChans > 0) ? frameInfo.nChans : 2;
                            int samples = frameInfo.outputSamps / nCh;
                            uint16_t head = trk->head;

                            if (nCh == 2) {
                                for (int f = 0; f < samples; f++) {
                                    trk->buffer[head] = (int16_t)(((int32_t)mp3FrameBuf[f * 2] + (int32_t)mp3FrameBuf[f * 2 + 1]) >> 1);
                                    head = (head + 1) & STREAM_BUF_MASK;
                                }
                            } else {
                                for (int f = 0; f < samples; f++) {
                                    trk->buffer[head] = mp3FrameBuf[f];
                                    head = (head + 1) & STREAM_BUF_MASK;
                                }
                            }
                            trk->head = head;

                            // Libera o áudio suavemente apenas quando o buffer acumulou amostras suficientes
                            if (trk->seeking && ((STREAM_BUF_SAMPLES + head - trk->tail) & STREAM_BUF_MASK) >= 1152) {
                                trk->seeking = false;
                            }
                        }
                    } else {
                        if (trk->mp3Data != nullptr) {
                            trk->mp3Offset++;
                        } else {
                            trk->mp3BytesLeft--;
                            trk->mp3ReadOffset++;
                        }
                    }

                    freeSpace = (STREAM_BUF_SAMPLES + trk->tail - trk->head - 1) & STREAM_BUF_MASK;
                }

                continue; // Skip WAV handling
            }
            #else
            if (!trk->active || !SYNTH_FILE_VALID(trk->file)) continue;
            #endif

            // WAV Seek
            if (trk->seekTarget >= 0) {
                uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
                if (bytesPerSample == 0) bytesPerSample = 2;
                uint32_t targetByte = trk->dataStartPos + (trk->seekTarget * bytesPerSample);
                if (targetByte < trk->dataStartPos + trk->dataSize) {
                    SYNTH_FILE_SEEK(trk->file, targetByte);
                    trk->samplesPlayed = trk->seekTarget;
                }
                trk->head = 0;
                trk->tail = 0;
                trk->seekTarget = -1;
            }

            if (!trk->playing) continue;

            uint8_t bytesPerCh = trk->bitsPerSample / 8;
            if (bytesPerCh == 0) bytesPerCh = 2;
            uint16_t frameSize = bytesPerCh * trk->numChannels;
            if (frameSize == 0) frameSize = 2;

            uint16_t maxFramesChunk = sizeof(tempBuf) / frameSize;
            uint16_t minThreshold = maxFramesChunk / 2;
            uint16_t freeSpace = (STREAM_BUF_SAMPLES + trk->tail - trk->head - 1) & STREAM_BUF_MASK;

            // RAW OPTIMIZATION: Burst read loop (keeps reading until ring buffer is topped off)
            while (freeSpace >= minThreshold && trk->playing) {
                needMoreYield = false;

                uint16_t framesToRead = (freeSpace > maxFramesChunk) ? maxFramesChunk : freeSpace;
                size_t bytesToRead = framesToRead * frameSize;

                uint32_t currentPos = SYNTH_FILE_POS(trk->file);
                uint32_t absoluteEnd = trk->loop ? trk->loopEndBytes : (trk->dataStartPos + trk->dataSize);

                if (currentPos + bytesToRead > absoluteEnd) {
                    bytesToRead = absoluteEnd - currentPos;
                    if (bytesToRead == 0) {
                        if (trk->loop) {
                            SYNTH_FILE_SEEK(trk->file, trk->loopStartBytes);
                            trk->samplesPlayed = (trk->loopStartBytes - trk->dataStartPos) / frameSize;
                        } else {
                            trk->playing = false;
                        }
                        break; // End of file or loop wrap
                    }
                }

                size_t bytesRead = (bytesToRead > 0) ? SYNTH_FILE_READ(trk->file, tempBuf, bytesToRead) : 0;
                if (bytesRead == 0) {
                    if (trk->loop) {
                        SYNTH_FILE_SEEK(trk->file, trk->loopStartBytes);
                    } else {
                        trk->playing = false;
                    }
                    break;
                }

                uint16_t framesRead = bytesRead / frameSize;
                uint8_t* ptr = tempBuf;
                uint16_t head = trk->head;

                // Decode & Downmix to 16-bit Mono (16, 24, 32, 8-bit)
                if (trk->bitsPerSample == 16) {
                    if (trk->numChannels == 2) {
                        for (uint16_t f = 0; f < framesRead; f++, ptr += 4) {
                            trk->buffer[head] = (int16_t)(((int32_t)((int16_t*)ptr)[0] + (int32_t)((int16_t*)ptr)[1]) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else {
                        uint16_t spaceToEnd = STREAM_BUF_SAMPLES - head;
                        if (framesRead <= spaceToEnd) {
                            memcpy((void*)&trk->buffer[head], ptr, framesRead * 2);
                            head = (head + framesRead) & STREAM_BUF_MASK;
                        } else {
                            memcpy((void*)&trk->buffer[head], ptr, spaceToEnd * 2);
                            memcpy((void*)&trk->buffer[0], ptr + (spaceToEnd * 2), (framesRead - spaceToEnd) * 2);
                            head = framesRead - spaceToEnd;
                        }
                    }
                } else if (trk->bitsPerSample == 24) {
                    if (trk->numChannels == 2) {
                        for (uint16_t f = 0; f < framesRead; f++, ptr += 6) {
                            int32_t l = (ptr[1] | ((int8_t)ptr[2] << 8));
                            int32_t r = (ptr[4] | ((int8_t)ptr[5] << 8));
                            trk->buffer[head] = (int16_t)((l + r) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else {
                        for (uint16_t f = 0; f < framesRead; f++, ptr += 3) {
                            trk->buffer[head] = (int16_t)(ptr[1] | ((int8_t)ptr[2] << 8));
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                } else if (trk->bitsPerSample == 32) {
                    if (trk->numChannels == 2) {
                        for (uint16_t f = 0; f < framesRead; f++, ptr += 8) {
                            int32_t l = ((int32_t*)ptr)[0] >> 16;
                            int32_t r = ((int32_t*)ptr)[1] >> 16;
                            trk->buffer[head] = (int16_t)((l + r) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else {
                        for (uint16_t f = 0; f < framesRead; f++, ptr += 4) {
                            trk->buffer[head] = (int16_t)(((int32_t*)ptr)[0] >> 16);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                } else if (trk->bitsPerSample == 8) {
                    if (trk->numChannels == 2) {
                        for (uint16_t f = 0; f < framesRead; f++, ptr += 2) {
                            int32_t l = ((int16_t)ptr[0] - 128) << 8;
                            int32_t r = ((int16_t)ptr[1] - 128) << 8;
                            trk->buffer[head] = (int16_t)((l + r) >> 1);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    } else {
                        for (uint16_t f = 0; f < framesRead; f++, ptr++) {
                            trk->buffer[head] = (int16_t)(((int16_t)*ptr - 128) << 8);
                            head = (head + 1) & STREAM_BUF_MASK;
                        }
                    }
                }

                trk->head = head;
                freeSpace = (STREAM_BUF_SAMPLES + trk->tail - trk->head - 1) & STREAM_BUF_MASK;
            }
        }
        uint32_t loopEnd = esp_cpu_get_cycle_count();
        activeCycles += (loopEnd - loopStart);

        uint32_t now = esp_cpu_get_cycle_count();
        uint32_t elapsed = now - windowStart;
        if (elapsed >= windowTargetCycles) {
            float load = ((float)activeCycles / (float)elapsed) * 100.0f;
            synth->_streamLoad = (load > 100.0f) ? 100.0f : load;
            activeCycles = 0;
            windowStart = now;
        }

        vTaskDelay(pdMS_TO_TICKS(needMoreYield ? 5 : 1));
    }
    
    synth->_streamLoad = 0.0f;

    #if ENABLE_MP3
    if (mp3FrameBuf) heap_caps_free(mp3FrameBuf);
    #endif

    // Shutdown
    synth->streamTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Fetch sample from wavetable
IRAM_ATTR int16_t ESP32Synth::fetchWavetableSample(uint16_t id, uint32_t phase) {
    if (id >= MAX_WAVETABLES) return 0;
    const auto& e = wavetables[id];
    if (!e.data || e.size == 0) return 0;

    uint32_t idx = (uint32_t)(((uint64_t)phase * e.size) >> 32);

    if (e.depth == BITS_8) {
        return (((uint8_t*)e.data)[idx] - 128) << 8;
    } else if (e.depth == BITS_4) {
        const uint8_t* p = (const uint8_t*)e.data;
        uint8_t val = p[idx / 2];
        val = ((idx & 1) == 0) ? (val & 0x0F) : (val >> 4);
        return ((int16_t)val - 8) * 4096;
    }

    // Default to 16-bit
    return ((int16_t*)e.data)[idx];
}

void ESP32Synth::generateSamples(int16_t* outBuffer, int numSamples) {
    if (!outBuffer || numSamples <= 0 || !_running) return;

    // Extremely fast and safe allocation on local stack, preventing heap fragmentation
    const int CHUNK_SIZE = 128;
    int32_t busMemory[MAX_BUSES][CHUNK_SIZE] __attribute__((aligned(16)));
    int32_t* busBuffers[MAX_BUSES];
    for (int i = 0; i < MAX_BUSES; i++) busBuffers[i] = busMemory[i];
    int32_t tempOut[CHUNK_SIZE] __attribute__((aligned(16)));

    int samplesRendered = 0;
    while (samplesRendered < numSamples) {
        int toRender = numSamples - samplesRendered;
        if (toRender > CHUNK_SIZE) toRender = CHUNK_SIZE;

        int simdRender = (toRender + 3) & ~3;
        uint32_t start_cycles = esp_cpu_get_cycle_count();

        render(tempOut, busBuffers, simdRender);

        uint32_t used_cycles = esp_cpu_get_cycle_count() - start_cycles;
        uint32_t max_cycles = (SYNTH_GET_CPU_FREQ_MHZ() * 1000000 / _sampleRate) * simdRender;
        if (max_cycles > 0) _dspLoad = ((float)used_cycles / (float)max_cycles) * 100.0f;

        if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
            for (int i = 0; i < toRender; i++) outBuffer[samplesRendered + i] = (int16_t)(tempOut[i] >> 16);
        } else {
            memcpy(outBuffer + samplesRendered, tempOut, toRender * sizeof(int16_t));
        }
        samplesRendered += toRender;
    }
}

void ESP32Synth::generateSamplesStereo(int16_t* outBufferLR, int numSamplePairs) {
    if (!outBufferLR || numSamplePairs <= 0 || !_running) return;

    const int CHUNK_SIZE = 128;
    int32_t busMemory[MAX_BUSES][CHUNK_SIZE] __attribute__((aligned(16)));
    int32_t* busBuffers[MAX_BUSES];
    for (int i = 0; i < MAX_BUSES; i++) busBuffers[i] = busMemory[i];
    int32_t tempOut[CHUNK_SIZE] __attribute__((aligned(16)));

    int samplesRendered = 0;
    while (samplesRendered < numSamplePairs) {
        int toRender = numSamplePairs - samplesRendered;
        if (toRender > CHUNK_SIZE) toRender = CHUNK_SIZE;

        int simdRender = (toRender + 3) & ~3;
        uint32_t start_cycles = esp_cpu_get_cycle_count();

        render(tempOut, busBuffers, simdRender);

        uint32_t used_cycles = esp_cpu_get_cycle_count() - start_cycles;
        uint32_t max_cycles = (SYNTH_GET_CPU_FREQ_MHZ() * 1000000 / _sampleRate) * simdRender;
        if (max_cycles > 0) _dspLoad = ((float)used_cycles / (float)max_cycles) * 100.0f;

        if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
            for (int i = 0; i < toRender; i++) {
                int16_t smp = (int16_t)(tempOut[i] >> 16);
                outBufferLR[(samplesRendered + i) * 2]     = smp;
                outBufferLR[(samplesRendered + i) * 2 + 1] = smp;
            }
        } else {
            int16_t* t16 = (int16_t*)tempOut;
            for (int i = 0; i < toRender; i++) {
                int16_t smp = t16[i];
                outBufferLR[(samplesRendered + i) * 2]     = smp;
                outBufferLR[(samplesRendered + i) * 2 + 1] = smp;
            }
        }
        samplesRendered += toRender;
    }
}

// Control Rate Logic (LFOs, Envelopes, Arps). Runs at ~100Hz.
void IRAM_ATTR ESP32Synth::processControl() {
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];

        // RAW OPTIMIZATION: If the voice is not active, skip everything.
        // Recovers significant processing power when not all voices are in use.
        if (!vo->active) continue;

        // --- LFOs (Vibrato and Tremolo) ---
        int32_t vibCentiHz = 0;
        if (vo->vibDepthInc > 0) {
            vo->vibPhase += (vo->vibRateInc * controlIntervalSamples);
            int16_t lfoVal = sineLUT[vo->vibPhase >> SINE_SHIFT];
            vo->vibOffset = ((int64_t)vo->vibDepthInc * lfoVal) >> 15;
            // Extremely fast conversion from phase increment scale back to centiHz (no divisions)
            vibCentiHz = (int32_t)(((int64_t)vo->vibOffset * _sampleRate * 100) >> 32);
        } else {
            vo->vibOffset = 0;
        }

        if (vo->trmDepth > 0) {
            vo->trmPhase += (vo->trmRateInc * controlIntervalSamples);
            int32_t lfo = sineLUT[vo->trmPhase >> SINE_SHIFT] + 32768;
            int32_t depth = vo->trmDepth;
            int32_t reduction = (lfo * depth) >> 16;
            vo->trmModGain = 256 - reduction;
        } else {
            vo->trmModGain = 256;
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
            if (vo->slideFreqRem != 0 && vo->slideFreqTicksTotal > 0) {
                vo->slideFreqRemAcc += vo->slideFreqRem;
                int32_t sign = (vo->slideFreqRem > 0) ? 1 : -1;
                if (abs(vo->slideFreqRemAcc) >= (int32_t)vo->slideFreqTicksTotal) {
                    vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + sign);
                    vo->slideFreqRemAcc -= sign * (int32_t)vo->slideFreqTicksTotal;
                }
            }
            vo->slideFreqTicksRemaining--;

            vo->freqVal = (uint32_t)(((uint64_t)vo->phaseInc * _sampleRate * 100) >> 32);

            if (vo->slideFreqTicksRemaining == 0) {
                vo->phaseInc = vo->slideFreqTargetInc;
                vo->freqVal = vo->slideFreqTargetCenti;
                vo->slideFreqActive = false;
            }
        }

        // --- Recalculate Sample/Stream Increment (for Vibrato and/or Slide) ---
        if (vo->slideFreqActive || vo->vibDepthInc > 0) {
            int32_t modFreq = (int32_t)vo->freqVal + vibCentiHz;
            if (modFreq < 0) modFreq = 0;

            if (vo->type == WAVE_STREAM && vo->streamTrackId >= 0) {
                StreamTrack* trk = &streams[vo->streamTrackId];
                if (trk->rootFreqCentiHz > 0) {
                    uint64_t ratio1616 = ((uint64_t)modFreq << 16) / trk->rootFreqCentiHz;
                    vo->sampleInc1616 = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);
                }
            } else if (vo->type == WAVE_SAMPLE) {
                if (vo->instSample != nullptr) {
                    const SampleData* sData = nullptr;
                    uint32_t root = 0;
                    for (int i = 0; i < vo->instSample->numZones; i++) {
                        const SampleZone* z = &vo->instSample->zones[i];
                        if (vo->freqVal >= z->lowFreq && vo->freqVal <= z->highFreq) {
                            if (z->sampleId < MAX_SAMPLES) {
                                sData = &registeredSamples[z->sampleId];
                                root = (z->rootOverride > 0) ? z->rootOverride : sData->rootFreqCentiHz;
                            }
                            break;
                        }
                    }
                    if (sData && sData->data && root > 0) {
                        uint64_t ratio1616 = ((uint64_t)modFreq << 16) / root;
                        vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
                    }
                } else {
                    const SampleData* sData = &registeredSamples[vo->curSampleId];
                    if (sData->data && sData->rootFreqCentiHz > 0) {
                        uint64_t ratio1616 = ((uint64_t)modFreq << 16) / sData->rootFreqCentiHz;
                        vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
                    }
                }
            }
        }

        // --- Volume Portamento (Slide) ---
        if (vo->slideVolActive && vo->slideVolTicksRemaining > 0) {
            vo->slideVolCurr += vo->slideVolInc;
            int64_t vCurr = vo->slideVolCurr >> 16;
            if (vCurr < 0) vCurr = 0;
            else if (vCurr > 65535) vCurr = 65535;
            vo->vol = (uint16_t)vCurr;

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
            // Instruments are NATIVELY stored in 8-bit in memory to save RAM.
            // The code dynamically shifts to 16-bit (<< 8) without breaking your legacy code!
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
} // John 3:16