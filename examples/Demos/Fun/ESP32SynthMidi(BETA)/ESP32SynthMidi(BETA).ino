/**
 * @file ESP32SynthMidi.ino
 * @author Danilo Gabriel
 * @brief A polyphonic MIDI synthesizer controlled via Serial commands. (BETA)
 *
 * This example turns the ESP32 into a multi-voice synthesizer that responds to
 * simple text commands sent over the Serial port. It's a template for building
 * a more complete MIDI-controlled instrument.
 *
 * --- Serial Command Format ---
 * Commands are single letters followed by parameters, terminated by a newline.
 * - Note On:  N<note> <velocity>  (e.g., "N60 127")
 * - Note Off: F<note>             (e.g., "F60")
 * - Envelope: E<a> <d> <s> <r>     (e.g., "E10 100 200 300")
 * - Vibrato:  V<rate> <depth>     (e.g., "V500 2500")
 * - Tremolo:  T<rate> <depth>     (e.g., "T200 128")
 * - Glide:    P<time_ms>          (e.g., "P50")
 * - Waveform: W<wave_id>          (e.g., "W-3" for SAW)
 * - Volume:   M<master_vol>       (e.g., "M200")
 */

#include <Arduino.h>
#include "ESP32Synth.h"

// --- CONFIGURATION ---
// Set the number of simultaneous voices.
// IMPORTANT: This MUST be less than or equal to MAX_VOICES in ESP32Synth.h (default 6)
#define POLYPHONY 6

#define BAUD_RATE 115200 // Use a standard, reliable baud rate

// --- CHOOSE YOUR OUTPUT MODE ---
#define USE_I2S 1 // 1 for I2S DAC, 0 for internal DAC (classic ESP32 only)

#if USE_I2S
  const int BCK_PIN  = 26;
  const int WS_PIN   = 25;
  const int DATA_PIN = 22;
#else
  const int DAC_PIN = 25;
#endif

ESP32Synth synth;

int voiceNotes[POLYPHONY];     // Stores the MIDI note number for each voice
uint32_t glideTimeMs = 0;      // Glide/Portamento time in ms
uint32_t lastFreqCentiHz = 0;  // Last frequency played (for glide)

char serialBuf[64];
uint8_t bufPos = 0;

/**
 * @brief Converts a MIDI note number to frequency in centiHertz.
 * @param midiNote The MIDI note number (0-127).
 * @return The frequency in centiHertz (Hz * 100).
 */
uint32_t midiToFreq(uint8_t midiNote) {
  // Using the formula: freq = 440 * 2^((note - 69) / 12)
  double freq = 440.0 * pow(2.0, (midiNote - 69.0) / 12.0);
  return (uint32_t)(freq * 100.0);
}

void setup() {
    Serial.begin(BAUD_RATE);
    
    // --- Initialize Synth ---
    bool success;
    #if USE_I2S
      Serial.println("Initializing I2S output...");
      // Using 32-bit I2S for higher quality, as the original code intended
      success = synth.begin(BCK_PIN, WS_PIN, DATA_PIN, I2S_32BIT);
    #else
      Serial.println("Initializing DAC output...");
      #if !defined(CONFIG_IDF_TARGET_ESP32)
        Serial.println("!!! ERROR: Internal DAC is only available on original ESP32 chips.");
        while(1) delay(1000);
      #endif
      success = synth.begin(DAC_PIN);
    #endif

    if (!success) {
      Serial.println("!!! ERROR: Failed to initialize synthesizer.");
      while(1);
    }

    synth.setMasterVolume(200);
    synth.setControlRateHz(500); // Higher control rate for more responsive slides/LFOs

    // Initialize all voices
    for (int i = 0; i < POLYPHONY; i++) {
        voiceNotes[i] = -1; // -1 means the voice is free
        synth.setWave(i, WAVE_SINE);
        synth.setEnv(i, 10, 100, 200, 300); // Default ADSR
    }
    Serial.println("MIDI Synthesizer Ready. Waiting for commands...");
}

// Finds a free voice or "steals" the oldest one.
int getFreeVoice() {
    for (int i = 0; i < POLYPHONY; i++) {
        if (!synth.isVoiceActive(i)) {
            return i;
        }
    }
    // If no voice is free, steal one in a round-robin fashion.
    // A better implementation might find the quietest or longest-held note.
    static int stealIdx = 0;
    stealIdx = (stealIdx + 1) % POLYPHONY;
    return stealIdx;
}

void handleNoteOn(int note, int vel) {
    if (vel == 0) { // MIDI Note On with velocity 0 is a Note Off
      handleNoteOff(note);
      return;
    }
    
    int voice = getFreeVoice();
    voiceNotes[voice] = note;
    uint32_t targetFreq = midiToFreq(note);

    if (glideTimeMs > 0 && lastFreqCentiHz > 0) {
        // Correct sequence for glide: set up slide, THEN start the note.
        synth.slideFreq(voice, lastFreqCentiHz, targetFreq, glideTimeMs);
        synth.noteOn(voice, lastFreqCentiHz, vel);
    } else {
        synth.noteOn(voice, targetFreq, vel);
    }
    
    lastFreqCentiHz = targetFreq;
}

void handleNoteOff(int note) {
    for (int i = 0; i < POLYPHONY; i++) {
        if (voiceNotes[i] == note) {
            synth.noteOff(i);
            voiceNotes[i] = -1; // Mark voice as free
        }
    }
}

// Custom lightweight atoi to parse commands faster.
int fastAtoi(char** str) {
    while (**str == ' ') (*str)++; 
    int res = 0;
    bool neg = false;
    if (**str == '-') { neg = true; (*str)++; }
    while (**str >= '0' && **str <= '9') {
        res = res * 10 + (**str - '0');
        (*str)++;
    }
    return neg ? -res : res;
}

void parseCommand(char* cmd) {
    if (strlen(cmd) == 0) return;
    char type = cmd[0];
    char* ptr = cmd + 1; 
    
    if (type == 'N') { // Note On: N<note> <velocity>
        int note = fastAtoi(&ptr);
        int vel = fastAtoi(&ptr);
        handleNoteOn(note, vel);
    } 
    else if (type == 'F') { // Note Off: F<note>
        int note = fastAtoi(&ptr);
        handleNoteOff(note);
    } 
    else if (type == 'E') { // Envelope: E<a> <d> <s> <r>
        int a = fastAtoi(&ptr);
        int d = fastAtoi(&ptr);
        int s = fastAtoi(&ptr);
        int r = fastAtoi(&ptr);
        for (int i = 0; i < POLYPHONY; i++) synth.setEnv(i, a, d, s, r);
    } 
    else if (type == 'V') { // Vibrato: V<rate> <depth>
        int rate = fastAtoi(&ptr);
        int depth = fastAtoi(&ptr);
        for (int i = 0; i < POLYPHONY; i++) synth.setVibrato(i, rate, depth);
    } 
    else if (type == 'T') { // Tremolo: T<rate> <depth>
        int rate = fastAtoi(&ptr);
        int depth = fastAtoi(&ptr);
        for (int i = 0; i < POLYPHONY; i++) synth.setTremolo(i, rate, depth);
    } 
    else if (type == 'P') { // Glide (Portamento): P<time_ms>
        glideTimeMs = fastAtoi(&ptr);
    } 
    else if (type == 'W') { // Waveform: W<wave_id>
        int wave = fastAtoi(&ptr);
        for (int i = 0; i < POLYPHONY; i++) synth.setWave(i, (WaveType)wave);
    }
    else if (type == 'M') { // Master Volume: M<vol>
        int vol = fastAtoi(&ptr);
        synth.setMasterVolume(vol);
    }
}

void loop() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') {
            serialBuf[bufPos] = '\0'; 
            parseCommand(serialBuf);
            bufPos = 0;
        } else if (c != '\r' && bufPos < sizeof(serialBuf) - 1) {
            serialBuf[bufPos++] = c;
        }
    }
}
