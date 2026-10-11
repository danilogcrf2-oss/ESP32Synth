/**
 * @file EffectsDemo.ino
 * @author Danilo Gabriel
 * @brief Demonstrates slide, vibrato, and tremolo functions of ESP32Synth.
 * 
 * This example showcases various ways to use the synthesizer's built-in
 * effects to add expression and character to sounds.
 *
 * It can use either I2S output or the built-in DAC on classic ESP32s.
 * 
 * --- Configuration ---
 * To use I2S (recommended for better quality), define USE_I2S as 1
 * and set your I2S pins.
 * 
 * To use the internal DAC (ESP32 only), define USE_I2S as 0 and set your DAC pin.
 */

#include <ESP32Synth.h>

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

// =================================================================================
// SETUP
// =================================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32Synth Effects Demo");
  Serial.printf("Using ESP32 core %s\n", synth.getChipModel());

  // --- Initialize the synthesizer ---
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
  Serial.println("Synth initialized!");

  // --- Configure a voice for the demo ---
  // We'll use voice 0 for all our tests.
  uint8_t voice = 0;
  synth.setEnv(voice, 50, 200, 200, 300); // A, D, S, R
  synth.setWave(voice, WAVE_PULSE);
  synth.setPulseWidth(voice, 192); // 75% duty cycle
}

// =================================================================================
// MAIN LOOP
// =================================================================================
void loop() {
  Serial.println("\n--- Starting Effects Demonstration Cycle ---");

  // 1. TREMOLO DEMO
  demonstrateTremolo();
  delay(1000);

  // 2. VIBRATO DEMO
  demonstrateVibrato();
  delay(1000);

  // 3. FREQUENCY SLIDE DEMO
  demonstrateFreqSlide();
  delay(1000);

  // 4. VOLUME SLIDE DEMO
  demonstrateVolumeSlide();
  delay(1000);

  // 5. COMBINED EFFECTS DEMO
  demonstrateCombined();
  delay(3000); // Wait longer before the next cycle
}

// =================================================================================
// DEMONSTRATION FUNCTIONS
// =================================================================================

/**
 * @brief Demonstrates the Tremolo effect (volume modulation).
 */
void demonstrateTremolo() {
  uint8_t voice = 0;
  
  Serial.println("\n[1] Tremolo Demo");
  Serial.println("Playing a sustained note (A4)...");
  synth.noteOn(voice, a4, 255);
  delay(1000);

  Serial.println("-> Applying slow, shallow tremolo (2 Hz)");
  // setTremolo(voice, rateCentiHz, depth)
  // rateCentiHz: Speed in 1/100ths of a Hz. 200 = 2 Hz.
  // depth: Amount of volume modulation (0-255 is a good range).
  synth.setTremolo(voice, 200, 128);
  delay(2000);

  Serial.println("-> Increasing tremolo rate (8 Hz)");
  synth.setTremolo(voice, 800, 128);
  delay(2000);

  Serial.println("-> Increasing tremolo depth (220)");
  synth.setTremolo(voice, 800, 220);
  delay(2000);
  
  Serial.println("-> Turning off tremolo");
  synth.setTremolo(voice, 0, 0);
  delay(1000);

  Serial.println("-> Note Off");
  synth.noteOff(voice);
}


/**
 * @brief Demonstrates the Vibrato effect (pitch modulation).
 */
void demonstrateVibrato() {
  uint8_t voice = 0;

  Serial.println("\n[2] Vibrato Demo");
  Serial.println("Playing a sustained note (C5)...");
  synth.noteOn(voice, c5, 255);
  delay(1000);

  Serial.println("-> Applying gentle vibrato (5 Hz, 25Hz deviation)");
  // setVibrato(voice, rateCentiHz, depthCentiHz)
  // rateCentiHz: Speed of the effect in 1/100ths of a Hz. 500 = 5 Hz.
  // depthCentiHz: Max pitch deviation from base freq in 1/100ths of a Hz.
  // 2500 = pitch varies by +/- 25 Hz.
  synth.setVibrato(voice, 500, 2500);
  delay(2000);

  Serial.println("-> Increasing vibrato depth (+/- 75 Hz)");
  synth.setVibrato(voice, 500, 7500);
  delay(2000);

  Serial.println("-> Increasing vibrato rate (10 Hz)");
  synth.setVibrato(voice, 1000, 7500);
  delay(2000);

  Serial.println("-> Turning off vibrato");
  synth.setVibrato(voice, 0, 0);
  delay(1000);

  Serial.println("-> Note Off");
  synth.noteOff(voice);
}


/**
 * @brief Demonstrates the Frequency Slide effect (portamento).
 */
void demonstrateFreqSlide() {
  uint8_t voice = 0;
  
  Serial.println("\n[3] Frequency Slide Demo");

  Serial.println("-> Sliding up one octave (C4 to C5) over 800ms");
  // slideFreq() prepares the slide. noteOn() starts it.
  synth.slideFreq(voice, c4, c5, 800);
  synth.noteOn(voice, c4, 255); // The frequency here MUST match the slide's start frequency.
  delay(1000); 
  synth.noteOff(voice);
  delay(1000); 

  Serial.println("-> Playing a high note (G5)...");
  synth.noteOn(voice, g5, 255);
  delay(1000);

  Serial.println("-> Sliding down to G4 over 1500ms");
  // slideFreqTo() slides a note that is already playing.
  synth.slideFreqTo(voice, g4, 1500);
  delay(2000); 
  
  Serial.println("-> Note Off");
  synth.noteOff(voice);
}

/**
 * @brief Demonstrates the Volume Slide effect.
 */
void demonstrateVolumeSlide() {
  uint8_t voice = 0;

  Serial.println("\n[4] Volume Slide Demo");

  Serial.println("-> Sliding volume from 0 to 255 over 1000ms (a 'swell')");
  // slideVol() lets you define start and end volumes.
  synth.slideVol(voice, 0, 255, 1000);
  synth.noteOn(voice, a4, 0); // Note starts at the slide's initial volume.
  delay(1500);
  
  Serial.println("-> Sliding volume down to 50 over 800ms");
  // slideVolTo() slides from the current volume to a new target.
  synth.slideVolTo(voice, 50, 800);
  delay(1200);

  Serial.println("-> Note Off");
  synth.noteOff(voice);
}


/**
 * @brief Demonstrates combining effects.
 */
void demonstrateCombined() {
  uint8_t voice = 0;

  Serial.println("\n[5] Combined Effects Demo");
  Serial.println("Playing a note (E4) with vibrato and tremolo...");
  
  synth.noteOn(voice, e4, 255);
  
  // Apply both effects at once
  synth.setVibrato(voice, 600, 4000);  // 6Hz rate, 40Hz deviation
  synth.setTremolo(voice, 400, 100);   // 4Hz rate, depth 100

  delay(2000);

  Serial.println("-> Sliding frequency up to B4 while effects are active");
  synth.slideFreqTo(voice, b4, 1200);
  delay(1500);

  Serial.println("-> Sliding volume down while still playing");
  synth.slideVolTo(voice, 0, 800);
  delay(1000);

  // Effects are still active, but note is silent.
  // Turn them off and stop the note completely.
  Serial.println("-> Turning off effects and note");
  synth.setVibrato(voice, 0, 0);
  synth.setTremolo(voice, 0, 0);
  synth.noteOff(voice);
}
