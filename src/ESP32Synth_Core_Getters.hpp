#pragma once
#include "ESP32Synth.h"

uint16_t ESP32Synth::getMasterVolume() {
    return _masterVolume >> _volShift; // Returns at the exact resolution configured by the user
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
    if (voice >= MAX_VOICES) return 0;
    uint32_t val = voices[voice].currEnvVal >> 20;
    return (val > 255) ? 255 : (uint8_t)val;
}

uint8_t ESP32Synth::getOutput8Bit(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    uint32_t env = voices[voice].currEnvVal >> 20;
    if (env > 255) env = 255;
    uint32_t vol = voices[voice].vol >> 8;
    if (vol > 255) vol = 255;
    return (uint8_t)((env * vol) >> 8);
}

uint32_t ESP32Synth::getVolumeRaw(uint16_t voice) {
    return (voice < MAX_VOICES) ? (uint32_t)voices[voice].vol : 0;
}

uint32_t ESP32Synth::getEnvRaw(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].currEnvVal : 0;
}

uint32_t ESP32Synth::getOutputRaw(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    // 64-bit prevents overflow when multiplying max envelope (32b) by internal volume (16b)
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

#if SYNTH_STEREO
PanLaw ESP32Synth::getPanLaw() {
    return _panLaw;
}

int32_t ESP32Synth::getPan(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    int32_t maxVal = (1L << (_panShift - 1)) - 1;
    int32_t pan16  = voices[voice].pan;
    return (pan16 >= 0) ? ((pan16 * maxVal + 16383) / 32767) : -((-pan16 * maxVal + 16383) / 32767);
}

uint16_t ESP32Synth::getStereoWidth(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].stereoWidth : 256;
}

int32_t ESP32Synth::getMasterPan() {
    int32_t maxVal = (1L << (_panShift - 1)) - 1;
    int32_t pan16  = _masterPan;
    return (pan16 >= 0) ? ((pan16 * maxVal + 16383) / 32767) : -((-pan16 * maxVal + 16383) / 32767);
}

uint16_t ESP32Synth::getMasterStereoWidth() {
    return _masterWidth;
}
#endif