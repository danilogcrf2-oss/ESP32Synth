/**
 * @file ESP32Synth.cpp
 * @brief Implementação corrigida para ESP32 Core 3.x (Com Samples e Híbrido)
 * @brief Corrected implementation for ESP32 Core 3.x (With Samples and Hybrid)
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

// Registro global de samples (estático para velocidade)
// Global sample registry (static for speed)
SampleData registeredSamples[MAX_SAMPLES];

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
        voices[i].trmPhase = 0;
        voices[i].trmRateInc = 0;
        voices[i].trmDepth = 0;
        voices[i].pulseWidth = 0x80000000;

        // Padrões de instrumento
        // Instrument defaults
        voices[i].inst = nullptr;
        voices[i].instSample = nullptr; // Importante inicializar / Important to initialize
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

        // Padrões de slide
        // Slide defaults
        voices[i].slideActive = false;
        voices[i].slideTicksRemaining = 0;
        voices[i].slideTicksTotal = 0;
        voices[i].slideDeltaInc = 0;
        voices[i].slideTargetInc = 0;
        voices[i].slideTargetFreqCenti = 0;
        voices[i].slideRem = 0;
        voices[i].slideRemAcc = 0;

        // Padrões de arpejador
        // Arpeggiator defaults
        voices[i].arpActive = false;
        voices[i].arpLen = 0;
        voices[i].arpIdx = 0;
        voices[i].arpSpeedMs = 0;
        voices[i].arpTickCounter = 0;
        
        // Padrões de sample
        // Sample defaults
        voices[i].curSampleId = 0;
        voices[i].samplePos1616 = 0;
        voices[i].sampleInc1616 = 0;
        voices[i].sampleLoopMode = LOOP_OFF; 
        voices[i].sampleFinished = false;
    }

    // Inicializa entradas do registro de wavetable
    // Initialize wavetable registry entries
    for (uint16_t i = 0; i < MAX_WAVETABLES; i++) {
        wavetables[i].data = nullptr;
        wavetables[i].size = 0;
        wavetables[i].depth = BITS_8;
    }

    // Agendamento padrão da taxa de controle
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
    // Gera tabela de seno
    // Generate sine table
    for(int i=0; i<256; i++) {
        sineLUT[i] = (int16_t)(sin((i / 256.0) * 6.283185) * 20000.0);
    }

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
    int16_t buf[512]; 
    size_t written;
    while (1) {
        render(buf, 512); 
        i2s_channel_write(tx_handle, buf, sizeof(buf), &written, portMAX_DELAY);
    }
}

// Função principal de renderização
// Main rendering function
void IRAM_ATTR ESP32Synth::render(int16_t* buffer, int samples) {
    memset(buffer, 0, samples * sizeof(int16_t));
    int32_t mix[512] = {0}; 

    controlSampleCounter += (uint32_t)samples;
    while (controlSampleCounter >= controlIntervalSamples) {
        processControl();
        controlSampleCounter -= controlIntervalSamples;
    }

    for (int v = 0; v < MAX_VOICES; v++) {
        if (!voices[v].active) continue;
        Voice* vo = &voices[v];
        
        uint32_t currentInc = (uint32_t)((int32_t)vo->phaseInc + vo->vibOffset);

        for (int i = 0; i < samples; i++) {
            // --- LÓGICA DE ENVELOPE (ADSR) ---
            // --- ENVELOPE LOGIC (ADSR) ---
            
            if (!vo->inst && !vo->instSample) { 
                // Sintetizador Padrão (Sem instrumento definido)
                // Standard Synthesizer (No instrument defined)
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
            } 
            else if (vo->instSample) {
                // --- CORREÇÃO AQUI: ADSR Completo para Samples ---
                // --- FIX HERE: Full ADSR for Samples ---
                // Agora o sampler obedece Attack, Decay, Sustain e Release configurados via setEnv()
                // Now the sampler obeys Attack, Decay, Sustain and Release configured via setEnv()
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
                    case ENV_SUSTAIN: 
                        vo->currEnvVal = vo->levelSustain; 
                        break;
                    case ENV_RELEASE:
                        // Aqui está a mágica: O volume desce suavemente mantendo o loop tocando
                        // Here is the magic: The volume goes down smoothly keeping the loop playing
                        if (vo->currEnvVal > vo->rateRelease) vo->currEnvVal -= vo->rateRelease;
                        else { vo->currEnvVal = 0; vo->active = false; vo->envState = ENV_IDLE; }
                        break;
                    case ENV_IDLE: 
                        vo->currEnvVal = 0; 
                        break;
                }
            } 
            else {
                // Instrumento Tracker (Wavetable Sequencer) - Controla o próprio volume
                // Tracker Instrument (Wavetable Sequencer) - Controls its own volume
                vo->currEnvVal = ENV_MAX;
            }

            // --- GERAÇÃO DE ÁUDIO ---
            // --- AUDIO GENERATION ---

            int16_t s = 0;
            if (vo->type == WAVE_NOISE) {
                uint32_t noiseInc = currentInc << 4; 
                if (noiseInc >= 0x60000000) { 
                    vo->rngState = (vo->rngState * 1664525) + 1013904223;
                    s = (int16_t)(vo->rngState >> 16);
                } else {
                    uint32_t ph = vo->phase;
                    uint32_t nextPh = ph + noiseInc;
                    if (nextPh < ph) { 
                        vo->rngState = (vo->rngState * 1664525) + 1013904223;
                        vo->noiseSample = (int16_t)(vo->rngState >> 16);
                    }
                    s = vo->noiseSample;
                    vo->phase = nextPh;
                }
            } 
            else if (vo->type == WAVE_SAMPLE) {
                if (!vo->sampleFinished) {
                    const SampleData* sData = &registeredSamples[vo->curSampleId];
                    
                    if (sData->data) {
                        // Pega índice atual (usando shift de 16 bits do contador 64 bits)
                        // Get current index (using 16-bit shift of the 64-bit counter)
                        uint32_t idx = (uint32_t)(vo->samplePos1616 >> 16);
                        
                        // Segurança básica de memória
                        // Basic memory safety
                        if (idx >= sData->length) {
                             if(vo->sampleDirection) idx = sData->length - 1; 
                             else idx = 0;
                        }

                        s = sData->data[idx];

                        // --- CÁLCULO DA PRÓXIMA POSIÇÃO ---
                        // --- NEXT POSITION CALCULATION ---
                        
                        if (vo->sampleDirection) { 
                            // INDO (Para frente)
                            // GOING (Forward)
                            vo->samplePos1616 += vo->sampleInc1616;
                            
                            // Verifica Fim do Loop
                            // Check Loop End
                            if ((vo->samplePos1616 >> 16) >= vo->sampleLoopEnd) {
                                if (vo->sampleLoopMode == LOOP_FORWARD) {
                                    vo->samplePos1616 = (uint64_t)vo->sampleLoopStart << 16;
                                } 
                                else if (vo->sampleLoopMode == LOOP_PINGPONG) {
                                    vo->sampleDirection = false;
                                    vo->samplePos1616 = (uint64_t)vo->sampleLoopEnd << 16;
                                } 
                                else if (vo->sampleLoopMode == LOOP_OFF) {
                                    if ((vo->samplePos1616 >> 16) >= sData->length) {
                                        s = 0;
                                        vo->sampleFinished = true;
                                    }
                                }
                            }
                        } 
                        else { 
                            // VOLTANDO (Reverso ou volta do PingPong)
                            // RETURNING (Reverse or PingPong return)
                            if (vo->samplePos1616 >= vo->sampleInc1616) {
                                vo->samplePos1616 -= vo->sampleInc1616;
                            } else {
                                vo->samplePos1616 = 0;
                            }

                            // Verifica Início do Loop
                            // Check Loop Start
                            if ((vo->samplePos1616 >> 16) <= vo->sampleLoopStart) {
                                if (vo->sampleLoopMode == LOOP_PINGPONG) {
                                    vo->sampleDirection = true;
                                    vo->samplePos1616 = (uint64_t)vo->sampleLoopStart << 16;
                                } 
                                else if (vo->sampleLoopMode == LOOP_REVERSE) {
                                    vo->samplePos1616 = (uint64_t)vo->sampleLoopEnd << 16;
                                }
                                else if (vo->sampleLoopMode == LOOP_OFF) {
                                    s = 0;
                                    vo->sampleFinished = true;
                                }
                            }
                        }
                    }
                }
            }
            else if (vo->inst) {
                // Lógica do Instrumento Tracker (Wavetable Morphing)
                // Tracker Instrument Logic (Wavetable Morphing)
                int16_t sampleA = 0, sampleB = 0;
                uint32_t ph = vo->phase;
                uint32_t nextPh = ph + currentInc;
                bool wrapped = (nextPh < ph);
                if (wrapped) {
                    vo->rngState = (vo->rngState * 1664525) + 1013904223;
                    vo->noiseSample = (int16_t)(vo->rngState >> 16);
                }

                if (vo->currWaveIsBasic) {
                    switch (vo->currWaveType) {
                        case 1: sampleA = sineLUT[ph >> 24]; break; 
                        case 2: { int16_t saw = (int16_t)(ph >> 16); sampleA = (int16_t)(((saw ^ (saw >> 15)) * 2) - 32767); break; } 
                        case 3: sampleA = (int16_t)(ph >> 16); break; 
                        case 4: sampleA = (ph < vo->pulseWidth) ? 20000 : -20000; break; 
                        case 5: sampleA = vo->noiseSample; break; 
                        default: sampleA = 0; break;
                    }
                } else {
                    sampleA = fetchWavetableSample(vo->currWaveId, ph);
                }

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
                // Ondas Básicas
                // Basic Waves
                uint32_t ph = vo->phase;
                if (vo->type == WAVE_SAW) s = (int16_t)(ph >> 16); 
                else if (vo->type == WAVE_PULSE) s = (ph < vo->pulseWidth) ? 20000 : -20000;
                else if (vo->type == WAVE_TRIANGLE) {
                    int16_t saw = (int16_t)(ph >> 16);
                    s = (int16_t) (((saw ^ (saw >> 15)) * 2) - 32767);
                } else {
                    s = sineLUT[ph >> 24];
                }
                vo->phase += currentInc;
            }
            
            // --- FILTROS ---
            // --- FILTERS ---
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

            // --- MIXAGEM ---
            // --- MIXING ---
            int32_t valWithVel = (s * vo->vol) >> 8; 
            valWithVel = (valWithVel * vo->trmModGain) >> 8; 

            mix[i] += (int32_t)(((int64_t)valWithVel * vo->currEnvVal) >> 28);
        }
    }

    // --- LIMITER / SAÍDA ---
    // --- LIMITER / OUTPUT ---
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

void ESP32Synth::setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r) {
    // Configura envelope ADSR
    // Configure ADSR envelope
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
    Voice* vo = &voices[voice];
    
    vo->freqVal = freqCentiHz;
    vo->vol = volume;
    vo->active = true;
    vo->currEnvVal = 0;
    vo->envState = ENV_ATTACK;
    vo->filterLow = 0;
    vo->filterBand = 0;

    // --- LÓGICA DE SAMPLE ---
    // --- SAMPLE LOGIC ---
    if (vo->instSample) {
        vo->type = WAVE_SAMPLE;
        vo->sampleFinished = false;
        vo->sampleLoopMode = vo->instSample->loopMode;
        
        // Configura limites do loop
        // Configure loop limits
        vo->sampleLoopStart = vo->instSample->loopStart;
        
        // Se o usuário passar 0 no loopEnd, assumimos que é o final do sample
        // If user passes 0 in loopEnd, we assume it is the end of the sample
        // Mas precisamos descobrir o tamanho do sample primeiro (veja abaixo)
        // But we need to find out the sample size first (see below)
        
        // 1. Procura a Zona (lógica igual a antes)
        // 1. Search Zone (logic same as before)
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

        // Configura o final do loop se for 0
        // Configure loop end if 0
        if (sData) {
            if (vo->instSample->loopEnd == 0 || vo->instSample->loopEnd > sData->length) {
                vo->sampleLoopEnd = sData->length;
            } else {
                vo->sampleLoopEnd = vo->instSample->loopEnd;
            }
        }

        // Lógica de Direção Inicial
        // Initial Direction Logic
        if (vo->sampleLoopMode == LOOP_REVERSE) {
            vo->sampleDirection = false; // Começa voltando / Starts returning
            // Começa no fim do sample (ou no loopEnd se quiser)
            // Starts at sample end (or loopEnd if desired)
            // Aqui vou fazer começar no fim do arquivo pra tocar tudo ao contrário
            // Here I will make it start at the end of the file to play everything backwards
            vo->samplePos1616 = (uint64_t)sData->length << 16; 
        } else {
            vo->sampleDirection = true; // Começa indo / Starts going
            vo->samplePos1616 = 0;      // Começa do zero / Starts from zero
        }

        // 2. Calcula Velocidade (Pitch)
        // 2. Calculate Speed (Pitch)
        if (sData && sData->data && root > 0) {
            float pitchRatio = (float)freqCentiHz / (float)root;
            float rateRatio = (float)sData->sampleRate / (float)SYNTH_RATE;
            vo->sampleInc1616 = (uint32_t)(pitchRatio * rateRatio * 65536.0f);
        } else {
            vo->sampleInc1616 = 0;
        }
    }
    // --- LÓGICA DE SÍNTESE PADRÃO ---
    // --- STANDARD SYNTHESIS LOGIC ---
    else {
        if (vo->type == WAVE_NOISE) vo->rngState += micros(); 
        
        if (vo->arpActive) {
            vo->arpIdx = 0;       
            vo->arpTickCounter = 0;
        }

        uint64_t num = (uint64_t)freqCentiHz << 32;
        uint32_t den = SYNTH_RATE * 100;
        vo->phaseInc = (uint32_t)(num / den);

        if (vo->inst) {
            vo->stageIdx = 0;
            vo->controlTick = 0;
            vo->currWaveId = 0;
            vo->nextWaveId = 0;
            vo->morph = 0;
            vo->currEnvVal = ENV_MAX;
            processControl(); 
        }
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
    voices[voice].instSample = nullptr; // CORREÇÃO: Limpa o ponteiro de sample / FIX: Clears sample pointer
    
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
        voices[voice].currEnvVal = 0;
        voices[voice].envState = ENV_ATTACK;
    } else {
        voices[voice].currEnvVal = ENV_MAX; 
    }
}

void ESP32Synth::detachWave(uint8_t voice, WaveType type) {
    if (voice >= MAX_VOICES) return;
    setInstrument(voice, (Instrument*)nullptr); // CORREÇÃO: Cast para resolver ambiguidade / FIX: Cast to resolve ambiguity
    setWave(voice, type);
}

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

void ESP32Synth::slide(uint8_t voice, uint32_t startFreqCentiHz, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];

    uint64_t numStart = (uint64_t)startFreqCentiHz << 32;
    uint64_t numEnd = (uint64_t)endFreqCentiHz << 32;
    uint32_t den = SYNTH_RATE * 100;
    uint32_t startInc = (uint32_t)(numStart / den);
    uint32_t endInc = (uint32_t)(numEnd / den);

    uint32_t ticks = (durationMs == 0) ? 0 : ((durationMs * controlRateHz + 999) / 1000);
    if (ticks == 0) {
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
    int32_t delta = (int32_t)(diff / (int64_t)ticks); 
    int64_t rem = diff - (int64_t)delta * (int64_t)ticks; 

    v->phaseInc = startInc;
    v->slideDeltaInc = delta;
    v->slideRem = (int32_t)rem;
    v->slideRemAcc = 0;
    v->slideTicksTotal = ticks;
    v->slideTicksRemaining = ticks;
    v->slideTargetInc = endInc;
    v->slideTargetFreqCenti = endFreqCentiHz;
    v->slideActive = true;
    v->freqVal = startFreqCentiHz; 
}

void ESP32Synth::slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs) {
    if (voice >= MAX_VOICES) return;
    uint32_t start = voices[voice].freqVal;
    if (start == 0) {
        uint64_t curNum = (uint64_t)voices[voice].phaseInc * (uint64_t)SYNTH_RATE * 100;
        if (voices[voice].phaseInc != 0) start = (uint32_t)(curNum >> 32);
        else start = endFreqCentiHz;
    }
    slide(voice, start, endFreqCentiHz, durationMs);
}

void ESP32Synth::registerSample(uint16_t id, const int16_t* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz) {
    if (id >= MAX_SAMPLES) return;
    registeredSamples[id].data = data;
    registeredSamples[id].length = length;
    registeredSamples[id].sampleRate = sampleRate;
    registeredSamples[id].rootFreqCentiHz = rootFreqCentiHz;
}

void ESP32Synth::setInstrument(uint8_t voice, Instrument_Sample* inst) {
    if (voice >= MAX_VOICES) return;
    voices[voice].instSample = inst;
    voices[voice].inst = nullptr; 
    
    voices[voice].currEnvVal = 0;
    voices[voice].envState = ENV_IDLE;
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
    uint32_t envNorm = voices[voice].currEnvVal >> 20; 
    uint32_t totalLevel = ((uint32_t)voices[voice].vol * envNorm);
    return (uint8_t)(totalLevel >> 8);
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

void IRAM_ATTR ESP32Synth::processControl() {
    for (int v = 0; v < MAX_VOICES; v++) {
        Voice* vo = &voices[v];

        if (vo->vibDepthInc > 0) {
            vo->vibPhase += (vo->vibRateInc * controlIntervalSamples);
            int16_t lfoVal = sineLUT[vo->vibPhase >> 24];
            vo->vibOffset = ((int64_t)vo->vibDepthInc * lfoVal) >> 14;
        } else {
            vo->vibOffset = 0;
        }

        if (vo->trmDepth > 0) {
            vo->trmPhase += (vo->trmRateInc * controlIntervalSamples);
            int16_t lfo = sineLUT[vo->trmPhase >> 24];
            int32_t reduction = ((int32_t)(lfo + 20000) * vo->trmDepth) >> 15;
            vo->trmModGain = 256 - reduction;
        } else {
            vo->trmModGain = 256;
        }

        if (vo->arpActive && vo->arpLen > 0) {
            if (vo->arpTickCounter == 0) {
                uint32_t targetFreq = vo->arpNotes[vo->arpIdx];
                vo->freqVal = targetFreq;
                uint64_t num = (uint64_t)targetFreq << 32;
                uint32_t den = SYNTH_RATE * 100;
                vo->phaseInc = (uint32_t)(num / den);
                vo->arpIdx++;
                if (vo->arpIdx >= vo->arpLen) vo->arpIdx = 0;
                vo->arpTickCounter = ((uint32_t)vo->arpSpeedMs * (uint32_t)controlRateHz + 999) / 1000;
                if (vo->arpTickCounter == 0) vo->arpTickCounter = 1; 
            }
            vo->arpTickCounter--;
        }

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
            vo->freqVal = (uint32_t)(((uint64_t)vo->phaseInc * (uint64_t)SYNTH_RATE * 100) >> 32);
            if (vo->slideTicksRemaining == 0) {
                vo->phaseInc = vo->slideTargetInc;
                vo->freqVal = vo->slideTargetFreqCenti;
                vo->slideActive = false;
            }
        }

        if (!vo->active || !vo->inst) continue;
        Instrument* inst = vo->inst;

        auto decodeWave = [&](int16_t wVal, uint8_t& isBasic, uint8_t& type, uint16_t& id) {
            if (wVal < 0) { 
                isBasic = 1;
                id = 0;
                type = (uint8_t)(-wVal); 
            } else { 
                isBasic = 0;
                type = 0;
                id = (uint16_t)wVal;
            }
        };

        auto compute_ticks = [&](uint32_t stageMs)->uint32_t {
            if (stageMs == 0) return 1;
            return ((uint32_t)stageMs * (uint32_t)controlRateHz + 999) / 1000;
        };

        switch (vo->envState) {
            case ENV_ATTACK: { 
                uint8_t len = inst->seqLen;
                if (len == 0) { vo->envState = ENV_SUSTAIN; break; }

                if (vo->controlTick == 0) vo->controlTick = compute_ticks(inst->seqSpeedMs);

                uint8_t idx = vo->stageIdx;
                if (idx >= len) idx = len - 1;

                vo->vol = inst->seqVolumes[idx];

                decodeWave(inst->seqWaves[idx], vo->currWaveIsBasic, vo->currWaveType, vo->currWaveId);

                uint8_t nextIdx = (idx + 1 < len) ? (idx + 1) : idx;
                decodeWave(inst->seqWaves[nextIdx], vo->nextWaveIsBasic, vo->nextWaveType, vo->nextWaveId);

                if (inst->smoothMorph) {
                    uint32_t totalTicks = compute_ticks(inst->seqSpeedMs);
                    vo->morph = (uint8_t)(((totalTicks - vo->controlTick) * 255) / totalTicks);
                } else {
                    vo->morph = 0; 
                }

                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) vo->envState = ENV_SUSTAIN;
                }
                break;
            }
            
            case ENV_DECAY: { 
                vo->envState = ENV_SUSTAIN;
                break; 
            }

            case ENV_SUSTAIN: {
                vo->vol = inst->susVol;
                
                decodeWave(inst->susWave, vo->currWaveIsBasic, vo->currWaveType, vo->currWaveId);
                decodeWave(inst->susWave, vo->nextWaveIsBasic, vo->nextWaveType, vo->nextWaveId);
                vo->morph = 0;
                break;
            }

            case ENV_RELEASE: {
                uint8_t len = inst->relLen;
                if (len == 0) { vo->active = false; break; }

                if (vo->controlTick == 0) vo->controlTick = compute_ticks(inst->relSpeedMs);

                uint8_t idx = vo->stageIdx;
                if (idx >= len) idx = len - 1;

                vo->vol = inst->relVolumes[idx];
                
                decodeWave(inst->relWaves[idx], vo->currWaveIsBasic, vo->currWaveType, vo->currWaveId);
                
                uint8_t nextIdx = (idx + 1 < len) ? (idx + 1) : idx;
                decodeWave(inst->relWaves[nextIdx], vo->nextWaveIsBasic, vo->nextWaveType, vo->nextWaveId);

                if (inst->smoothMorph) {
                    uint32_t totalTicks = compute_ticks(inst->relSpeedMs);
                    vo->morph = (uint8_t)(((totalTicks - vo->controlTick) * 255) / totalTicks);
                } else {
                    vo->morph = 0;
                }

                if (vo->controlTick > 0) vo->controlTick--;
                if (vo->controlTick == 0) {
                    vo->stageIdx++;
                    if (vo->stageIdx >= len) vo->active = false; 
                }
                break;
            }
            default: break;
        }
    }
}