// ESP32Synth.cpp - Implementation
// Se não fosse por Deus, o ESP32Synth nunca teria dado certo, então agradeçam a Ele por essa maravilha de código que é o ESP32Synth. Amém!
#pragma GCC optimize ("O3,unroll-loops")
#include "ESP32Synth.h"

// ====================================================================================
// SIMD Definitions para ESP32-S3 (Performance Bruta Xtensa LX7 128-bit)
// ====================================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
typedef int32_t v4i32 __attribute__((vector_size(16)));
typedef uint32_t v4u32 __attribute__((vector_size(16)));
#endif

// ====================================================================================
//    SINE WAVE LOOK-UP TABLE
// ====================================================================================

// Shared sine LUT
int16_t sineLUT[SINE_LUT_SIZE] __attribute__((aligned(16)));

// Sample storage
SampleData registeredSamples[MAX_SAMPLES];

// --- Other Modules includes ---
#include "ESP32Synth_Renders.hpp"
#include "ESP32Synth_Begins.hpp"
#include "ESP32Synth_Core.hpp"
#include "ESP32Synth_Core_Getters.hpp"
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

// --- Other Methods ---

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

const char* ESP32Synth::getChipModel() {
    return SYNTH_CHIP;
}

int32_t ESP32Synth::getSampleRate() {
    return _sampleRate;
}

float ESP32Synth::getCPULoad() {
    return _dspLoad;
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
    if (_sampleRate <= 8000) blockSamples = SYNTH_DMA_BUF_LEN / 8;
    else if (_sampleRate <= 16000) blockSamples = SYNTH_DMA_BUF_LEN / 4;
    else if (_sampleRate <= 32000) blockSamples = SYNTH_DMA_BUF_LEN / 2;

    if (blockSamples < 16) blockSamples = 16; 

    // BLINDAGEM SIMD: Garante que o bloco seja SEMPRE múltiplo de 4.
    // Isso impede que os loops (v4i32) escrevam fora da memória se o usuário usar buffers exóticos.
    blockSamples = (blockSamples + 3) & ~3;

    int32_t* buf = (int32_t*)heap_caps_aligned_alloc(16, blockSamples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* stereoBuf = heap_caps_aligned_alloc(16, blockSamples * 2 * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int32_t* mixBuf = (int32_t*)heap_caps_aligned_alloc(16, blockSamples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!buf || !stereoBuf || !mixBuf) {
        _running = false;
        if(buf) heap_caps_free(buf);
        if(stereoBuf) heap_caps_free(stereoBuf);
        if(mixBuf) heap_caps_free(mixBuf);
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

        // GPTimer totalmente deletado daqui. Não inicializamos mais ele, o ISR do LEDC cuida de tudo e se auto chama!
        
        xSemaphoreGive(pwm_sema); 
    }

    uint32_t cpu_freq = ESP.getCpuFreqMHz() * 1000000;
    uint32_t max_cycles_per_block = (cpu_freq / _sampleRate) * blockSamples;

    while (_running) {
        uint32_t start_cycles = esp_cpu_get_cycle_count();

        render(buf, mixBuf, blockSamples); 

        uint32_t end_cycles = esp_cpu_get_cycle_count();
        uint32_t used_cycles = end_cycles - start_cycles;
        
        _dspLoad = ((float)used_cycles / (float)max_cycles_per_block) * 100.0f;

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
                uint64_t* out64 = (uint64_t*)stereoBuf;
                for (int i = 0; i < blockSamples; i++) {
                    uint64_t s = (uint32_t)buf[i]; 
                    out64[i] = (s << 32) | s;      
                }
                i2s_channel_write(tx_handle, stereoBuf, blockSamples * 2 * sizeof(int32_t), &written, portMAX_DELAY);
            } else { 
                int16_t* buf16 = (int16_t*)buf;
                uint32_t* out32 = (uint32_t*)stereoBuf;
                for (int i = 0; i < blockSamples; i++) {
                    uint32_t s = (uint16_t)buf16[i];
                    out32[i] = (s << 16) | s;      
                }
                i2s_channel_write(tx_handle, stereoBuf, blockSamples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
            }
       } else if (currentMode == SMODE_PWM) {
            xSemaphoreTake(pwm_sema, portMAX_DELAY);
            int render_target = 1 - pwm_active_buf;
            int16_t* buf16 = (int16_t*)buf;
            memcpy(pwm_ping_pong_buf[render_target], buf16, blockSamples * sizeof(int16_t));
            
        } else if (currentMode == SMODE_PDM) { 
            i2s_channel_write(tx_handle, buf, blockSamples * sizeof(int16_t), &written, portMAX_DELAY);
            
        } else if (currentMode == SMODE_CUSTOM) {
            if (_customOutput) {
                int16_t* buf16 = (int16_t*)buf;
                _customOutput(buf16, blockSamples);
                
                // PACING INTELIGENTE: Impede uso de 100% de CPU caso o callback do usuário não trave/bloqueie.
                uint32_t full_cycles = esp_cpu_get_cycle_count() - start_cycles;
                if (full_cycles < max_cycles_per_block) {
                    uint32_t wait_us = (max_cycles_per_block - full_cycles) / ESP.getCpuFreqMHz();
                    if (wait_us > 2000) vTaskDelay(pdMS_TO_TICKS(wait_us / 1000));
                    else delayMicroseconds(wait_us);
                }
            }
        }
    }

    if (buf) heap_caps_free(buf);
    if (stereoBuf) heap_caps_free(stereoBuf);
    if (mixBuf) heap_caps_free(mixBuf);
    audioTaskHandle = NULL;
    vTaskDelete(NULL); 
}
    
// Core mixer
void IRAM_ATTR ESP32Synth::render(void* buffer, int32_t* mixBuffer, int samples) {
    // Zero o buffer perfeitamente alinhado garantindo thread-safety
    memset(mixBuffer, 0, samples * sizeof(int32_t));

    controlSampleCounter += (uint32_t) samples;
    while (controlSampleCounter >= controlIntervalSamples) {
        processControl();
        controlSampleCounter -= controlIntervalSamples;
    }

    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];
        if (!vo->active) continue;

        int32_t startEnv, envStep;
        updateAdsrBlock(vo, samples, startEnv, envStep);
        if (startEnv == 0 && vo->currEnvVal == 0 && vo->envState != ENV_ATTACK) continue;

        if (!vo->inst) { 
            switch (vo->type) {
                case WAVE_SAMPLE:    renderBlockSample(vo, mixBuffer, samples, startEnv, envStep); break;
                case WAVE_STREAM:    renderBlockStream(vo, this->streams, mixBuffer, samples, startEnv, envStep); break;
                case WAVE_WAVETABLE: renderBlockWavetable(vo, mixBuffer, samples, startEnv, envStep); break;
                case WAVE_NOISE:     renderBlockNoise(vo, mixBuffer, samples, startEnv, envStep); break;
                case WAVE_CUSTOM:    if (vo->customWaveFunc) vo->customWaveFunc(vo, mixBuffer, samples, startEnv, envStep); break;
                default:             renderBlockBasic(vo, mixBuffer, samples, startEnv, envStep); break; 
            }
        } else { 
            if (vo->currWaveIsBasic) {
                WaveType dynamicType = (WaveType)vo->currWaveType;
                if (dynamicType == WAVE_NOISE) {
                    renderBlockNoise(vo, mixBuffer, samples, startEnv, envStep);
                } else {
                    WaveType original = vo->type;
                    vo->type = dynamicType; 
                    renderBlockBasic(vo, mixBuffer, samples, startEnv, envStep);
                    vo->type = original; 
                }
            } else {
                renderBlockWavetable(vo, mixBuffer, samples, startEnv, envStep);
            }
        }
    }
    
    if (_customDSP) {
        _customDSP(mixBuffer, samples); 
    }

    int32_t mVol = _masterVolume; 
    uint32_t mask32 = 0xFFFFFFFFUL; 
    uint32_t mask16 = 0xFFFFFFFFUL; 

    if (_bitcrush > 0) {
        mask32 = (_bitcrush >= 32) ? 0 : (0xFFFFFFFFUL << (32 - _bitcrush));
        mask16 = (_bitcrush >= 16) ? 0 : (0xFFFFFFFFUL << (16 - _bitcrush));
    }
    
    if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
        int32_t* buf32 = (int32_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int64_t val = (int64_t)mixBuffer[i] * mVol; 
            if (val > 2147483647LL) val = 2147483647LL; 
            else if (val < -2147483648LL) val = -2147483648LL;
            buf32[i] = (int32_t)val & mask32;
        }
    } else { 
        int16_t* buf16 = (int16_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int32_t val = (int32_t)(((int64_t)mixBuffer[i] * mVol) >> 16);
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
        uint8_t buf[512];
        int offset = 0;
        
        while (offset < 8192 && !foundRiff && file.available()) {
            file.seek(offset);
            int bytesRead = file.read(buf, 512);
            if (bytesRead < 4) break;
            
            for (int i = 0; i < bytesRead - 3; i++) {
                if (buf[i] == 'R' && buf[i+1] == 'I' && buf[i+2] == 'F' && buf[i+3] == 'F') {
                    file.seek(offset + i);
                    if (file.read(riff, 12) == 12) foundRiff = true;
                    break;
                }
            }
            offset += 512 - 3; // Sobreposição de 3 bytes para nunca cortar a palavra RIFF no meio do bloco
        }
        if (!foundRiff) return false;
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
                        uint16_t spaceToEnd = STREAM_BUF_SAMPLES - head;
                        if (framesRead <= spaceToEnd) {
                            memcpy((void*)&trk->buffer[head], ptr, framesRead * 2);
                            head = (head + framesRead) & STREAM_BUF_MASK;
                        } else {
                            // Copia até o fim do buffer
                            memcpy((void*)&trk->buffer[head], ptr, spaceToEnd * 2);
                            // Dá a volta e copia o resto no começo
                            memcpy((void*)&trk->buffer[0], ptr + (spaceToEnd * 2), (framesRead - spaceToEnd) * 2);
                            head = framesRead - spaceToEnd;
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

void ESP32Synth::generateSamples(int16_t* outBuffer, int numSamples) {
    if (!outBuffer || numSamples <= 0 || !_running) return;

    // Alocação extremamente rápida e segura na Stack local, sem heap fragmentation
    const int CHUNK_SIZE = 128; 
    int32_t mixBuffer[CHUNK_SIZE] __attribute__((aligned(16)));
    int16_t tempOut[CHUNK_SIZE] __attribute__((aligned(16)));

    int samplesRendered = 0;
    while (samplesRendered < numSamples) {
        int toRender = numSamples - samplesRendered;
        if (toRender > CHUNK_SIZE) toRender = CHUNK_SIZE;

        // Blindagem SIMD: O render precisa processar em blocos de 4
        int simdRender = (toRender + 3) & ~3; 

        uint32_t start_cycles = esp_cpu_get_cycle_count();
        
        render(tempOut, mixBuffer, simdRender);

        uint32_t used_cycles = esp_cpu_get_cycle_count() - start_cycles;
        uint32_t max_cycles = (ESP.getCpuFreqMHz() * 1000000 / _sampleRate) * simdRender;
        if (max_cycles > 0) _dspLoad = ((float)used_cycles / (float)max_cycles) * 100.0f;

        // Copia os samples estritamente requisitados para evitar overflow na memória do usuário
        memcpy(outBuffer + samplesRendered, tempOut, toRender * sizeof(int16_t));
        samplesRendered += toRender;
    }
}

void ESP32Synth::generateSamplesStereo(int16_t* outBufferLR, int numSamplePairs) {
    if (!outBufferLR || numSamplePairs <= 0 || !_running) return;

    const int CHUNK_SIZE = 128;
    int32_t mixBuffer[CHUNK_SIZE] __attribute__((aligned(16)));
    int16_t tempOut[CHUNK_SIZE] __attribute__((aligned(16)));

    int samplesRendered = 0;
    while (samplesRendered < numSamplePairs) {
        int toRender = numSamplePairs - samplesRendered;
        if (toRender > CHUNK_SIZE) toRender = CHUNK_SIZE;

        int simdRender = (toRender + 3) & ~3; 

        uint32_t start_cycles = esp_cpu_get_cycle_count();
        
        render(tempOut, mixBuffer, simdRender);

        uint32_t used_cycles = esp_cpu_get_cycle_count() - start_cycles;
        uint32_t max_cycles = (ESP.getCpuFreqMHz() * 1000000 / _sampleRate) * simdRender;
        if (max_cycles > 0) _dspLoad = ((float)used_cycles / (float)max_cycles) * 100.0f;

        // Duplica para Stereo Intercalado (L, R, L, R) perfeitinho pro Bluetooth (A2DP) / Wi-Fi
        for (int i = 0; i < toRender; i++) {
            int16_t smp = tempOut[i];
            outBufferLR[(samplesRendered + i) * 2]     = smp; // Canal Esquerdo
            outBufferLR[(samplesRendered + i) * 2 + 1] = smp; // Canal Direito
        }

        samplesRendered += toRender;
    }
}

// Control Rate Logic (LFOs, Envelopes, Arps). Runs at ~100Hz.
void IRAM_ATTR ESP32Synth::processControl() {
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];

        // OTIMIZAÇÃO BRUTA: Se a voz não está ativa, corta tudo.
        // Recupera um poder de processamento absurdo quando não estamos usando todas as vozes.
        if (!vo->active) continue;

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
