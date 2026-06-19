/**
 * @file BasicWaveforms.ino
 * @author Danilo Gabriel
 * @brief Demonstrates the basic waveforms of the ESP32Synth library.
 * 
 * This example plays a C major scale, cycling through the five fundamental
 * waveforms: Sine, Saw, Pulse, Triangle, and Noise.
 * 
 * It can use either I2S output or the built-in DAC on classic ESP32s.
 * 
 * --- Configuration ---
 * To use I2S (recommended for better quality), define USE_I2S as 1
 * and set your I2S pins.
 * 
 * To use the internal DAC (ESP32 only), define USE_I2S as 0 and set your DAC pin.
 */

#include <Arduino.h>
#include <ESP32Synth.h> // Includes ESP32SynthNotes.h automatically

// --- CHOOSE YOUR OUTPUT MODE ---
#define USE_I2S 1 // 1 for I2S DAC, 0 for internal DAC (classic ESP32 only)

#if USE_I2S
  // --- Pin Configuration for I2S DAC ---
  // --- You MUST change these pins to match your setup ---
  const int BCK_PIN  = 26; // Bit Clock
  const int WS_PIN   = 25; // Word Select (LRC)
  const int DATA_PIN = 22; // Data Out
#else
  // --- Pin Configuration for Internal DAC ---
  // --- Must be GPIO 25 or 26 on ESP32 ---
  const int DAC_PIN = 25;
#endif

ESP32Synth synth;

// C Major Scale in CentiHertz (Hz * 100) from ESP32SynthNotes.h
uint32_t scale[] = {
  c4, d4, e4, f4, g4, a4, b4, c5
};

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32Synth Basic Waveforms Example");
  Serial.printf("Using ESP32 core %s\n", synth.getChipModel());

  bool success;
  #if USE_I2S
    Serial.println("Initializing I2S output...");
    success = synth.begin(BCK_PIN, WS_PIN, DATA_PIN);
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
    while (1) delay(1000);
  }
  
  synth.setMasterVolume(200); // Set a reasonable master volume (0-255)
  Serial.println("Synth initialized successfully!");
}

void loop() {
  WaveType waves[] = { WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_PULSE, WAVE_NOISE };
  const char* waveNames[] = { "SINE", "TRIANGLE", "SAW", "PULSE", "NOISE" };
  
  for (int w = 0; w < 5; w++) {
    WaveType currentWave = waves[w];
    Serial.printf("\n--- Playing C Major Scale with %s wave ---\n", waveNames[w]);

    // Set up a single voice (voice 0)
    uint8_t voice = 0;
    synth.setWave(voice, currentWave);
    synth.setEnv(voice, 10, 200, 100, 300); // Attack(ms), Decay(ms), Sustain(0-255), Release(ms)

    // For pulse waves, set a pulse width (e.g., 50% duty cycle)
    if (currentWave == WAVE_PULSE) {
      synth.setPulseWidth(voice, 128); // 0-255, where 128 is ~50%
    }

    // Play the scale
    for (int i = 0; i < 8; i++) {
      Serial.printf("Note: %d, Freq: %.2f Hz\n", i, scale[i] / 100.0f);
      
      // For noise, the frequency parameter changes its "character" or "timbre"
      // rather than a specific pitch. We'll use a fixed value.
      uint32_t freqToPlay = (currentWave == WAVE_NOISE) ? 20000 : scale[i];

      synth.noteOn(voice, freqToPlay, 255); // Play note with full volume
      delay(350);
      synth.noteOff(voice);
      delay(150); // Allow time for release envelope
    }
    
    delay(1000); // Wait a second before the next waveform
  }
}
