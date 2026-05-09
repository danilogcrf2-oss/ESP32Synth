#pragma once

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

    // CORREÇÃO: Força matematicamente que o byte-alvo caia com precisão absoluta
    // no começo do Frame de Áudio para nunca "rachar" um sample no meio.
    startBytes -= (startBytes - trk->dataStartPos) % bytesPerSample;
    endBytes -= (endBytes - trk->dataStartPos) % bytesPerSample;

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