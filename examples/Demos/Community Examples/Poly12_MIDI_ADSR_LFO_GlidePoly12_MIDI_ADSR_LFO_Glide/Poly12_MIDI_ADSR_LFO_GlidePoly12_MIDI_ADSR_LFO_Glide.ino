#include <ESP32Synth.h>

/*
  ESP32Synth - Community Example
  
  Created/Contributed by: fjtrsq
  Source Fork: https://github.com/fjtrsq
  Description: 12-voice polyphonic MIDI synth

*/

ESP32Synth synth;

static constexpr uint8_t VOICE_COUNT = 12;
static constexpr uint8_t MIDI_CHANNEL = 1; // 1..16

struct VoiceSlot {
  bool active = false;
  uint8_t note = 0;
  uint32_t freq = 0;
  uint32_t startedAt = 0;
};

VoiceSlot voiceSlots[VOICE_COUNT];

// Global synth params controlled by MIDI CC
WaveType selectedWave = WAVE_SAW; // CC 20: 0-63 SAW, 64-127 PULSE
uint16_t attackMs = 12;
uint16_t decayMs = 120;
uint8_t sustain = 170;
uint16_t releaseMs = 220;
uint32_t glideMs = 0;
uint32_t lfoRateCentiHz = 550;  // 5.50 Hz
uint32_t lfoDepthCentiHz = 12;  // 0.12 Hz (very subtle)
uint16_t noteVolume = 255;

inline uint32_t midiNoteToCentiHz(uint8_t note) {
  const float hz = 440.0f * powf(2.0f, (int(note) - 69) / 12.0f);
  return uint32_t(hz * 100.0f + 0.5f);
}

int8_t findVoiceByNote(uint8_t note) {
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    if (voiceSlots[i].active && voiceSlots[i].note == note) return i;
  }
  return -1;
}

uint8_t allocateVoice() {
  for (uint8_t i = 0; i < VOICE_COUNT; ++i) {
    if (!voiceSlots[i].active) return i;
  }

  // Voice stealing: oldest active voice
  uint8_t oldest = 0;
  uint32_t oldestTime = voiceSlots[0].startedAt;
  for (uint8_t i = 1; i < VOICE_COUNT; ++i) {
    if (voiceSlots[i].startedAt < oldestTime) {
      oldestTime = voiceSlots[i].startedAt;
      oldest = i;
    }
  }
  synth.noteOff(oldest);
  voiceSlots[oldest].active = false;
  return oldest;
}

void applyVoiceParams(uint8_t v) {
  synth.setWave(v, selectedWave);
  synth.setEnv(v, attackMs, decayMs, sustain, releaseMs);
  synth.setVibrato(v, lfoRateCentiHz, lfoDepthCentiHz);
}

void handleNoteOn(uint8_t note, uint8_t velocity) {
  const uint8_t v = allocateVoice();
  const uint32_t targetFreq = midiNoteToCentiHz(note);

  applyVoiceParams(v);

  if (glideMs > 0 && voiceSlots[v].active) {
    synth.slideFreqTo(v, targetFreq, glideMs);
  } else {
    synth.setFrequency(v, targetFreq);
  }

  uint16_t vol = map(velocity, 0, 127, 20, noteVolume);
  synth.noteOn(v, targetFreq, vol);

  voiceSlots[v].active = true;
  voiceSlots[v].note = note;
  voiceSlots[v].freq = targetFreq;
  voiceSlots[v].startedAt = millis();
}

void handleNoteOff(uint8_t note) {
  int8_t v = findVoiceByNote(note);
  if (v < 0) return;
  synth.noteOff(v);
  voiceSlots[v].active = false;
}

void allNotesOff() {
  for (uint8_t v = 0; v < VOICE_COUNT; ++v) {
    synth.noteOff(v);
    voiceSlots[v].active = false;
  }
}

void handleCC(uint8_t cc, uint8_t value) {
  switch (cc) {
    case 1: // Mod wheel -> LFO depth
    case 22:
      lfoDepthCentiHz = map(value, 0, 127, 0, 1800); // up to 18 Hz dev
      break;
    case 5: // Portamento time
      glideMs = map(value, 0, 127, 0, 1200);
      break;
    case 7: // Volume
      noteVolume = map(value, 0, 127, 32, 255);
      break;
    case 20: // Wave selector
      selectedWave = (value < 64) ? WAVE_SAW : WAVE_PULSE;
      break;
    case 21: // LFO rate
      lfoRateCentiHz = map(value, 0, 127, 20, 1400); // 0.20 .. 14.00 Hz
      break;
    case 73: // Attack
      attackMs = map(value, 0, 127, 1, 3000);
      break;
    case 75: // Decay
      decayMs = map(value, 0, 127, 1, 4000);
      break;
    case 70: // Sustain
      sustain = map(value, 0, 127, 0, 255);
      break;
    case 72: // Release
      releaseMs = map(value, 0, 127, 1, 5000);
      break;
    case 123: // All notes off
      allNotesOff();
      break;
    default:
      break;
  }

  for (uint8_t v = 0; v < VOICE_COUNT; ++v) {
    if (voiceSlots[v].active) applyVoiceParams(v);
  }
}

void parseMidiByte(uint8_t b) {
  static uint8_t status = 0;
  static uint8_t data1 = 0;
  static bool waitingData2 = false;

  if (b & 0x80) {
    status = b;
    waitingData2 = false;
    return;
  }

  if (status == 0) return;

  const uint8_t msg = status & 0xF0;
  const uint8_t ch = (status & 0x0F) + 1;
  if (ch != MIDI_CHANNEL) return;

  if (!waitingData2) {
    data1 = b;
    waitingData2 = true;
    return;
  }

  uint8_t data2 = b;
  waitingData2 = false;

  switch (msg) {
    case 0x90:
      if (data2 == 0) handleNoteOff(data1);
      else handleNoteOn(data1, data2);
      break;
    case 0x80:
      handleNoteOff(data1);
      break;
    case 0xB0:
      handleCC(data1, data2);
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(31250);

  // PDM output example (works very well on ESP32-S3). For I2S use synth.begin(bck, ws, data).
  if (!synth.begin(42)) {
    while (true) delay(100);
  }

  synth.setMasterVolume(65280);

  for (uint8_t v = 0; v < VOICE_COUNT; ++v) {
    applyVoiceParams(v);
    synth.setPulseWidth(v, 32768); // 50% pulse when pulse wave selected
  }
}

void loop() {
  while (Serial.available() > 0) {
    parseMidiByte((uint8_t)Serial.read());
  }
}