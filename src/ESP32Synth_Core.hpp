#pragma once

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
            uint64_t ratio1616 = ((uint64_t)freqCentiHz << 16) / root;
            vo->sampleInc1616 = (uint32_t)((ratio1616 * sData->sampleRate) / _sampleRate);
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

// --- Envelope (ADSR) ---
void ESP32Synth::setEnv(uint16_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r) {
    if (voice >= MAX_VOICES) return;
    Voice* v = &voices[voice];
    v->levelSustain = (uint32_t)s * (ENV_MAX / 255);
    
    // Trava de segurança contra divisão por zero (spm mínimo de 1)
    uint32_t spm = (_sampleRate >= 1000) ? (_sampleRate / 1000) : 1; 
    
    v->rateAttack = (a == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)a * spm);
    v->rateDecay = (d == 0) ? ENV_MAX : (ENV_MAX - v->levelSustain) / ((uint32_t)d * spm);
    v->rateRelease = (r == 0) ? ENV_MAX : ENV_MAX / ((uint32_t)r * spm);
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