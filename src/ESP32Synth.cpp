/**
 * @file ESP32Synth.cpp
 * @brief ESP32Synth Implementation
 */

#include "ESP32Synth.h"

// Tabela de Seno Global para osciladores e LFOs
// Global Sine Table for oscillators and LFOs
#define SINE_LUT_SIZE 4096
#define SINE_LUT_MASK 4095
#define SINE_SHIFT    20    // 32 total bits - 12 table bits = 20 bits for phase shift

int32_t sineLUT[SINE_LUT_SIZE];
SampleData registeredSamples[MAX_SAMPLES];

// Renderiza um bloco de áudio para uma voz de sample.
// Renders an audio block for a sample voice.
static void IRAM_ATTR renderBlockSample(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (vo->sampleFinished) return;
    const SampleData* sData = &registeredSamples[vo->curSampleId];
    if (!sData->data) return;
    const int16_t* data = sData->data;
    const uint32_t len = sData->length;
    uint64_t pos = vo->samplePos1616;
    const uint32_t inc = vo->sampleInc1616;
    const uint32_t lStart = vo->sampleLoopStart;
    const uint32_t lEnd = (vo->sampleLoopEnd > 0 && vo->sampleLoopEnd <= len) ? vo->sampleLoopEnd : len;
    int32_t currentEnv = startEnv;
    int32_t volBase = (int32_t)vo->vol * vo->trmModGain >> 8; 
    bool dir = vo->sampleDirection;
    for (int i = 0; i < samples; i++) {
        uint32_t idx = (uint32_t)(pos >> 16);
        if (idx >= len) idx = (dir) ? len - 1 : 0;
        int16_t sampleVal = data[idx];
        int32_t finalVol = (currentEnv >> 20) * volBase; 
        
        mixBuffer[i] += (sampleVal * finalVol) >> 16;
        currentEnv += envStep;
        if (dir) {
            pos += inc;
            if ((pos >> 16) >= lEnd) {
                if (vo->sampleLoopMode == LOOP_FORWARD) pos = (uint64_t)lStart << 16;
                else if (vo->sampleLoopMode == LOOP_PINGPONG) { dir = false; pos = (uint64_t)lEnd << 16; } 
                else if (vo->sampleLoopMode == LOOP_OFF) { vo->sampleFinished = true; break; }
            }
        } else {
            if (pos >= inc) pos -= inc; else pos = 0;
            if ((pos >> 16) <= lStart) {
                if (vo->sampleLoopMode == LOOP_PINGPONG) { dir = true; pos = (uint64_t)lStart << 16; } 
                else if (vo->sampleLoopMode == LOOP_REVERSE) { pos = (uint64_t)lEnd << 16; } 
                else if (vo->sampleLoopMode == LOOP_OFF) { vo->sampleFinished = true; break; }
            }
        }
    }
    vo->samplePos1616 = pos;
    vo->sampleDirection = dir;
}

// Renderiza um bloco de áudio para uma voz de wavetable.
// Renders an audio block for a wavetable voice.
static void IRAM_ATTR renderBlockWavetable(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    if (!vo->wtData) return;

    // Common variables loaded from voice struct to be kept in registers
    int32_t currentEnv = startEnv;
    int32_t volBase = (int32_t)vo->vol * vo->trmModGain >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    const uint32_t size = vo->wtSize;

    // Branch once on bit depth, then enter a dedicated tight loop
    switch (vo->depth) {
        case BITS_16: {
            const int16_t* data = (const int16_t*)vo->wtData;
            for (int i = 0; i < samples; i++) {
                uint32_t idx = (uint32_t)(((uint64_t)ph * size) >> 32);
                int16_t s = data[idx];
                int32_t finalVol = (currentEnv >> 20) * volBase;
                mixBuffer[i] += (s * finalVol) >> 16;
                ph += inc;
                currentEnv += envStep;
            }
            break;
        }

        case BITS_8: {
            const uint8_t* data = (const uint8_t*)vo->wtData;
            for (int i = 0; i < samples; i++) {
                uint32_t idx = (uint32_t)(((uint64_t)ph * size) >> 32);
                int16_t s = ((int16_t)data[idx] - 128) << 8;
                int32_t finalVol = (currentEnv >> 20) * volBase;
                mixBuffer[i] += (s * finalVol) >> 16;
                ph += inc;
                currentEnv += envStep;
            }
            break;
        }

        case BITS_4: {
            const uint8_t* data = (const uint8_t*)vo->wtData;
            for (int i = 0; i < samples; i++) {
                uint32_t idx = (uint32_t)(((uint64_t)ph * size) >> 32);
                
                // Branchless 4-bit unpacking using bitwise logic
                uint8_t shift = (idx & 1) << 2; // 0 for even idx, 4 for odd idx
                uint8_t val = (data[idx >> 1] >> shift) & 0x0F;
                int16_t s = ((int16_t)val - 8) * 4096;

                int32_t finalVol = (currentEnv >> 20) * volBase;
                mixBuffer[i] += (s * finalVol) >> 16;
                ph += inc;
                currentEnv += envStep;
            }
            break;
        }
    }

    vo->phase = ph; // Store the updated phase back into the voice struct
}

// Renderiza um bloco de áudio para uma voz de onda básica (saw, sine, pulse, triangle).
// Renders an audio block for a basic wave voice (saw, sine, pulse, triangle).
static void IRAM_ATTR renderBlockBasic(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = (int32_t)vo->vol * vo->trmModGain >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset; 
    const WaveType type = vo->type;
    const uint32_t pw = vo->pulseWidth;
    if (type == WAVE_SAW) {
        for (int i = 0; i < samples; i++) {
            int16_t s = (int16_t)(ph >> 16);
            int32_t finalVol = (currentEnv >> 20) * volBase; 
            mixBuffer[i] += (s * finalVol) >> 16;
            ph += inc; currentEnv += envStep;
        }
    } 
    else if (type == WAVE_SINE) {
        for (int i = 0; i < samples; i++) {
            int32_t s = sineLUT[ph >> SINE_SHIFT]; 
            int32_t finalVol = (currentEnv >> 20) * volBase; 
            mixBuffer[i] += (int32_t)(((int64_t)s * finalVol) >> 31);
            ph += inc; currentEnv += envStep;
        }
    }
    else if (type == WAVE_PULSE) {
        for (int i = 0; i < samples; i++) {
            int16_t s = (ph < pw) ? 20000 : -20000;
            int32_t finalVol = (currentEnv >> 20) * volBase; 
            mixBuffer[i] += (s * finalVol) >> 16;
            ph += inc; currentEnv += envStep;
        }
    }
    else if (type == WAVE_TRIANGLE) {
        for (int i = 0; i < samples; i++) {
            int16_t saw = (int16_t)(ph >> 16);
            int16_t s = (int16_t)(((saw ^ (saw >> 15)) * 2) - 32767);
            int32_t finalVol = (currentEnv >> 20) * volBase; 
            mixBuffer[i] += (s * finalVol) >> 16;
            ph += inc; currentEnv += envStep;
        }
    }
    vo->phase = ph;
}

// Renderiza um bloco de áudio para uma voz de ruído.
// Renders an audio block for a noise voice.
static void IRAM_ATTR renderBlockNoise(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = (int32_t)vo->vol * vo->trmModGain >> 8;
    uint32_t rng = vo->rngState;
    uint32_t ph = vo->phase;
    uint32_t inc = (vo->phaseInc + vo->vibOffset) << 4; 
    int16_t currentSample = vo->noiseSample;
    for (int i = 0; i < samples; i++) {
        uint32_t nextPh = ph + inc;
        if (nextPh < ph) { 
             rng = (rng * 1664525) + 1013904223; 
             currentSample = (int16_t)(rng >> 16);
        }
        ph = nextPh;
        int32_t finalVol = (currentEnv >> 20) * volBase;
        mixBuffer[i] += (currentSample * finalVol) >> 16;
        currentEnv += envStep;
    }
    vo->rngState = rng;
    vo->phase = ph;
    vo->noiseSample = currentSample;
}

ESP32Synth::ESP32Synth() {
    _sampleRate = 48000; // Valor padrão seguro
    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active = false;
        voices[i].type = WAVE_SINE;
        voices[i].envState = ENV_IDLE;
        voices[i].rngState = 12345 + (i * 999);
        voices[i].rateAttack = ENV_MAX;
        voices[i].rateDecay = 0;
        voices[i].levelSustain = ENV_MAX;
        voices[i].rateRelease = ENV_MAX;
        voices[i].vibPhase = 0;
        voices[i].trmPhase = 0;
        voices[i].pulseWidth = 0x80000000;
        voices[i].inst = nullptr;
        voices[i].instSample = nullptr;
        voices[i].samplePos1616 = 0;
        voices[i].sampleInc1616 = 0;
        voices[i].sampleLoopMode = LOOP_OFF; 
        voices[i].sampleFinished = false;
        voices[i].wtData = nullptr;
        voices[i].wtSize = 0;
    }

    for (uint16_t i = 0; i < MAX_WAVETABLES; i++) {
        wavetables[i].data = nullptr;
        wavetables[i].size = 0;
        wavetables[i].depth = BITS_8;
    }
    controlSampleCounter = 0;
    controlRateHz = 100;
}

ESP32Synth::~ESP32Synth() {
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
    }
}

// Overloads para conveniência
// Overloads for convenience
bool ESP32Synth::begin(int dacPin) {
    // Chama a função principal configurando para DAC
    // Calls the main function configured for DAC
    return begin(dacPin, SMODE_DAC, -1, -1, I2S_16BIT);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin) {
    // Chama a função principal configurando para I2S 16-bit
    // Calls the main function configured for 16-bit I2S
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, I2S_16BIT);
}

bool ESP32Synth::begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth) {
    // Chama a função principal com a profundidade de bits especificada
    // Calls the main function with the specified bit depth
    return begin(dataPin, SMODE_I2S, bckPin, wsPin, i2sDepth);
}

bool ESP32Synth::begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth) {
    this->currentMode = mode;
    this->_i2sDepth = i2sDepth;

    #if defined(CONFIG_IDF_TARGET_ESP32S3)
        if (mode == SMODE_PDM) _sampleRate = 52036; else _sampleRate = 48000; 
    #else
        _sampleRate = 48000; 
        controlIntervalSamples = _sampleRate / controlRateHz;
    #endif

    controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1;

    // Preenche a tabela de seno com amplitude de 31 bits.
    // Fills the sine table with 31-bit amplitude.
    for (int i = 0; i < SINE_LUT_SIZE; i++) {
        sineLUT[i] = (int32_t)(sin(i * 2.0 * PI / (double)SINE_LUT_SIZE) * 1050000000.0);
    }

    if (mode == SMODE_DAC) {
        #if !defined(CONFIG_IDF_TARGET_ESP32)
            Serial.println("SMODE_DAC Error: ESP32-S3/C3 not supported.");
            return false;
        #else
            dac_continuous_config_t cont_cfg = {
                .chan_mask = (dataPin == 26) ? DAC_CHANNEL_MASK_CH1 : DAC_CHANNEL_MASK_CH0,
                .desc_num = 8, .buf_size = 2048, .freq_hz = _sampleRate, .offset = 0,
                .clk_src = DAC_DIGI_CLK_SRC_DEFAULT, .chan_mode = DAC_CHANNEL_MODE_SIMUL,
            };
            if (dac_continuous_new_channels(&cont_cfg, &dac_handle) != ESP_OK) return false;
            if (dac_continuous_enable(dac_handle) != ESP_OK) return false;
            xTaskCreatePinnedToCore(audioTask, "SynthTask", 4096, this, configMAX_PRIORITIES - 1, NULL, 1);
            return true;
        #endif
    }
    
    i2s_port_t requested_port = (mode == SMODE_PDM) ? I2S_NUM_0 : I2S_NUM_AUTO;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(requested_port, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8; 
    chan_cfg.dma_frame_num = 512; 

    if (i2s_new_channel(&chan_cfg, &tx_handle, NULL) != ESP_OK) return false;

    if (mode == SMODE_PDM) {
        i2s_pdm_tx_config_t pdm_cfg = {
            .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(_sampleRate), 
            .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = { .clk = (gpio_num_t)clkPin, .dout = (gpio_num_t)dataPin }
        };
        if (i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_cfg) != ESP_OK) return false;
    } else { // Modo I2S Padrão
        i2s_data_bit_width_t width = (i2sDepth == I2S_32BIT) ? I2S_DATA_BIT_WIDTH_32BIT : I2S_DATA_BIT_WIDTH_16BIT;

        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sampleRate), 
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(width, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)clkPin,
                .ws = (gpio_num_t)wsPin,
                .dout = (gpio_num_t)dataPin
            }
        };
        if (i2s_channel_init_std_mode(tx_handle, &std_cfg) != ESP_OK) return false;
    }

    if (i2s_channel_enable(tx_handle) != ESP_OK) return false;

    xTaskCreatePinnedToCore(audioTask, "SynthTask", 4096, this, configMAX_PRIORITIES - 1, NULL, 1);
    return true;
}

void ESP32Synth::audioTask(void* param) {
    ((ESP32Synth*)param)->renderLoop();
}

void ESP32Synth::renderLoop() {
    int32_t buf[512]; 
    size_t written;

    while (1) {
        render(buf, 512); 

        if (currentMode == SMODE_DAC) {
            #if defined(CONFIG_IDF_TARGET_ESP32)
                int16_t* buf16 = (int16_t*)buf;
                uint8_t dacBuf[512];
                for (int i = 0; i < 512; i++) dacBuf[i] = (uint8_t)((buf16[i] + 32768) >> 8);
                dac_continuous_write(dac_handle, dacBuf, 512, &written, portMAX_DELAY);
            #endif
        } 
        else if (currentMode == SMODE_I2S) {
            if (_i2sDepth == I2S_32BIT) {
                int32_t smallStereo32[128];
                for (int offset = 0; offset < 512; offset += 64) {
                    for (int i = 0; i < 64; i++) {
                        int32_t s = buf[offset + i];
                        smallStereo32[i*2]     = s;
                        smallStereo32[i*2 + 1] = s;
                    }
                    i2s_channel_write(tx_handle, smallStereo32, sizeof(smallStereo32), &written, portMAX_DELAY);
                }
            } else { // Profundidade de 16 ou 8 bits
                int16_t* buf16 = (int16_t*)buf;
                int16_t smallStereo16[128]; 

                for (int offset = 0; offset < 512; offset += 64) {
                    for (int i = 0; i < 64; i++) {
                        int16_t s = buf16[offset + i];
                        smallStereo16[i*2]     = s;
                        smallStereo16[i*2 + 1] = s;
                    }
                    i2s_channel_write(tx_handle, smallStereo16, sizeof(smallStereo16), &written, portMAX_DELAY);
                }
            }
        }
        else { // PDM
            i2s_channel_write(tx_handle, buf, 512 * sizeof(int16_t), &written, portMAX_DELAY);
        }
    }
}

// Atualiza o estado do envelope ADSR para um bloco de samples.
// Updates the ADSR envelope state for a block of samples.
static inline void updateAdsrBlock(Voice* vo, int samples, int32_t& startEnv, int32_t& envStep) {
    // Para instrumentos tipo Tracker, o ADSR é controlado pelo instrumento.
    // For Tracker-style instruments, ADSR is controlled by the instrument itself.
    if (vo->inst) {
        startEnv = ENV_MAX;
        vo->currEnvVal = ENV_MAX;
        envStep = 0;
        return;
    }

    startEnv = vo->currEnvVal;
    
    // Se o envelope está estável, o passo é zero.
    // If the envelope is stable, the step is zero.
    if (vo->envState == ENV_IDLE || vo->envState == ENV_SUSTAIN) {
        envStep = 0;
        return;
    }

    uint32_t target = vo->currEnvVal;
    uint32_t steps = (uint32_t)samples;

    switch (vo->envState) {
        case ENV_ATTACK:
            if (vo->rateAttack >= ENV_MAX) {
                target = ENV_MAX;
                vo->envState = ENV_DECAY;
                startEnv = ENV_MAX; 
                envStep = 0;
            }
            else if (ENV_MAX - target > vo->rateAttack * steps) {
                target += vo->rateAttack * steps;
                envStep = (target - startEnv) / samples;
            } else { 
                target = ENV_MAX; vo->envState = ENV_DECAY; 
                envStep = (target - startEnv) / samples;
            }
            break;
            
        case ENV_DECAY:
            if (vo->rateDecay >= ENV_MAX) {
                target = vo->levelSustain;
                vo->envState = ENV_SUSTAIN;
                startEnv = target;
                envStep = 0;
            }
            else if (target > vo->levelSustain && (target - vo->levelSustain) > vo->rateDecay * steps) {
                target -= vo->rateDecay * steps;
                envStep = -((int32_t)vo->rateDecay);
            } 
            else { 
                target = vo->levelSustain; 
                vo->envState = ENV_SUSTAIN; 
                envStep = ((int32_t)target - (int32_t)startEnv) / samples;
            }
            break;
            
        case ENV_RELEASE:
            if (vo->rateRelease >= ENV_MAX) {
                target = 0; vo->active = false; vo->envState = ENV_IDLE;
                startEnv = 0;
                envStep = 0;
            }
            else if (target > vo->rateRelease * steps) {
                target -= vo->rateRelease * steps;
                envStep = -((int32_t)vo->rateRelease);
            } else { 
                target = 0; vo->active = false; vo->envState = ENV_IDLE;
                envStep = -startEnv / samples; 
                if (envStep == 0 && startEnv > 0) envStep = -1;
            }
            break;
            
        default: 
            envStep = 0; 
            break;
    }

    vo->currEnvVal = target;
}

void IRAM_ATTR ESP32Synth::render(void* buffer, int samples) {
    static int32_t mix[512]; 
    memset(mix, 0, samples * sizeof(int32_t));

    controlSampleCounter += (uint32_t)samples;
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
            if (vo->type == WAVE_SAMPLE) renderBlockSample(vo, mix, samples, startEnv, envStep);
            else if (vo->type == WAVE_WAVETABLE) renderBlockWavetable(vo, mix, samples, startEnv, envStep);
            else if (vo->type == WAVE_NOISE) renderBlockNoise(vo, mix, samples, startEnv, envStep);
            else renderBlockBasic(vo, mix, samples, startEnv, envStep);
        }
        else {
            if (vo->currWaveIsBasic) {
                 WaveType oldType = vo->type;
                 vo->type = (WaveType)vo->currWaveType; 
                 if (vo->type == WAVE_NOISE) renderBlockNoise(vo, mix, samples, startEnv, envStep);
                 else renderBlockBasic(vo, mix, samples, startEnv, envStep); 
                 vo->type = oldType;
            } else {
                 renderBlockWavetable(vo, mix, samples, startEnv, envStep);
            }
        }
    }

    // Processamento final e escrita para o buffer de saída
    // Final processing and writing to the output buffer
    if (currentMode == SMODE_I2S && _i2sDepth == I2S_32BIT) {
        int32_t* buf32 = (int32_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int32_t val = mix[i];
            val = (val * _masterVolume) >> 8; 
            if (val > 32767) val = 32767; else if (val < -32768) val = -32768;
            buf32[i] = val << 16; 
        }
    }
    else if (currentMode == SMODE_I2S && _i2sDepth == I2S_8BIT) {
        int16_t* buf16 = (int16_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int32_t val = mix[i];
            val = (val * _masterVolume) >> 9; 
            if (val > 32767) val = 32767; else if (val < -32768) val = -32768;
            buf16[i] = (int16_t)(val & 0xFF00); 
        }
    }
    else { // Padrão 16-bit para I2S, PDM ou DAC
        int16_t* buf16 = (int16_t*)buffer;
        for (int i = 0; i < samples; i++) {
            int32_t val = mix[i];
            val = (val * _masterVolume) >> 9; 
            if (val > 32767) val = 32767; else if (val < -32768) val = -32768;
            buf16[i] = (int16_t)val;
        }
    }
}

void ESP32Synth::setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz) {
    if (voice >= MAX_VOICES) return;
    voices[voice].vibRateInc = rateCentiHz * 895; 
    voices[voice].vibDepthInc = depthCentiHz * 733; 
}

void ESP32Synth::setTremolo(uint8_t voice, uint32_t rateCentiHz, uint16_t depth) {
    if (voice >= MAX_VOICES) return;
    voices[voice].trmRateInc = rateCentiHz * 895; 
    voices[voice].trmDepth = depth; 
}

void ESP32Synth::setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth) {
    if (voice >= MAX_VOICES) return;
    voices[voice].wtData = data; voices[voice].wtSize = size; voices[voice].depth = depth;
}

void ESP32Synth::setWavetable(const void* data, uint32_t size, BitDepth depth) {
    for (int i = 0; i < MAX_VOICES; i++) { 
        voices[i].wtData = data; voices[i].wtSize = size; voices[i].depth = depth; 
    }
}

void ESP32Synth::setSample(uint8_t voice, uint16_t sampleId, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd) {
    if (voice >= MAX_VOICES || sampleId >= MAX_SAMPLES) return;
    Voice* v = &voices[voice];
    
    v->type = WAVE_SAMPLE;
    v->curSampleId = sampleId;
    v->sampleLoopMode = loopMode;
    v->sampleLoopStart = loopStart;
    v->sampleLoopEnd = loopEnd;
    
    // Desacopla qualquer instrumento de sample anterior para forçar o controle bruto
    v->instSample = nullptr; 
}

void ESP32Synth::setSampleLoop(uint8_t voice, LoopMode loopMode, uint32_t loopStart, uint32_t loopEnd) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    
    // Agora o tipo está seguro e blindado
    v->sampleLoopMode = loopMode;
    v->sampleLoopStart = loopStart;
    v->sampleLoopEnd = loopEnd;
}

void ESP32Synth::setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->levelSustain = (uint32_t)s * (ENV_MAX / 255);
    uint32_t samplesPerMs = _sampleRate / 1000;
    v->rateAttack  = (a == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)a * samplesPerMs);
    v->rateDecay   = (d == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)d * samplesPerMs);
    v->rateRelease = (r == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)r * samplesPerMs);
    v->attackMs = a; v->decayMs = d; v->releaseMs = r;
}

void ESP32Synth::noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume) {
    if (voice >= MAX_VOICES) return;
    Voice* vo = &voices[voice];
    
    vo->freqVal = freqCentiHz;
    vo->vol = volume;
    vo->active = true;

    uint64_t num = (uint64_t)freqCentiHz << 32;
    uint32_t den = _sampleRate * 100;
    vo->phaseInc = (uint32_t)(num / den);

    controlSampleCounter = controlIntervalSamples; 

    if (vo->inst) {
        vo->envState = ENV_ATTACK;
        vo->currEnvVal = ENV_MAX;
        
        vo->stageIdx = 0;
        vo->controlTick = 0;
        vo->currWaveId = 0;
        vo->currWaveIsBasic = 0;
        vo->nextWaveId = 0;
        vo->morph = 0;
        processControl();
    } 

    else if (vo->instSample) {
        vo->type = WAVE_SAMPLE;
        vo->envState = ENV_ATTACK;
        vo->currEnvVal = ENV_MAX;
        vo->sampleFinished = false;
        vo->sampleLoopMode = vo->instSample->loopMode;
        vo->sampleLoopStart = vo->instSample->loopStart;
        
        const SampleData* sData = nullptr;
        uint32_t root = 0;
        for(int i=0; i < vo->instSample->numZones; i++) {
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
            if (vo->instSample->loopEnd == 0 || vo->instSample->loopEnd > sData->length) vo->sampleLoopEnd = sData->length;
            else vo->sampleLoopEnd = vo->instSample->loopEnd;
        }
        
        if (vo->sampleLoopMode == LOOP_REVERSE) {
            vo->sampleDirection = false; 
            if(sData) vo->samplePos1616 = (uint64_t)sData->length << 16; 
        } else {
            vo->sampleDirection = true; 
            vo->samplePos1616 = 0;      
        }
        
        if (sData && sData->data && root > 0) {
            float pitchRatio = (float)freqCentiHz / (float)root;
            float rateRatio = (float)sData->sampleRate / (float)_sampleRate;
            vo->sampleInc1616 = (uint32_t)(pitchRatio * rateRatio * 65536.0f);
        } else { vo->sampleInc1616 = 0; }
    }
    
    else {
        if (vo->type == WAVE_NOISE) {
            vo->rngState += micros(); 
        } 
        else if (vo->type == WAVE_SAMPLE) {
            
            vo->sampleFinished = false;
            const SampleData* sData = &registeredSamples[vo->curSampleId];
            
            if (vo->sampleLoopMode == LOOP_REVERSE) {
                vo->sampleDirection = false;
                vo->samplePos1616 = (uint64_t)sData->length << 16;
            } else {
                vo->sampleDirection = true;
                vo->samplePos1616 = 0;
            }
            if (sData->data && sData->rootFreqCentiHz > 0) {
                uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / sData->rootFreqCentiHz;
                vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
            } else { 
                vo->sampleInc1616 = 0; 
            }
        } 
        else {
            vo->phase = 0; 
        }

        if (vo->arpActive) { vo->arpIdx = 0; vo->arpTickCounter = 0; }
        
        if (vo->rateAttack >= ENV_MAX) {
            vo->currEnvVal = ENV_MAX; 
            vo->envState = ENV_DECAY;
        } else {
            vo->currEnvVal = 0; 
            vo->envState = ENV_ATTACK;
        }
    }
}

void ESP32Synth::noteOff(uint8_t voice) {
    if (voice < MAX_VOICES && voices[voice].active) {
        voices[voice].envState = ENV_RELEASE;
        voices[voice].stageIdx = 0;
        voices[voice].controlTick = 0;
    }
}

void ESP32Synth::setWave(uint8_t voice, WaveType type) {
    if (voice < MAX_VOICES) voices[voice].type = type;
}

void ESP32Synth::setFrequency(uint8_t voice, uint32_t freqCentiHz) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->freqVal = freqCentiHz;
    if (v->type == WAVE_SAMPLE && v->instSample == nullptr) {
        const SampleData* sData = &registeredSamples[v->curSampleId];
        if (sData->data && sData->rootFreqCentiHz > 0) {
          
            uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / sData->rootFreqCentiHz;
            v->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
        }
    } else {
        v->phaseInc = (uint32_t)(((uint64_t)freqCentiHz * 4294967296ULL) / (_sampleRate * 100ULL));
    }
}

void ESP32Synth::setVolume(uint8_t voice, uint8_t volume) {
    if (voice >= MAX_VOICES) return;
    voices[voice].vol = volume;
}

void ESP32Synth::setPulseWidth(uint8_t voice, uint8_t width) {
    if (voice >= MAX_VOICES) return;
    voices[voice].pulseWidth = (uint32_t)width << 24; 
}

void ESP32Synth::setInstrument(uint8_t voice, Instrument* inst) {
    if (voice >= MAX_VOICES) return;
    voices[voice].inst = inst;
    voices[voice].instSample = nullptr;
    voices[voice].stageIdx = 0;
    voices[voice].controlTick = 0;
    if (inst == nullptr) {
        voices[voice].currEnvVal = 0;
        voices[voice].envState = ENV_ATTACK;
    } else {
        voices[voice].currEnvVal = ENV_MAX; 
    }
}

void ESP32Synth::detachWave(uint8_t voice, WaveType NewWaveType) {
    if (voice >= MAX_VOICES) return;
    setInstrument(voice, (Instrument*)nullptr); 
    setWave(voice, NewWaveType);
}

void ESP32Synth::detachInstrument(uint8_t voice, WaveType NewWaveType) {
    detachWave(voice, NewWaveType);
} 

void ESP32Synth::registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth) {
    if (id >= MAX_WAVETABLES) return;
    wavetables[id].data = data;
    wavetables[id].size = size;
    wavetables[id].depth = depth;
}

void ESP32Synth::setControlRateHz(uint16_t hz) {
    if (hz > 0) {
        controlRateHz = hz;
        controlIntervalSamples = (_sampleRate / controlRateHz) ? (_sampleRate / controlRateHz) : 1; 
    }
}

void ESP32Synth::slide(uint8_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    uint64_t numStart = (uint64_t)startFreqCentiHz << 32;
    uint64_t numEnd = (uint64_t)endFreqCentiHz << 32;
    uint32_t den = _sampleRate * 100;
    uint32_t startInc = (uint32_t)(numStart / den);
    uint32_t endInc = (uint32_t)(numEnd / den);
    uint32_t ticks = (durationMs == 0) ? 0 : ((durationMs * controlRateHz + 999) / 1000);
    if (ticks == 0) {
        v->phaseInc = endInc; v->freqVal = endFreqCentiHz; v->slideActive = false; return;
    }
    int64_t diff = (int64_t)endInc - (int64_t)startInc;
    int32_t delta = (int32_t)(diff / (int64_t)ticks); 
    int64_t rem = diff - (int64_t)delta * (int64_t)ticks; 
    v->phaseInc = startInc; v->slideDeltaInc = delta; v->slideRem = (int32_t)rem;
    v->slideRemAcc = 0; v->slideTicksTotal = ticks; v->slideTicksRemaining = ticks;
    v->slideTargetInc = endInc; v->slideTargetFreqCenti = endFreqCentiHz; v->slideActive = true; v->freqVal = startFreqCentiHz; 
}

void ESP32Synth::slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    uint32_t start = voices[voice].freqVal;
    if (start == 0) {
        uint64_t curNum = (uint64_t)voices[voice].phaseInc * (uint64_t)_sampleRate * 100;
        if (voices[voice].phaseInc != 0) start = (uint32_t)(curNum >> 32); else start = endFreqCentiHz;
    }
    slide(voice, start, endFreqCentiHz, durationMs);
}

bool ESP32Synth::registerSample(uint16_t sampleId, const int16_t* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz) {
    if (sampleId >= MAX_SAMPLES || data == nullptr) return false;
    
    registeredSamples[sampleId].data = data;
    registeredSamples[sampleId].length = length;
    registeredSamples[sampleId].sampleRate = sampleRate;
    registeredSamples[sampleId].rootFreqCentiHz = rootFreqCentiHz;
    
    return true;
}

void ESP32Synth::setInstrument(uint8_t voice, Instrument_Sample* inst) {
    if (voice >= MAX_VOICES) return;
    voices[voice].instSample = inst; voices[voice].inst = nullptr; 
    voices[voice].currEnvVal = 0; voices[voice].envState = ENV_IDLE;
}

IRAM_ATTR int16_t ESP32Synth::fetchWavetableSample(uint16_t id, uint32_t phase) {
    if (id >= MAX_WAVETABLES) return 0;
    const auto &e = wavetables[id];
    if (!e.data || e.size == 0) return 0;
    uint32_t idx = (uint32_t)(((uint64_t)phase * e.size) >> 32);

    if (e.depth == BITS_8) {
        return (((uint8_t*)e.data)[idx] - 128) << 8;
    } else if (e.depth == BITS_4) {
        const uint8_t* p = (const uint8_t*)e.data;
        uint8_t val = p[idx / 2];
        if ((idx & 1) == 0) { // Check for even index
            val &= 0x0F; // Low nibble
        } else {
            val >>= 4;   // High nibble
        }
        return ((int16_t)val - 8) * 4096;
    }
    
    // Default to 16-bit
    return ((int16_t*)e.data)[idx];
}

uint32_t ESP32Synth::getFrequencyCentiHz(uint8_t voice) {
     if (voice >= MAX_VOICES) return 0; 
     return voices[voice].freqVal; 
}

uint8_t ESP32Synth::getVolume8Bit(uint8_t voice) { 
    if (voice >= MAX_VOICES) return 0; 
    return voices[voice].vol; 
}

uint8_t ESP32Synth::getEnv8Bit(uint8_t voice) { 
    if (voice >= MAX_VOICES) return 0; 
    return (uint8_t)(voices[voice].currEnvVal >> 20); 
}

uint8_t ESP32Synth::getOutput8Bit(uint8_t voice) { 
    if (voice >= MAX_VOICES) return 0; 
    return (uint8_t)(((voices[voice].currEnvVal >> 20) * voices[voice].vol) >> 8); 
}

uint32_t ESP32Synth::getVolumeRaw(uint8_t voice) { 
    if (voice >= MAX_VOICES) return 0; 
    return (uint32_t)voices[voice].vol;
}

uint32_t ESP32Synth::getEnvRaw(uint8_t voice) { 
    if (voice >= MAX_VOICES) return 0; 
    return voices[voice].currEnvVal; 
}

uint32_t ESP32Synth::getOutputRaw(uint8_t voice) { 
    if (voice >= MAX_VOICES) return 0; 
    return (voices[voice].currEnvVal >> 8) * voices[voice].vol; 
}

void ESP32Synth::detachArpeggio(uint8_t voice) {
    if (voice >= MAX_VOICES) return;
    voices[voice].arpActive = false;
}
void ESP32Synth::setMasterVolume(uint8_t volume) {
    _masterVolume = volume;
}

uint8_t ESP32Synth::getMasterVolume() {
    return _masterVolume;
}

void IRAM_ATTR ESP32Synth::processControl() {
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];

        // LFOs (Vibrato e Tremolo)
        if (vo->vibDepthInc > 0) {
            vo->vibPhase += (vo->vibRateInc * controlIntervalSamples);
            int16_t lfoVal = (int16_t)(sineLUT[vo->vibPhase >> SINE_SHIFT] >> 16);
            vo->vibOffset = ((int64_t)vo->vibDepthInc * lfoVal) >> 14;
        } else { vo->vibOffset = 0; }

        if (vo->trmDepth > 0) {
            vo->trmPhase += (vo->trmRateInc * controlIntervalSamples);
            int16_t lfo = (int16_t)(sineLUT[vo->trmPhase >> SINE_SHIFT] >> 16);
            int32_t magnitude = (int32_t)lfo + 20000;
            int32_t reduction = (magnitude * vo->trmDepth) >> 15;
            if (reduction > 255) reduction = 255;
            vo->trmModGain = 255 - reduction;
        } else { vo->trmModGain = 255; }

        // Arpejador
        if (vo->arpActive && vo->arpLen > 0) {
            if (vo->arpTickCounter == 0) {
                uint32_t targetFreq = vo->arpNotes[vo->arpIdx];
                vo->freqVal = targetFreq;
                uint64_t num = (uint64_t)targetFreq << 32;
                uint32_t den = _sampleRate * 100;
                vo->phaseInc = (uint32_t)(num / den);
                vo->arpIdx = (vo->arpIdx + 1) % vo->arpLen;
                vo->arpTickCounter = ((uint32_t)vo->arpSpeedMs * (uint32_t)controlRateHz + 999) / 1000;
                if (vo->arpTickCounter == 0) vo->arpTickCounter = 1; 
            }
            vo->arpTickCounter--;
        }

        // Portamento (slide)
        if (vo->slideActive && vo->slideTicksRemaining > 0) {
            vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + vo->slideDeltaInc);
            if (vo->slideRem != 0 && vo->slideTicksTotal > 0) {
                vo->slideRemAcc += vo->slideRem;
                int32_t sign = (vo->slideRem > 0) ? 1 : -1;
                if (abs(vo->slideRemAcc) >= (int32_t)vo->slideTicksTotal) {
                    vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + sign);
                    vo->slideRemAcc -= sign * (int32_t)vo->slideTicksTotal;
                }
            }
            vo->slideTicksRemaining--;
            vo->freqVal = (uint32_t)(((uint64_t)vo->phaseInc * (uint64_t)_sampleRate * 100) >> 32);
            if (vo->slideTicksRemaining == 0) {
                vo->phaseInc = vo->slideTargetInc;
                vo->freqVal = vo->slideTargetFreqCenti;
                vo->slideActive = false;
            }
        }

        // Lógica de Instrumento (Tracker)
        if (!vo->active || !vo->inst) continue;
        Instrument* inst = vo->inst;

        int16_t wVal = 0;
        uint8_t nextVol = 0;
        bool updateNeeded = false;

        switch (vo->envState) {
            case ENV_ATTACK: { 
                uint8_t len = inst->seqLen;
                if (len == 0) { vo->envState = ENV_SUSTAIN; break; }
                
                if (vo->controlTick == 0) {
                     uint32_t ms = inst->seqSpeedMs;
                     vo->controlTick = (ms == 0) ? 1 : ((uint32_t)ms * (uint32_t)controlRateHz + 999) / 1000;
                }

                uint8_t idx = vo->stageIdx;
                if (idx >= len) idx = len - 1;
                
                wVal = inst->seqWaves[idx];
                nextVol = inst->seqVolumes[idx];
                updateNeeded = true;

                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) vo->envState = ENV_SUSTAIN;
                }
                break;
            }
            case ENV_DECAY: { vo->envState = ENV_SUSTAIN; break; }
            case ENV_SUSTAIN: {
                wVal = inst->susWave;
                nextVol = inst->susVol;
                updateNeeded = true;
                break;
            }
            case ENV_RELEASE: {
                uint8_t len = inst->relLen;
                if (len == 0) { vo->active = false; break; }
                
                if (vo->controlTick == 0) {
                    uint32_t ms = inst->relSpeedMs;
                    vo->controlTick = (ms == 0) ? 1 : ((uint32_t)ms * (uint32_t)controlRateHz + 999) / 1000;
                }

                uint8_t idx = vo->stageIdx;
                if (idx >= len) idx = len - 1;

                wVal = inst->relWaves[idx];
                nextVol = inst->relVolumes[idx];
                updateNeeded = true;

                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) vo->active = false; 
                }
                break;
            }
            default: break;
        }

        if (updateNeeded) {
            vo->vol = nextVol;

            if (wVal < 0) { // Onda Básica
                vo->currWaveIsBasic = 1; 
                vo->currWaveId = 0;
                switch(wVal) {
                    case W_SINE:  vo->currWaveType = WAVE_SINE; break;
                    case W_TRI:   vo->currWaveType = WAVE_TRIANGLE; break;
                    case W_SAW:   vo->currWaveType = WAVE_SAW; break;
                    case W_PULSE: vo->currWaveType = WAVE_PULSE; break;
                    case W_NOISE: vo->currWaveType = WAVE_NOISE; break;
                    default:      vo->currWaveType = WAVE_SINE; break;
                }
            } else { // Wavetable
                vo->currWaveIsBasic = 0; 
                vo->currWaveType = 0;
                vo->currWaveId = (uint16_t)wVal; 
                
                if (vo->currWaveId < MAX_WAVETABLES) {
                    vo->wtData = wavetables[vo->currWaveId].data;
                    vo->wtSize = wavetables[vo->currWaveId].size;
                    vo->depth  = wavetables[vo->currWaveId].depth;
                    if (vo->depth == 0) vo->depth = BITS_8;
                }
            }
        }
    }
}