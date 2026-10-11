#pragma once
#include "ESP32Synth.h"

#if ENABLE_MP3
#include "helix/mp3dec.h"
#endif

#ifdef ARDUINO
int8_t ESP32Synth::setupStream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz, bool loop) {
#else
int8_t ESP32Synth::setupStream(uint16_t voice, const char* path, uint32_t rootFreqCentiHz, bool loop) {
#endif
    if (voice >= MAX_VOICES) return -1;
    #if ENABLE_MP3
    size_t pLen = strlen(path);
    if (pLen >= 4 && strcasecmp(path + pLen - 4, ".mp3") == 0) {
        #ifdef ARDUINO
        return setupMP3Stream(voice, fs, path, rootFreqCentiHz, loop);
        #else
        return setupMP3Stream(voice, path, rootFreqCentiHz, loop);
        #endif
    }
    #endif
    // Start SD task on-demand
    if (streamTaskHandle == NULL) {
        xTaskCreatePinnedToCore(streamLoaderTask, "SynthStream", 8192, this, 1, &streamTaskHandle, SYNTH_STREAM_TASK_CORE);
    }

    // Clear existing stream on this voice
    if (voices[voice].streamTrackId >= 0) {
        stopStream(voice);
    }

    // Find free stream slot
    int8_t streamId = -1;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!streams[i].active) { streamId = i; break; }
    }
    if (streamId == -1) return -1;

    SYNTH_FILE file = SYNTH_STREAM_OPEN();
    if (!SYNTH_FILE_VALID(file)) return -1;

    uint32_t sRate, dPos, dSize;
    uint16_t channels, bits;

    if (!parseWavHeader(file, sRate, dPos, dSize, channels, bits)) {
        SYNTH_FILE_CLOSE(file);
        return -1;
    }
    SYNTH_FILE_SEEK(file, dPos);

    StreamTrack* trk = &streams[streamId];
    memset(trk, 0, sizeof(StreamTrack));

    trk->file           = file;
    trk->sampleRate     = sRate;
    trk->dataStartPos   = dPos;
    trk->dataSize       = dSize;
    trk->numChannels    = channels;
    trk->bitsPerSample  = bits;
    trk->loop           = loop;
    trk->seekTarget     = -1;
    trk->loopStartBytes = dPos;
    trk->loopEndBytes   = dPos + dSize;
    trk->rootFreqCentiHz = rootFreqCentiHz;
    trk->active         = true;

    uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
    if (bytesPerSample == 0) bytesPerSample = 2;
    uint32_t totalSamples = trk->dataSize / bytesPerSample;
    trk->durationMs = (sRate > 0) ? (uint32_t)(((uint64_t)totalSamples * 1000ULL) / sRate) : 0;

    Voice* vo           = &voices[voice];
    vo->type            = WAVE_STREAM;
    vo->streamTrackId   = streamId;
    vo->active          = false;
    vo->envState        = ENV_IDLE;
    vo->currEnvVal      = 0;

    return streamId;
}

#ifdef ARDUINO
int8_t ESP32Synth::playStream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
#else
int8_t ESP32Synth::playStream(uint16_t voice, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
#endif
    if (voice >= MAX_VOICES) return -1;

#ifdef ARDUINO
    int8_t streamId = setupStream(voice, fs, path, rootFreqCentiHz, loop);
#else
    int8_t streamId = setupStream(voice, path, rootFreqCentiHz, loop);
#endif

    if (streamId < 0) return -1;

    StreamTrack* trk = &streams[streamId];
    trk->playing     = true;

    // Wait briefly for the buffer to pre-fill
    int timeout = 100;
    while (trk->head == trk->tail && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    Voice* vo = &voices[voice];
    vo->vol   = volume << _volShift;

    uint32_t rFreq = (rootFreqCentiHz > 0) ? rootFreqCentiHz : ((trk->sampleRate > 0) ? trk->sampleRate * 100 : 4800000);
    if (vo->freqVal == 0) vo->freqVal = rFreq;
    uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / rFreq;
    uint32_t sRate = (trk->sampleRate > 0) ? trk->sampleRate : _sampleRate;
    vo->sampleInc1616  = (uint32_t)((ratio1616 * sRate) / _sampleRate);

    // Start ADSR envelope
    if (vo->rateAttack >= ENV_MAX) {
        vo->currEnvVal = ENV_MAX;
        vo->envState   = ENV_DECAY;
    } else {
        vo->currEnvVal = 0;
        vo->envState   = ENV_ATTACK;
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
        trk->active  = false;

        #if ENABLE_MP3
        if (trk->isMP3) {
            if (trk->mp3Decoder) {
                vTaskDelay(pdMS_TO_TICKS(2));
                MP3FreeDecoder((HMP3Decoder)trk->mp3Decoder);
                trk->mp3Decoder = nullptr;
            }
            if (trk->mp3InBuf) {
                heap_caps_free(trk->mp3InBuf);
                trk->mp3InBuf = nullptr;
            }
        }
        #endif

        SYNTH_FILE_CLOSE(trk->file);
        voices[voice].streamTrackId = -1;
    }
}

void ESP32Synth::seekStreamMs(uint16_t voice, uint32_t ms) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk      = &streams[voices[voice].streamTrackId];
        uint32_t sampleTarget = (uint32_t)(((uint64_t)ms * trk->sampleRate) / 1000ULL);
        trk->seeking          = true;
        trk->seekTarget       = sampleTarget;
    }
}

void ESP32Synth::setStreamLoopPointsMs(uint16_t voice, uint32_t startMs, uint32_t endMs) {
    if (voice >= MAX_VOICES || voices[voice].streamTrackId < 0) return;
    StreamTrack* trk = &streams[voices[voice].streamTrackId];

    uint32_t bytesPerSample = (trk->bitsPerSample / 8) * trk->numChannels;
    if (bytesPerSample == 0) bytesPerSample = 2;

    uint32_t startBytes = trk->dataStartPos + (uint32_t)((((uint64_t)startMs * trk->sampleRate) / 1000ULL) * bytesPerSample);
    uint32_t endBytes   = trk->dataStartPos + (uint32_t)((((uint64_t)endMs   * trk->sampleRate) / 1000ULL) * bytesPerSample);

    startBytes -= (startBytes - trk->dataStartPos) % bytesPerSample;
    endBytes   -= (endBytes   - trk->dataStartPos) % bytesPerSample;

    if (startBytes < trk->dataStartPos)                             startBytes = trk->dataStartPos;
    if (endBytes > trk->dataStartPos + trk->dataSize || endMs == 0) endBytes   = trk->dataStartPos + trk->dataSize;

    trk->loopStartBytes = startBytes;
    trk->loopEndBytes   = endBytes;
    trk->loop           = true;
}

uint32_t ESP32Synth::getStreamPositionMs(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        if (trk->sampleRate == 0) return 0;
        return (uint32_t)(((uint64_t)trk->samplesPlayed * 1000ULL) / trk->sampleRate);
    }
    return 0;
}

uint32_t ESP32Synth::getStreamDurationMs(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].durationMs;
    }
    return 0;
}

bool ESP32Synth::isStreamPlaying(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].playing;
    }
    return false;
}

#ifdef ARDUINO
bool ESP32Synth::startRecording(fs::FS &fs, const char* path) {
#else
bool ESP32Synth::startRecording(const char* path) {
#endif
    if (_isRecording) return false;

    if (!_recBuffer) {
        _recBuffer = (int16_t*)heap_caps_malloc(RECORD_BUF_SAMPLES * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!_recBuffer) return false;
    }

#ifdef ARDUINO
    _recordFile = fs.open(path, "w");
#else
    _recordFile = fopen(path, "wb");
#endif

    if (!SYNTH_FILE_VALID(_recordFile)) {
        heap_caps_free(_recBuffer);
        _recBuffer = nullptr;
        return false;
    }

    _recHead = 0;
    _recTail = 0;
    _recordedDataSize = 0;

    uint8_t dummyHeader[44] = {0};
    SYNTH_FILE_WRITE(_recordFile, dummyHeader, 44);

    _isRecording = true;

    if (xTaskCreatePinnedToCore(sdWriterTask, "SynthRec", 4096, this, 1, &recordTaskHandle, SYNTH_SD_TASK_CORE) != pdPASS) {
        _isRecording = false;
        SYNTH_FILE_CLOSE(_recordFile);
        heap_caps_free(_recBuffer);
        _recBuffer = nullptr;
        return false;
    }

    return true;
}

void ESP32Synth::stopRecording() {
    if (!_isRecording) return;
    _isRecording = false;

    while (recordTaskHandle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool ESP32Synth::isRecordingActive() {
    return _isRecording;
}

// Background Writer Task
void ESP32Synth::sdWriterTask(void* param) {
    ESP32Synth* synth = (ESP32Synth*)param;
    const int CHUNK_SAMPLES = 1024;
    const int CHUNK_BYTES = CHUNK_SAMPLES * 2;
    uint8_t* writeBuf = (uint8_t*)heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (!writeBuf) {
        synth->_isRecording = false;
        synth->recordTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (synth->_isRecording || synth->_recHead != synth->_recTail) {
        uint32_t h = synth->_recHead;
        uint32_t t = synth->_recTail;
        uint32_t avail = (RECORD_BUF_SAMPLES + h - t) & RECORD_BUF_MASK;

        uint32_t samplesToWrite = 0;
        if (avail >= CHUNK_SAMPLES) {
            samplesToWrite = CHUNK_SAMPLES;
        } else if (!synth->_isRecording && avail > 0) {
            samplesToWrite = avail;
        }

        if (samplesToWrite > 0) {
            uint32_t tillEnd = RECORD_BUF_SAMPLES - t;
            uint32_t bytesToWrite = samplesToWrite * 2;

            if (samplesToWrite <= tillEnd) {
                memcpy(writeBuf, (void*)&synth->_recBuffer[t], bytesToWrite);
            } else {
                memcpy(writeBuf, (void*)&synth->_recBuffer[t], tillEnd * 2);
                memcpy(writeBuf + (tillEnd * 2), (void*)&synth->_recBuffer[0], (samplesToWrite - tillEnd) * 2);
            }
            synth->_recTail = (t + samplesToWrite) & RECORD_BUF_MASK;

            SYNTH_FILE_WRITE(synth->_recordFile, writeBuf, bytesToWrite);
            synth->_recordedDataSize += bytesToWrite;
        } else {
            if (!synth->_isRecording) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    uint32_t sRate = synth->_sampleRate;
    uint32_t dataSize = synth->_recordedDataSize;
    uint32_t fileSize = dataSize + 36;
    uint32_t byteRate = sRate * 2;

    uint8_t head[44];
    head[0] = 'R'; head[1] = 'I'; head[2] = 'F'; head[3] = 'F';
    head[4] = (uint8_t)(fileSize); head[5] = (uint8_t)(fileSize >> 8); head[6] = (uint8_t)(fileSize >> 16); head[7] = (uint8_t)(fileSize >> 24);
    head[8] = 'W'; head[9] = 'A'; head[10] = 'V'; head[11] = 'E';
    head[12] = 'f'; head[13] = 'm'; head[14] = 't'; head[15] = ' ';
    head[16] = 16; head[17] = 0; head[18] = 0; head[19] = 0;
    head[20] = 1;  head[21] = 0;
    head[22] = 1;  head[23] = 0;
    head[24] = (uint8_t)(sRate); head[25] = (uint8_t)(sRate >> 8); head[26] = (uint8_t)(sRate >> 16); head[27] = (uint8_t)(sRate >> 24);
    head[28] = (uint8_t)(byteRate); head[29] = (uint8_t)(byteRate >> 8); head[30] = (uint8_t)(byteRate >> 16); head[31] = (uint8_t)(byteRate >> 24);
    head[32] = 2;  head[33] = 0;
    head[34] = 16; head[35] = 0;
    head[36] = 'd'; head[37] = 'a'; head[38] = 't'; head[39] = 'a';
    head[40] = (uint8_t)(dataSize); head[41] = (uint8_t)(dataSize >> 8); head[42] = (uint8_t)(dataSize >> 16); head[43] = (uint8_t)(dataSize >> 24);

    SYNTH_FILE_SEEK(synth->_recordFile, 0);
    SYNTH_FILE_WRITE(synth->_recordFile, head, 44);
    SYNTH_FILE_CLOSE(synth->_recordFile);

    heap_caps_free(writeBuf);
    if (synth->_recBuffer) {
        heap_caps_free(synth->_recBuffer);
        synth->_recBuffer = nullptr;
    }

    synth->recordTaskHandle = NULL;
    vTaskDelete(NULL);
}

#if ENABLE_MP3
int8_t ESP32Synth::playMP3StreamFromFlash(uint16_t voice, const uint8_t* mp3Data, uint32_t mp3Size, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
    if (voice >= MAX_VOICES || !mp3Data || mp3Size == 0) return -1;

    if (streamTaskHandle == NULL) {
        xTaskCreatePinnedToCore(streamLoaderTask, "SynthStream", 8192, this, 1, &streamTaskHandle, SYNTH_STREAM_TASK_CORE);
    }

    if (voices[voice].streamTrackId >= 0) stopStream(voice);

    int8_t streamId = -1;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!streams[i].active) { streamId = i; break; }
    }
    if (streamId == -1) return -1;

    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) return -1;

    int bytesLeft = (int)mp3Size;
    unsigned char* probePtr = (unsigned char*)mp3Data;
    int syncOffset = MP3FindSyncWord(probePtr, bytesLeft);
    if (syncOffset < 0) {
        MP3FreeDecoder(decoder);
        return -1;
    }

    probePtr += syncOffset;
    bytesLeft -= syncOffset;

    MP3FrameInfo frameInfo;
    uint32_t sRate = 44100;
    uint16_t chans = 2;
    uint32_t bitrate = 128000;
    uint32_t durMs = 0;

    if (MP3GetNextFrameInfo(decoder, &frameInfo, probePtr) == ERR_MP3_NONE && frameInfo.samprate > 0) {
        sRate   = frameInfo.samprate;
        chans   = (uint16_t)frameInfo.nChans;
        bitrate = frameInfo.bitrate;

        const uint8_t* fh = probePtr;
        bool isMpeg1 = (((fh[1] >> 3) & 3) == 3);
        uint32_t samplesPerFrame = isMpeg1 ? 1152 : 576;

        int scanLimit = 48;
        if (scanLimit > bytesLeft - 16) scanLimit = bytesLeft - 16;

        for (int p = 13; p <= scanLimit; p++) {
            const uint8_t* xp = probePtr + p;
            if ((xp[0] == 'X' && xp[1] == 'i' && xp[2] == 'n' && xp[3] == 'g') ||
                (xp[0] == 'I' && xp[1] == 'n' && xp[2] == 'f' && xp[3] == 'o')) {
                uint32_t flags = ((uint32_t)xp[4] << 24) | ((uint32_t)xp[5] << 16) | ((uint32_t)xp[6] << 8) | xp[7];
                int offset = 8;
                uint32_t frames = 0, bytes = 0;
                if (flags & 0x01) {
                    frames = ((uint32_t)xp[offset] << 24) | ((uint32_t)xp[offset + 1] << 16) | ((uint32_t)xp[offset + 2] << 8) | xp[offset + 3];
                    offset += 4;
                }
                if (flags & 0x02) {
                    bytes = ((uint32_t)xp[offset] << 24) | ((uint32_t)xp[offset + 1] << 16) | ((uint32_t)xp[offset + 2] << 8) | xp[offset + 3];
                }
                if (frames > 0 && sRate > 0) {
                    uint64_t totalSmp = (uint64_t)frames * samplesPerFrame;
                    durMs = (uint32_t)((totalSmp * 1000ULL) / sRate);
                    uint32_t bCount = (bytes > 0) ? bytes : mp3Size;
                    if (durMs > 0) bitrate = (uint32_t)(((uint64_t)bCount * 8000ULL) / durMs);
                }
                break;
            } else if (xp[0] == 'V' && xp[1] == 'B' && xp[2] == 'R' && xp[3] == 'I') {
                uint32_t bytes  = ((uint32_t)xp[10] << 24) | ((uint32_t)xp[11] << 16) | ((uint32_t)xp[12] << 8) | xp[13];
                uint32_t frames = ((uint32_t)xp[14] << 24) | ((uint32_t)xp[15] << 16) | ((uint32_t)xp[16] << 8) | xp[17];
                if (frames > 0 && sRate > 0) {
                    uint64_t totalSmp = (uint64_t)frames * samplesPerFrame;
                    durMs = (uint32_t)((totalSmp * 1000ULL) / sRate);
                    uint32_t bCount = (bytes > 0) ? bytes : mp3Size;
                    if (durMs > 0) bitrate = (uint32_t)(((uint64_t)bCount * 8000ULL) / durMs);
                }
                break;
            }
        }

        if (durMs == 0 && bitrate <= 64000 && bytesLeft > 4) {
            int nextSync = MP3FindSyncWord(probePtr + 4, bytesLeft - 4);
            if (nextSync >= 0) {
                MP3FrameInfo nextInfo;
                if (MP3GetNextFrameInfo(decoder, &nextInfo, probePtr + 4 + nextSync) == ERR_MP3_NONE) {
                    if (nextInfo.bitrate > bitrate) bitrate = nextInfo.bitrate;
                }
            }
        }
    }

    if (durMs == 0) {
        if (bitrate <= 32000) bitrate = 128000;
        durMs = (bitrate > 0) ? (uint32_t)(((uint64_t)mp3Size * 8000ULL) / bitrate) : 0;
    }

    StreamTrack* trk = &streams[streamId];
    memset(trk, 0, sizeof(StreamTrack));

    trk->sampleRate     = sRate;
    trk->numChannels    = chans;
    trk->mp3Bitrate     = bitrate;
    trk->durationMs     = durMs;
    trk->isMP3          = true;
    trk->mp3Decoder     = (void*)decoder;
    trk->mp3Data        = mp3Data;
    trk->dataSize       = mp3Size;
    trk->mp3Offset      = syncOffset;
    trk->bitsPerSample  = 16;
    trk->loop           = loop;
    trk->seekTarget     = -1;
    trk->rootFreqCentiHz = (rootFreqCentiHz > 0) ? rootFreqCentiHz : 0;
    trk->active         = true;
    trk->playing        = true;

    int timeout = 100;
    while (trk->head == trk->tail && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    Voice* vo = &voices[voice];
    vo->type          = WAVE_STREAM;
    vo->streamTrackId = streamId;
    vo->vol           = volume << _volShift;
    vo->freqVal       = (rootFreqCentiHz > 0) ? rootFreqCentiHz : 4800000;

    if (trk->rootFreqCentiHz > 0) {
        uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / trk->rootFreqCentiHz;
        vo->sampleInc1616  = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);
    } else {
        vo->sampleInc1616  = (uint32_t)(((uint64_t)trk->sampleRate << 16) / _sampleRate);
    }

    if (vo->rateAttack >= ENV_MAX) {
        vo->currEnvVal = ENV_MAX;
        vo->envState   = ENV_DECAY;
    } else {
        vo->currEnvVal = 0;
        vo->envState   = ENV_ATTACK;
    }
    vo->active = true;

    return streamId;
}

#ifdef ARDUINO
int8_t ESP32Synth::setupMP3Stream(uint16_t voice, fs::FS &fs, const char* path, uint32_t rootFreqCentiHz, bool loop) {
#else
int8_t ESP32Synth::setupMP3Stream(uint16_t voice, const char* path, uint32_t rootFreqCentiHz, bool loop) {
#endif
    if (voice >= MAX_VOICES) return -1;

    if (streamTaskHandle == NULL) {
        xTaskCreatePinnedToCore(streamLoaderTask, "SynthStream", 8192, this, 1, &streamTaskHandle, SYNTH_STREAM_TASK_CORE);
    }

    if (voices[voice].streamTrackId >= 0) stopStream(voice);

    int8_t streamId = -1;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!streams[i].active) { streamId = i; break; }
    }
    if (streamId == -1) return -1;

    SYNTH_FILE file = SYNTH_STREAM_OPEN();
    if (!SYNTH_FILE_VALID(file)) return -1;

    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        SYNTH_FILE_CLOSE(file);
        return -1;
    }

    uint8_t* inBuf = (uint8_t*)heap_caps_malloc(SYNTH_MP3_IN_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!inBuf) {
        MP3FreeDecoder(decoder);
        SYNTH_FILE_CLOSE(file);
        return -1;
    }

    uint32_t dataStart = 0;
    uint8_t id3Head[10];
    if (SYNTH_FILE_READ(file, id3Head, 10) == 10) {
        if (id3Head[0] == 'I' && id3Head[1] == 'D' && id3Head[2] == '3') {
            uint32_t tagSize = 10 + (((id3Head[6] & 0x7F) << 21) | ((id3Head[7] & 0x7F) << 14) | ((id3Head[8] & 0x7F) << 7) | (id3Head[9] & 0x7F));
            if (id3Head[5] & 0x10) tagSize += 10;
            dataStart = tagSize;
            SYNTH_FILE_SEEK(file, dataStart);
        } else {
            SYNTH_FILE_SEEK(file, 0);
        }
    } else {
        SYNTH_FILE_SEEK(file, 0);
    }

    uint32_t totalFileSize = SYNTH_FILE_SIZE(file);
    uint32_t audioSize = (totalFileSize > dataStart) ? (totalFileSize - dataStart) : totalFileSize;

    size_t bytesRead = SYNTH_FILE_READ(file, inBuf, SYNTH_MP3_IN_BUF_SIZE);
    if (bytesRead < 4) {
        heap_caps_free(inBuf);
        MP3FreeDecoder(decoder);
        SYNTH_FILE_CLOSE(file);
        return -1;
    }

    int syncOffset = MP3FindSyncWord(inBuf, (int)bytesRead);
    if (syncOffset < 0) {
        heap_caps_free(inBuf);
        MP3FreeDecoder(decoder);
        SYNTH_FILE_CLOSE(file);
        return -1;
    }

    MP3FrameInfo frameInfo;
    uint32_t sRate = 44100;
    uint16_t chans = 2;
    uint32_t bitrate = 128000;
    uint32_t durMs = 0;

    if (MP3GetNextFrameInfo(decoder, &frameInfo, inBuf + syncOffset) == ERR_MP3_NONE && frameInfo.samprate > 0) {
        sRate   = frameInfo.samprate;
        chans   = (uint16_t)frameInfo.nChans;
        bitrate = frameInfo.bitrate;

        const uint8_t* fh = inBuf + syncOffset;
        bool isMpeg1 = (((fh[1] >> 3) & 3) == 3);
        uint32_t samplesPerFrame = isMpeg1 ? 1152 : 576;

        int scanLimit = syncOffset + 48;
        if (scanLimit > (int)bytesRead - 16) scanLimit = (int)bytesRead - 16;

        for (int p = syncOffset + 13; p <= scanLimit; p++) {
            const uint8_t* xp = inBuf + p;
            if ((xp[0] == 'X' && xp[1] == 'i' && xp[2] == 'n' && xp[3] == 'g') ||
                (xp[0] == 'I' && xp[1] == 'n' && xp[2] == 'f' && xp[3] == 'o')) {
                uint32_t flags = ((uint32_t)xp[4] << 24) | ((uint32_t)xp[5] << 16) | ((uint32_t)xp[6] << 8) | xp[7];
                int offset = 8;
                uint32_t frames = 0, bytes = 0;
                if (flags & 0x01) {
                    frames = ((uint32_t)xp[offset] << 24) | ((uint32_t)xp[offset + 1] << 16) | ((uint32_t)xp[offset + 2] << 8) | xp[offset + 3];
                    offset += 4;
                }
                if (flags & 0x02) {
                    bytes = ((uint32_t)xp[offset] << 24) | ((uint32_t)xp[offset + 1] << 16) | ((uint32_t)xp[offset + 2] << 8) | xp[offset + 3];
                }
                if (frames > 0 && sRate > 0) {
                    uint64_t totalSmp = (uint64_t)frames * samplesPerFrame;
                    durMs = (uint32_t)((totalSmp * 1000ULL) / sRate);
                    uint32_t bCount = (bytes > 0) ? bytes : audioSize;
                    if (durMs > 0) bitrate = (uint32_t)(((uint64_t)bCount * 8000ULL) / durMs);
                }
                break;
            } else if (xp[0] == 'V' && xp[1] == 'B' && xp[2] == 'R' && xp[3] == 'I') {
                uint32_t bytes  = ((uint32_t)xp[10] << 24) | ((uint32_t)xp[11] << 16) | ((uint32_t)xp[12] << 8) | xp[13];
                uint32_t frames = ((uint32_t)xp[14] << 24) | ((uint32_t)xp[15] << 16) | ((uint32_t)xp[16] << 8) | xp[17];
                if (frames > 0 && sRate > 0) {
                    uint64_t totalSmp = (uint64_t)frames * samplesPerFrame;
                    durMs = (uint32_t)((totalSmp * 1000ULL) / sRate);
                    uint32_t bCount = (bytes > 0) ? bytes : audioSize;
                    if (durMs > 0) bitrate = (uint32_t)(((uint64_t)bCount * 8000ULL) / durMs);
                }
                break;
            }
        }

        if (durMs == 0 && bitrate <= 64000) {
            int probePos = syncOffset + 4;
            int probeSync = MP3FindSyncWord(inBuf + probePos, (int)bytesRead - probePos);
            if (probeSync >= 0) {
                MP3FrameInfo nextInfo;
                if (MP3GetNextFrameInfo(decoder, &nextInfo, inBuf + probePos + probeSync) == ERR_MP3_NONE) {
                    if (nextInfo.bitrate > bitrate) bitrate = nextInfo.bitrate;
                }
            }
        }
    }

    if (durMs == 0) {
        if (bitrate <= 32000) bitrate = 128000;
        durMs = (bitrate > 0) ? (uint32_t)(((uint64_t)audioSize * 8000ULL) / bitrate) : 0;
    }

    StreamTrack* trk = &streams[streamId];
    memset(trk, 0, sizeof(StreamTrack));

    trk->file           = file;
    trk->sampleRate     = sRate;
    trk->dataStartPos   = dataStart;
    trk->dataSize       = audioSize;
    trk->durationMs     = durMs;
    trk->numChannels    = chans;
    trk->bitsPerSample  = 16;
    trk->loop           = loop;
    trk->seekTarget     = -1;
    trk->rootFreqCentiHz = (rootFreqCentiHz > 0) ? rootFreqCentiHz : 0;
    trk->active         = true;

    trk->isMP3          = true;
    trk->mp3Decoder     = (void*)decoder;
    trk->mp3Data        = nullptr;
    trk->mp3InBuf       = inBuf;
    trk->mp3BytesLeft   = (int)bytesRead;
    trk->mp3ReadOffset  = 0;
    trk->mp3Bitrate     = bitrate;

    Voice* vo           = &voices[voice];
    vo->type            = WAVE_STREAM;
    vo->streamTrackId   = streamId;
    vo->active          = false;
    vo->envState        = ENV_IDLE;
    vo->currEnvVal      = 0;

    return streamId;
}

#ifdef ARDUINO
int8_t ESP32Synth::playMP3Stream(uint16_t voice, fs::FS &fs, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
#else
int8_t ESP32Synth::playMP3Stream(uint16_t voice, const char* path, uint16_t volume, uint32_t rootFreqCentiHz, bool loop) {
#endif
    if (voice >= MAX_VOICES) return -1;
#ifdef ARDUINO
    int8_t streamId = setupMP3Stream(voice, fs, path, rootFreqCentiHz, loop);
#else
    int8_t streamId = setupMP3Stream(voice, path, rootFreqCentiHz, loop);
#endif
    if (streamId < 0) return -1;

    StreamTrack* trk = &streams[streamId];
    trk->playing     = true;

    int timeout = 100;
    while (trk->head == trk->tail && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    }

    Voice* vo   = &voices[voice];
    vo->vol     = volume << _volShift;
    vo->freqVal = (rootFreqCentiHz > 0) ? rootFreqCentiHz : 4800000;

    if (trk->rootFreqCentiHz > 0) {
        uint64_t ratio1616 = ((uint64_t)vo->freqVal << 16) / trk->rootFreqCentiHz;
        vo->sampleInc1616  = (uint32_t)((ratio1616 * trk->sampleRate) / _sampleRate);
    } else {
        vo->sampleInc1616  = (uint32_t)(((uint64_t)trk->sampleRate << 16) / _sampleRate);
    }

    if (vo->rateAttack >= ENV_MAX) {
        vo->currEnvVal = ENV_MAX;
        vo->envState   = ENV_DECAY;
    } else {
        vo->currEnvVal = 0;
        vo->envState   = ENV_ATTACK;
    }
    vo->active = true;

    return streamId;
}
#endif

uint32_t ESP32Synth::getStreamSampleRate(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].sampleRate;
    }
    return 0;
}

uint16_t ESP32Synth::getStreamChannels(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].numChannels;
    }
    return 0;
}

uint16_t ESP32Synth::getStreamBitsPerSample(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        return streams[voices[voice].streamTrackId].bitsPerSample;
    }
    return 0;
}

uint32_t ESP32Synth::getStreamBitrate(uint16_t voice, bool snapToStandard) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        #if ENABLE_MP3
        StreamTrack* trk = &streams[voices[voice].streamTrackId];
        if (trk->isMP3) {
            uint32_t rate = trk->mp3Bitrate;
            if (snapToStandard && rate > 0) {
                static const uint16_t stdKbps[] = {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
                uint32_t kbps = (rate + 500) / 1000;
                uint16_t best = stdKbps[0];
                int32_t bestDiff = abs((int32_t)best - (int32_t)kbps);
                for (int i = 1; i < 14; i++) {
                    int32_t diff = abs((int32_t)stdKbps[i] - (int32_t)kbps);
                    if (diff < bestDiff) {
                        bestDiff = diff;
                        best = stdKbps[i];
                    }
                }
                return (uint32_t)best * 1000;
            }
            return rate;
        }
        #endif
    }
    return 0;
}

bool ESP32Synth::isStreamVBR(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        #if ENABLE_MP3
        return streams[voices[voice].streamTrackId].isVBR;
        #endif
    }
    return false;
}

bool ESP32Synth::isStreamMP3(uint16_t voice) {
    if (voice < MAX_VOICES && voices[voice].streamTrackId >= 0) {
        #if ENABLE_MP3
        return streams[voices[voice].streamTrackId].isMP3;
        #endif
    }
    return false;
}