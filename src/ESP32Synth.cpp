/**
 * @file ESP32Synth.cpp
 * @brief Implementação do motor de áudio e processamento de sinal.
 * @author Danilo
 */

#include "ESP32Synth.h"

// Tabela de busca para frequências de corte do filtro (Logarítmica)
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
        voices[i].pulseWidth = 0x80000000; // 50%
    }
}

ESP32Synth::~ESP32Synth() {
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
    }
}

bool ESP32Synth::begin(int clkPin, int dataPin) {
    // Inicializa tabela de busca da onda senoidal
    for(int i=0; i<256; i++) {
        sineLUT[i] = (int16_t)(sin((i / 256.0) * 6.283185) * 20000.0);
    }

    // Configuração do driver I2S para modo PDM 
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if (i2s_new_channel(&chan_cfg, &tx_handle, NULL) != ESP_OK) return false;

    int bclk = clkPin;
    int dout = dataPin;

    #if defined(CONFIG_IDF_TARGET_ESP32)
        if (bclk == -1) bclk = 25; 
        if (dout == -1) dout = 22; 
    #else
        if (bclk == -1) bclk = I2S_WS_PIN;
        if (dout == -1) dout = I2S_DATA_PIN;
    #endif

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(I2S_RATE),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = (gpio_num_t)bclk, .dout = (gpio_num_t)dout }
    };

    if (i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_cfg) != ESP_OK) return false;
    if (i2s_channel_enable(tx_handle) != ESP_OK) return false;

    // Inicia tarefa de áudio no Core 1 para evitar interrupções 
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

void IRAM_ATTR ESP32Synth::render(int16_t* buffer, int samples) {
    memset(buffer, 0, samples * sizeof(int16_t));
    int32_t mix[256] = {0}; // Acumulador de 32 bits 

    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) continue;
        Voice* vo = &voices[v];
        
        for (int i = 0; i < samples; i++) {
            // Processamento do Envelope ADSR
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

            // Geração de Onda e Modulação de Vibrato
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
            } else if (vo->type == WAVE_WAVETABLE && vo->wtData) {
                // Cálculo de Wavetable (suporte a 8-bit e 16-bit)
                uint32_t idx = (uint32_t)(((uint64_t)vo->phase * vo->wtSize) >> 32); 
                if (vo->depth == BITS_8) s = (((uint8_t*)vo->wtData)[idx] - 128) << 8;
                else s = ((int16_t*)vo->wtData)[idx];
                vo->phase += currentInc;
            } else {
                uint32_t ph = vo->phase;
                if (vo->type == WAVE_SAW) s = (int16_t)(ph >> 16); 
                else if (vo->type == WAVE_PULSE) {
                    // PWM: Compara fase com largura de pulso definida
                    s = (ph < vo->pulseWidth) ? 20000 : -20000;
                }
                else if (vo->type == WAVE_TRIANGLE) {
                    int16_t saw = (int16_t)(ph >> 16);
                    s = (int16_t) (((saw ^ (saw >> 15)) * 2) - 32767);
                } else s = sineLUT[ph >> 24];
                vo->phase += currentInc;
            }
            
            // Processamento do Filtro (Chamberlin SVF)
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

            // Mixagem com Volume e Envelope 
            int32_t valWithVel = (s * vo->vol) >> 8; 
            mix[i] += (int32_t)(((int64_t)valWithVel * vo->currEnvVal) >> 28);
        }
    }

    // Estágio de Limiter e Saída
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
    // Converte 8-bit (0-255) para 32-bit via bitshift para comparação rápida
    voices[voice].pulseWidth = (uint32_t)width << 24; 
}