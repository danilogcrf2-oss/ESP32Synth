#pragma once

uint16_t ESP32Synth::getMasterVolume() {
    return _masterVolume >> _volShift; // Retorna na exata resolução que o usuário configurou
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
    return (voice < MAX_VOICES) ? (uint8_t)(voices[voice].currEnvVal >> 20) : 0;
}

uint8_t ESP32Synth::getOutput8Bit(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    // Otimizado: Combina Env de 8-bits com Vol realocado para 8-bits
    return (uint8_t)(((voices[voice].currEnvVal >> 20) * (voices[voice].vol >> 8)) >> 8);
}

uint32_t ESP32Synth::getVolumeRaw(uint16_t voice) {
    return (voice < MAX_VOICES) ? (uint32_t)voices[voice].vol : 0;
}

uint32_t ESP32Synth::getEnvRaw(uint16_t voice) {
    return (voice < MAX_VOICES) ? voices[voice].currEnvVal : 0;
}

uint32_t ESP32Synth::getOutputRaw(uint16_t voice) {
    if (voice >= MAX_VOICES) return 0;
    // 64-bits previne overflow na multiplicação do envelope máximo (32b) com volume interno (16b)
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