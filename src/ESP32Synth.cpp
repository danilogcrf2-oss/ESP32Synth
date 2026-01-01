/**
 * @file ESP32Synth.cpp
 * @brief Implementação corrigida para ESP32 Core 3.x
 */

#include "ESP32Synth.h"

const int16_t LUT_CUTOFF[256] = {
    15, 20, 35, 50, 70, 90, 115, 140, 165, 190, 220, 250, 280, 315, 350, 390,
    430, 470, 515, 560, 610, 660, 715, 770, 830, 890, 955, 1020, 1090, 1160, 1235, 1310,
    1390, 1470, 1555, 1640, 1730, 1820, 1915, 2010, 2110, 2210, 2315, 2420, 2530, 2640, 2755, 2870,
    2990, 3110, 3235, 3360, 3490, 3620, 3755, 3890, 4030, 4170, 4315, 4460, 4610, 4760, 4915, 5070,
    5230, 5390, 5555, 5720, 5890, 6060, 6235, 6410, 6590, 6770, 6955, 7140, 7330, 7520, 7715, 7910,
    8110, 8310, 8515, 8720, 8930, 9140, 9355, 9570, 9790, 10010, 10235, 10460, 10690, 10920, 11155, 11390,
    11630, 11870, 12115, 12360, 12610, 12860, 13115, 13370, 13630, 13890, 14000, 14000, 14000, 14000, 14000, 14000,
    14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000,
    14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000,
    14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000,
    14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000,
    14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000, 14000
};

ESP32Synth::ESP32Synth() {
    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active = false;
        voices[i].type = WAVE_SINE;
        voices[i].envState = ENV_IDLE;
        voices[i].rngState = 12345 + (i * 999);
        voices[i].rateAttack = ENV_MAX;
        voices[i].rateDecay = 0;
        voices[i].levelSustain = ENV_MAX;
        voices[i].rateRelease = ENV_MAX;
        voices[i].filterType = FILTER_NONE;
        voices[i].vibPhase = 0;
        voices[i].vibRateInc = 0;
        voices[i].vibDepthInc = 0;
        voices[i].pulseWidth = 0x80000000;

        // Instrument defaults
        voices[i].inst = nullptr;
        voices[i].stageIdx = 0;
        voices[i].controlTick = 0;
        voices[i].currWaveId = 0;
        voices[i].nextWaveId = 0;
        voices[i].currWaveIsBasic = 0;
        voices[i].nextWaveIsBasic = 0;
        voices[i].currWaveType = 0;
        voices[i].nextWaveType = 0;
        voices[i].morph = 0;
        voices[i].attackMs = 0;
        voices[i].decayMs = 0;
        voices[i].releaseMs = 0;

        // Slide defaults
        voices[i].slideActive = false;
        voices[i].slideTicksRemaining = 0;
        voices[i].slideTicksTotal = 0;
        voices[i].slideDeltaInc = 0;
        voices[i].slideTargetInc = 0;
        voices[i].slideTargetFreqCenti = 0;
        voices[i].slideRem = 0;
        voices[i].slideRemAcc = 0;
    }

    // Initialize wavetable registry entries
    for (uint16_t i = 0; i < MAX_WAVETABLES; i++) {
        wavetables[i].data = nullptr;
        wavetables[i].size = 0;
        wavetables[i].depth = BITS_8;
    }

    // Default control-rate scheduling
    controlSampleCounter = 0;
    controlRateHz = 100;
    controlIntervalSamples = (SYNTH_RATE / controlRateHz) ? (SYNTH_RATE / controlRateHz) : 1;
}

ESP32Synth::~ESP32Synth() {
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
    }
}

bool ESP32Synth::begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin) {
    for(int i=0; i<256; i++) {
        sineLUT[i] = (int16_t)(sin((i / 256.0) * 6.283185) * 20000.0);
    }

    // For PDM, ESP-IDF requires using I2S0. For other modes, allow auto allocation.
    i2s_port_t requested_port = (mode == SMODE_PDM) ? I2S_NUM_0 : I2S_NUM_AUTO;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(requested_port, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &tx_handle, NULL) != ESP_OK) return false;

    if (mode == SMODE_PDM) {
        i2s_pdm_tx_config_t pdm_cfg = {
            .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(I2S_RATE),
            .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = { 
                .clk = (gpio_num_t)clkPin, 
                .dout = (gpio_num_t)dataPin 
            }
        };
        if (i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_cfg) != ESP_OK) return false;
    } 
    else {
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_RATE),
            .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = (gpio_num_t)clkPin,
                .ws = (gpio_num_t)wsPin,
                .dout = (gpio_num_t)dataPin
            }
        };
        // CORREÇÃO AQUI: mudado de init_std_tx_mode para init_std_mode
        if (i2s_channel_init_std_mode(tx_handle, &std_cfg) != ESP_OK) return false;
    }

    if (i2s_channel_enable(tx_handle) != ESP_OK) return false;

    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, this, configMAX_PRIORITIES - 1, NULL, 1);
    return true;
}

void ESP32Synth::audioTask(void* param) {
    ((ESP32Synth*)param)->renderLoop();
}

void ESP32Synth::renderLoop() {
    int16_t buf[256];
    size_t written;
    while (1) {
        render(buf, 256);
        i2s_channel_write(tx_handle, buf, sizeof(buf), &written, portMAX_DELAY);
    }
}

// Main rendering function
void IRAM_ATTR ESP32Synth::render(int16_t* buffer, int samples) {
    memset(buffer, 0, samples * sizeof(int16_t));
    int32_t mix[256] = {0}; 

    // Advance control-rate as many ticks as necessary for this block
    controlSampleCounter += (uint32_t)samples;
    while (controlSampleCounter >= controlIntervalSamples) {
        processControl();
        controlSampleCounter -= controlIntervalSamples;
    }

    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) continue;
        Voice* vo = &voices[v];
        
        for (int i = 0; i < samples; i++) {
            // Legacy ADSR progression (still used when no instrument assigned)
            if (!vo->inst) {
                switch (vo->envState) {
                    case ENV_ATTACK:
                        if (ENV_MAX - vo->currEnvVal > vo->rateAttack) vo->currEnvVal += vo->rateAttack;
                        else { vo->currEnvVal = ENV_MAX; vo->envState = ENV_DECAY; }
                        break;
                    case ENV_DECAY:
                        if (vo->currEnvVal > vo->levelSustain && (vo->currEnvVal - vo->levelSustain) > vo->rateDecay) 
                            vo->currEnvVal -= vo->rateDecay;
                        else { vo->currEnvVal = vo->levelSustain; vo->envState = ENV_SUSTAIN; }
                        break;
                    case ENV_SUSTAIN: vo->currEnvVal = vo->levelSustain; break;
                    case ENV_RELEASE:
                        if (vo->currEnvVal > vo->rateRelease) vo->currEnvVal -= vo->rateRelease;
                        else { vo->currEnvVal = 0; vo->envState = ENV_IDLE; vo->active = false; }
                        break;
                    case ENV_IDLE: vo->currEnvVal = 0; break;
                }
            } else {
                // Instrument mode: env handled in processControl()
                vo->currEnvVal = ENV_MAX;
            }

            uint32_t currentInc = vo->phaseInc; 
            if (vo->vibDepthInc > 0) {
                vo->vibPhase += vo->vibRateInc;
                int16_t lfoVal = sineLUT[vo->vibPhase >> 24]; 
                int32_t pitchShift = ((int64_t)vo->vibDepthInc * lfoVal) >> 14;
                currentInc = (uint32_t)((int32_t)currentInc + pitchShift);
            }

            int16_t s = 0;
            if (vo->type == WAVE_NOISE) {
                uint32_t ph = vo->phase;
                uint32_t nextPh = ph + currentInc;
                if (nextPh < ph) { 
                    vo->rngState = (vo->rngState * 1664525) + 1013904223;
                    vo->noiseSample = (int16_t)(vo->rngState >> 16);
                }
                s = vo->noiseSample;
                vo->phase = nextPh;
            } 
            else if (vo->inst) {
                // Instrument path: support wavetable <-> wavetable, basic <-> basic and mixed morphing
                int16_t sampleA = 0, sampleB = 0;
                uint32_t ph = vo->phase;
                uint32_t nextPh = ph + currentInc;
                bool wrapped = (nextPh < ph);
                if (wrapped) {
                    
                    vo->rngState = (vo->rngState * 1664525) + 1013904223;
                    vo->noiseSample = (int16_t)(vo->rngState >> 16);
                }

                // sample A
                if (vo->currWaveIsBasic) {
                    switch (vo->currWaveType) {
                        case 1: sampleA = sineLUT[ph >> 24]; break; // sine
                        case 2: { int16_t saw = (int16_t)(ph >> 16); sampleA = (int16_t)(((saw ^ (saw >> 15)) * 2) - 32767); break; } // triangle
                        case 3: sampleA = (int16_t)(ph >> 16); break; // saw
                        case 4: sampleA = (ph < vo->pulseWidth) ? 20000 : -20000; break; // pulse
                        case 5: sampleA = vo->noiseSample; break; // noise (rng updated above if wrap)
                        default: sampleA = 0; break;
                    }
                } else {
                    sampleA = fetchWavetableSample(vo->currWaveId, ph);
                }

                // sample B
                if (vo->nextWaveIsBasic) {
                    switch (vo->nextWaveType) {
                        case 1: sampleB = sineLUT[ph >> 24]; break;
                        case 2: { int16_t saw = (int16_t)(ph >> 16); sampleB = (int16_t)(((saw ^ (saw >> 15)) * 2) - 32767); break; }
                        case 3: sampleB = (int16_t)(ph >> 16); break;
                        case 4: sampleB = (ph < vo->pulseWidth) ? 20000 : -20000; break;
                        case 5: sampleB = vo->noiseSample; break;
                        default: sampleB = 0; break;
                    }
                } else {
                    sampleB = fetchWavetableSample(vo->nextWaveId, ph);
                }

                uint8_t morph = vo->morph;
                int32_t combined = ((int32_t)sampleA * (256 - morph) + (int32_t)sampleB * morph) >> 8;
                s = (int16_t)combined;
                vo->phase += currentInc;
            }
            else if (vo->type == WAVE_WAVETABLE && vo->wtData) {
                uint32_t idx = (uint32_t)(((uint64_t)vo->phase * vo->wtSize) >> 32); 
                if (vo->depth == BITS_8) s = (((uint8_t*)vo->wtData)[idx] - 128) << 8;
                else s = ((int16_t*)vo->wtData)[idx];
                vo->phase += currentInc;
            } 
            else {
                uint32_t ph = vo->phase;
                if (vo->type == WAVE_SAW) s = (int16_t)(ph >> 16); 
                else if (vo->type == WAVE_PULSE) {
                    s = (ph < vo->pulseWidth) ? 20000 : -20000;
                }
                else if (vo->type == WAVE_TRIANGLE) {
                    int16_t saw = (int16_t)(ph >> 16);
                    s = (int16_t) (((saw ^ (saw >> 15)) * 2) - 32767);
                } else {
                    s = sineLUT[ph >> 24];
                }
                vo->phase += currentInc;
            }
            
            if (vo->filterType != FILTER_NONE) {
                int32_t low = vo->filterLow;
                int32_t band = vo->filterBand;
                int32_t f = vo->coefF;
                int32_t q = vo->coefQ;

                low += (f * band) >> 14; 
                int32_t high = (int32_t)s - low - ((band * q) >> 14);
                band += (f * high) >> 14;
                
                vo->filterLow = low;
                vo->filterBand = band;
                
                switch(vo->filterType) {
                    case FILTER_LP: s = (int16_t)constrain(low, -32000, 32000); break;
                    case FILTER_HP: s = (int16_t)constrain(high, -32000, 32000); break;
                    case FILTER_BP: s = (int16_t)constrain(band, -32000, 32000); break;
                }
            }

            int32_t valWithVel = (s * vo->vol) >> 8; 
            mix[i] += (int32_t)(((int64_t)valWithVel * vo->currEnvVal) >> 28);
        }
    }

    for (int i = 0; i < samples; i++) {
        int32_t val = mix[i];
        if (val > 32700) val = 32700;
        else if (val < -32700) val = -32700;
        buffer[i] = (int16_t)val;
    }
}

void ESP32Synth::setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t resonance) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->filterType = type;
    
    uint8_t safeCutoff = map(cutoff, 0, 255, 0, 200);
    v->coefF = LUT_CUTOFF[safeCutoff];

    uint8_t safeRes = map(resonance, 0, 255, 15, 255);
    int32_t qRaw = 32768 - ((int32_t)safeRes * 120); 
    v->coefQ = (int16_t)qRaw;
}

void ESP32Synth::setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz) {
    if (voice >= MAX_VOICES) return;
    voices[voice].vibRateInc = rateCentiHz * 895; 
    voices[voice].vibDepthInc = depthCentiHz * 733; 
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

void ESP32Synth::setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->levelSustain = (uint32_t)s * (ENV_MAX / 255);


    uint32_t samplesPerMs = SYNTH_RATE / 1000;
    v->rateAttack  = (a == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)a * samplesPerMs);
    v->rateDecay   = (d == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)d * samplesPerMs);
    v->rateRelease = (r == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)r * samplesPerMs);

    
    v->attackMs = a;
    v->decayMs = d;
    v->releaseMs = r;
}

void ESP32Synth::noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume) {
    if (voice >= MAX_VOICES) return;
    voices[voice].freqVal = freqCentiHz;
    voices[voice].vol = volume;
    voices[voice].active = true;
    voices[voice].currEnvVal = 0;
    voices[voice].envState = ENV_ATTACK;
    if (voices[voice].type == WAVE_NOISE) voices[voice].rngState += micros(); 
    voices[voice].filterLow = 0;
    voices[voice].filterBand = 0;

    uint64_t num = (uint64_t)freqCentiHz << 32;
    uint32_t den = SYNTH_RATE * 100;
    voices[voice].phaseInc = (uint32_t)(num / den);

    
    if (voices[voice].inst) {
        voices[voice].stageIdx = 0;
        voices[voice].controlTick = 0;
        voices[voice].currWaveId = 0;
        voices[voice].nextWaveId = 0;
        voices[voice].morph = 0;
        voices[voice].currEnvVal = ENV_MAX;
        processControl(); 
    }
}

void ESP32Synth::noteOff(uint8_t voice) {
    if (voice < MAX_VOICES && voices[voice].active) voices[voice].envState = ENV_RELEASE; 
}

void ESP32Synth::setWave(uint8_t voice, WaveType type) {
    if (voice < MAX_VOICES) voices[voice].type = type;
}

void ESP32Synth::setFrequency(uint8_t voice, uint32_t freqCentiHz) {
    if (voice >= MAX_VOICES) return;
    voices[voice].freqVal = freqCentiHz;
    uint64_t num = (uint64_t)freqCentiHz << 32;
    uint32_t den = SYNTH_RATE * 100;
    voices[voice].phaseInc = (uint32_t)(num / den);
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
    // reset instrument state when assigned/cleared
    voices[voice].stageIdx = 0;
    voices[voice].controlTick = 0;
    voices[voice].currWaveId = 0;
    voices[voice].nextWaveId = 0;
    voices[voice].currWaveIsBasic = 0;
    voices[voice].nextWaveIsBasic = 0;
    voices[voice].currWaveType = 0;
    voices[voice].nextWaveType = 0;
    voices[voice].morph = 0;
    if (inst == nullptr) {
        // switch back to legacy ADSR behavior and trigger attack so user can set wave with envelope
        voices[voice].currEnvVal = 0;
        voices[voice].envState = ENV_ATTACK;
    } else {
        voices[voice].currEnvVal = ENV_MAX; // instrument uses direct volume arrays
    }
}

void ESP32Synth::detachWave(uint8_t voice, WaveType type) {
    if (voice >= MAX_VOICES) return;
    setInstrument(voice, nullptr);
    setWave(voice, type);
}

// Deprecated wrapper for compatibility
void ESP32Synth::detachInstrumentAndSetWave(uint8_t voice, WaveType type) {
    detachWave(voice, type);
} 

void ESP32Synth::registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth) {
    if (id >= MAX_WAVETABLES) return;
    wavetables[id].data = data;
    wavetables[id].size = size;
    wavetables[id].depth = depth;
}

void ESP32Synth::setControlRateHz(uint16_t hz) {
    if (hz == 0) return;
    controlRateHz = hz;
    controlIntervalSamples = (SYNTH_RATE / controlRateHz) ? (SYNTH_RATE / controlRateHz) : 1;
}

// Slide implementation: compute per-control-tick phaseInc delta to move smoothly from start->end over durationMs
void ESP32Synth::slide(uint8_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];

    // compute phaseInc for given centiHz using same formula as noteOn/setFrequency
    uint64_t numStart = (uint64_t)startFreqCentiHz << 32;
    uint64_t numEnd = (uint64_t)endFreqCentiHz << 32;
    uint32_t den = SYNTH_RATE * 100;
    uint32_t startInc = (uint32_t)(numStart / den);
    uint32_t endInc = (uint32_t)(numEnd / den);

    // duration in control ticks (rounded up)
    uint32_t ticks = (durationMs == 0) ? 0 : ((durationMs * controlRateHz + 999) / 1000);
    if (ticks == 0) {
        // immediate jump
        v->phaseInc = endInc;
        v->freqVal = endFreqCentiHz;
        v->slideActive = false;
        v->slideTicksRemaining = 0;
        v->slideTargetFreqCenti = endFreqCentiHz;
        v->slideTargetInc = endInc;
        v->slideRem = 0;
        v->slideRemAcc = 0;
        return;
    }

    int64_t diff = (int64_t)endInc - (int64_t)startInc;
    int32_t delta = (int32_t)(diff / (int64_t)ticks); // integer delta per tick (rounding)
    int64_t rem = diff - (int64_t)delta * (int64_t)ticks; // signed remainder

    v->phaseInc = startInc;
    v->slideDeltaInc = delta;
    v->slideRem = (int32_t)rem;
    v->slideRemAcc = 0;
    v->slideTicksTotal = ticks;
    v->slideTicksRemaining = ticks;
    v->slideTargetInc = endInc;
    v->slideTargetFreqCenti = endFreqCentiHz;
    v->slideActive = true;
    v->freqVal = startFreqCentiHz; // logical start
}

void ESP32Synth::slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    uint32_t start = voices[voice].freqVal;
    // If start is 0 (unused) fall back to computing from current phaseInc
    if (start == 0) {
        uint64_t curNum = (uint64_t)voices[voice].phaseInc * (uint64_t)SYNTH_RATE * 100;
        // compute approximate centiHz from phaseInc, avoid division by zero
        if (voices[voice].phaseInc != 0) start = (uint32_t)(curNum >> 32);
        else start = endFreqCentiHz;
    }
    // Do NOT assign final freq immediately; slide will update freqVal per tick and set exact final value at the end.
    slide(voice, start, endFreqCentiHz, durationMs);
}

IRAM_ATTR inline int16_t ESP32Synth::fetchWavetableSample(uint16_t id, uint32_t phase) {
    if (id >= MAX_WAVETABLES) return 0;
    const auto &e = wavetables[id];
    if (!e.data || e.size == 0) return 0;
    uint32_t idx = (uint32_t)(((uint64_t)phase * e.size) >> 32);
    if (e.depth == BITS_8) return (((uint8_t*)e.data)[idx] - 128) << 8;
    return ((int16_t*)e.data)[idx];
}
uint32_t ESP32Synth::getFrequencyCentiHz(uint8_t voice) {
    if (voice >= MAX_VOICES) return 0;
    return voices[voice].freqVal;
}
void IRAM_ATTR ESP32Synth::processControl() {
    // Called at control-rate to advance instrument arrays per voice
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];

        // Apply any active pitch slide (runs at control-rate; very cheap)
        if (vo->slideActive && vo->slideTicksRemaining > 0) {
            // base increment per tick
            vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + vo->slideDeltaInc);

            // distribute remainder like Bresenham to avoid truncation drift
            if (vo->slideRem != 0 && vo->slideTicksTotal > 0) {
                vo->slideRemAcc += vo->slideRem;
                int32_t sign = (vo->slideRem > 0) ? 1 : -1;
                if (abs(vo->slideRemAcc) >= (int32_t)vo->slideTicksTotal) {
                    vo->phaseInc = (uint32_t)((int32_t)vo->phaseInc + sign);
                    vo->slideRemAcc -= sign * (int32_t)vo->slideTicksTotal;
                }
            }

            vo->slideTicksRemaining--;

            // Update logical frequency (centi-Hz) based on current phaseInc -- cheap integer math
            uint32_t curFreqCenti = (uint32_t)(((uint64_t)vo->phaseInc * (uint64_t)SYNTH_RATE * 100) >> 32);
            vo->freqVal = curFreqCenti;

            if (vo->slideTicksRemaining == 0) {
                // snap to exact target to avoid any remaining drift
                vo->phaseInc = vo->slideTargetInc;
                // ensure final frequency is exact by using stored target
                vo->freqVal = vo->slideTargetFreqCenti;
                vo->slideActive = false;
                vo->slideRem = 0;
                vo->slideRemAcc = 0;
            }
        }

        if (!vo->active || !vo->inst) continue; // only instrument mode
        Instrument* inst = vo->inst;

        // Helper lambda to compute ticks per element for a stage
        auto compute_ticks = [&](uint32_t stageMs, uint8_t len)->uint32_t {
            if (len == 0) return 0; // no elements
            uint32_t ticks_total = ((uint32_t)stageMs * (uint32_t)controlRateHz + 999) / 1000; // round up
            uint32_t per = ticks_total / len;
            return per ? per : 1u;
        };

        switch (vo->envState) {
            case ENV_ATTACK: {
                uint8_t len = inst->lenA;
                if (len == 0) { vo->envState = ENV_DECAY; vo->stageIdx = 0; vo->controlTick = 0; break; }
                uint32_t ticksPer = compute_ticks(vo->attackMs, len);
                if (vo->controlTick == 0) vo->controlTick = ticksPer; // initialize element duration
                uint8_t idx = vo->stageIdx < len ? vo->stageIdx : (len - 1);

                // waveType may override wavetable: non-zero => basic wave (1..5)
                uint8_t wtype = (inst->waveTypeA) ? inst->waveTypeA[idx] : 0;
                uint8_t nextIdx = (idx + 1 < len) ? (idx + 1) : idx;
                uint8_t nextType = (inst->waveTypeA) ? inst->waveTypeA[nextIdx] : 0;

                if (wtype != 0) {
                    vo->currWaveIsBasic = 1; vo->currWaveType = wtype; vo->currWaveId = 0;
                    if (nextType != 0) { vo->nextWaveIsBasic = 1; vo->nextWaveType = nextType; vo->nextWaveId = 0; }
                    else { vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveA[nextIdx]; }
                } else {
                    vo->currWaveIsBasic = 0; vo->currWaveType = 0; vo->currWaveId = inst->waveA[idx];
                    if (nextType != 0) { vo->nextWaveIsBasic = 1; vo->nextWaveType = nextType; vo->nextWaveId = 0; }
                    else { vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveA[nextIdx]; }
                }

                vo->vol = inst->volA[idx];
                uint32_t elapsed = ticksPer - vo->controlTick;
                vo->morph = (uint8_t)((elapsed * 255) / ticksPer);
                vo->currEnvVal = ENV_MAX;
                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) { vo->envState = ENV_DECAY; vo->stageIdx = 0; vo->controlTick = 0; }
                }
                break;
            }
            case ENV_DECAY: {
                uint8_t len = inst->lenD;
                if (len == 0) { vo->envState = ENV_SUSTAIN; break; }
                uint32_t ticksPer = compute_ticks(vo->decayMs, len);
                if (vo->controlTick == 0) vo->controlTick = ticksPer;
                uint8_t idx = vo->stageIdx < len ? vo->stageIdx : (len - 1);
                uint8_t wtype = (inst->waveTypeD) ? inst->waveTypeD[idx] : 0;
                uint8_t nextIdx = (idx + 1 < len) ? (idx + 1) : idx;
                uint8_t nextType = (inst->waveTypeD) ? inst->waveTypeD[nextIdx] : 0;

                if (wtype != 0) {
                    vo->currWaveIsBasic = 1; vo->currWaveType = wtype; vo->currWaveId = 0;
                    if (nextType != 0) { vo->nextWaveIsBasic = 1; vo->nextWaveType = nextType; vo->nextWaveId = 0; }
                    else { vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveD[nextIdx]; }
                } else {
                    vo->currWaveIsBasic = 0; vo->currWaveType = 0; vo->currWaveId = inst->waveD[idx];
                    if (nextType != 0) { vo->nextWaveIsBasic = 1; vo->nextWaveType = nextType; vo->nextWaveId = 0; }
                    else { vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveD[nextIdx]; }
                }

                vo->vol = inst->volD[idx];
                uint32_t elapsed = ticksPer - vo->controlTick;
                vo->morph = (uint8_t)((elapsed * 255) / ticksPer);
                vo->currEnvVal = ENV_MAX;
                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) { vo->envState = ENV_SUSTAIN; vo->stageIdx = 0; vo->controlTick = 0; }
                }
                break;
            }
            case ENV_SUSTAIN: {
                // sustain holds single values; support basic waveType or wavetable single value
                vo->vol = inst->volS;
                uint8_t wtype = inst->waveTypeS;
                if (wtype != 0) {
                    vo->currWaveIsBasic = 1; vo->currWaveType = wtype; vo->currWaveId = 0;
                    vo->nextWaveIsBasic = 1; vo->nextWaveType = wtype; vo->nextWaveId = 0;
                } else {
                    vo->currWaveIsBasic = 0; vo->currWaveType = 0; vo->currWaveId = inst->waveS;
                    vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveS;
                }
                vo->morph = 0;
                vo->currEnvVal = ENV_MAX;
                break;
            }
            case ENV_RELEASE: {
                uint8_t len = inst->lenR;
                if (len == 0) { vo->active = false; break; }
                uint32_t ticksPer = compute_ticks(vo->releaseMs, len);
                if (vo->controlTick == 0) vo->controlTick = ticksPer;
                uint8_t idx = vo->stageIdx < len ? vo->stageIdx : (len - 1);
                uint8_t wtype = (inst->waveTypeR) ? inst->waveTypeR[idx] : 0;
                uint8_t nextIdx = (idx + 1 < len) ? (idx + 1) : idx;
                uint8_t nextType = (inst->waveTypeR) ? inst->waveTypeR[nextIdx] : 0;

                if (wtype != 0) {
                    vo->currWaveIsBasic = 1; vo->currWaveType = wtype; vo->currWaveId = 0;
                    if (nextType != 0) { vo->nextWaveIsBasic = 1; vo->nextWaveType = nextType; vo->nextWaveId = 0; }
                    else { vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveR[nextIdx]; }
                } else {
                    vo->currWaveIsBasic = 0; vo->currWaveType = 0; vo->currWaveId = inst->waveR[idx];
                    if (nextType != 0) { vo->nextWaveIsBasic = 1; vo->nextWaveType = nextType; vo->nextWaveId = 0; }
                    else { vo->nextWaveIsBasic = 0; vo->nextWaveType = 0; vo->nextWaveId = inst->waveR[nextIdx]; }
                }

                vo->vol = inst->volR[idx];
                uint32_t elapsed = ticksPer - vo->controlTick;
                vo->morph = (uint8_t)((elapsed * 255) / ticksPer);
                vo->currEnvVal = ENV_MAX;
                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) { vo->active = false; }
                }
                break;
            }
            default:
                break;
        }
    }
}