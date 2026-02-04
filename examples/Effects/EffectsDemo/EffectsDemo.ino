/**
 * @file EffectsDemo.ino
 * @author Danilo Gabriel
 * @brief Demonstrates slide, vibrato, and tremolo functions of ESP32Synth.
 * 
 * This example showcases various ways to use the synthesizer's built-in
 * effects to add expression and character to sounds.
 * 
 * It uses the I2S output. Make sure you have an I2S DAC connected.
 * - BCK_PIN:  Your I2S bit clock pin
 * - WS_PIN:   Your I2S word select (LRC) pin
 * - DATA_PIN: Your I2S data out pin
 */

#include <ESP32Synth.h>

// --- Pin Configuration for I2S DAC ---
// --- You MUST change these pins to match your setup ---
const int BCK_PIN = 26;
const int WS_PIN = 25;
const int DATA_PIN = 22;

// For ESP32's internal DAC (Note: only available on classic ESP32)
// const int DAC_PIN = 25; // or 26

ESP32Synth synth;

// =================================================================================
// SETUP
// =================================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32Synth Effects Demo");
  Serial.printf("Using ESP32 core %s\n", synth.getChipModel());

  // --- Initialize the synthesizer ---
  // Uncomment the line that matches your hardware setup.

  // Option 1: Using an I2S DAC (Recommended for quality)
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }

  // Option 2: Using the internal DAC (ESP32 classic only)
  // if (!synth.begin(DAC_PIN)) {
  //   Serial.println("!!! ERROR: Failed to initialize synthesizer.");
  //   while (1) delay(1000);
  // }
  
  synth.setMasterVolume(200); // Set a reasonable master volume (0-255)
  Serial.println("Synth initialized!");

  // --- Configure a voice for the demo ---
  // We'll use voice 0 for all our tests.
  uint8_t voice = 0;

  // Set a simple ADSR envelope: quick attack, medium decay, high sustain, medium release
  synth.setEnv(voice, 50, 200, 200, 300);

  // Set the waveform to a pulse wave (good for hearing pitch and volume changes)
  synth.setWave(voice, WAVE_PULSE);

  // Set the pulse width for a classic "chiptune" sound
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

  // 3. SLIDE DEMO
  demonstrateSlide();
  delay(1000);

  // 4. COMBINED EFFECTS DEMO
  demonstrateCombined();
  delay(3000); // Wait longer before the next cycle
}

// =================================================================================
// DEMONSTRATION FUNCTIONS
// =================================================================================

/**
 * @brief Demonstrates the Tremolo effect.
 * Tremolo modulates the volume of a note.
 */
void demonstrateTremolo() {
  uint8_t voice = 0;
  uint8_t volume = 100;
  
  Serial.println("\n[1] Tremolo Demo");
  Serial.println("Playing a sustained note (A4)...");
  synth.noteOn(voice, a4, volume);
  delay(1000);

  // Apply a slow, shallow tremolo
  Serial.println("-> Applying slow, shallow tremolo (2 Hz, Depth 64)");
  // setTremolo(voice, rateCentiHz, depth)
  // rateCentiHz: Speed of the effect in 1/100ths of a Hz. 200 = 2 Hz.
  // depth: Amount of volume modulation (0-255 is a good range, but can be higher).
  synth.setTremolo(voice, 200, 64);
  delay(2000);

  // Make the tremolo faster
  Serial.println("-> Increasing tremolo rate (8 Hz)");
  synth.setTremolo(voice, 800, 64);
  delay(2000);

  // Make the tremolo deeper
  Serial.println("-> Increasing tremolo depth (200)");
  synth.setTremolo(voice, 800, 200);
  delay(2000);
  
  // Turn off tremolo
  Serial.println("-> Turning off tremolo");
  synth.setTremolo(voice, 0, 0);
  delay(1000);

  Serial.println("-> Note Off");
  synth.noteOff(voice);
}


/**
 * @brief Demonstrates the Vibrato effect.
 * Vibrato modulates the pitch (frequency) of a note.
 */
void demonstrateVibrato() {
  uint8_t voice = 0;
  uint8_t volume = 100;

  Serial.println("\n[2] Vibrato Demo");
  Serial.println("Playing a sustained note (C5)...");
  synth.noteOn(voice, c5, volume);
  delay(1000);

  // Apply a classic, gentle vibrato
  Serial.println("-> Applying gentle vibrato (5 Hz, +/- 25 cents)");
  // setVibrato(voice, rateCentiHz, depthCentiHz)
  // rateCentiHz: Speed of the effect in 1/100ths of a Hz. 500 = 5 Hz.
  // depthCentiHz: Pitch variation in 1/100ths of a Hz. 2500 = +/- 25 cents (a quarter semitone)
  synth.setVibrato(voice, 500, 2500); // 5 Hz rate, 25 cent depth
  delay(2000);

  // Make the vibrato wider (more pitch variation)
  Serial.println("-> Increasing vibrato depth (+/- 75 cents)");
  synth.setVibrato(voice, 500, 7500); // 5 Hz rate, 75 cent depth
  delay(2000);

  // Make the vibrato faster
  Serial.println("-> Increasing vibrato rate (10 Hz)");
  synth.setVibrato(voice, 1000, 7500); // 10 Hz rate, 75 cent depth
  delay(2000);

  // Turn off vibrato
  Serial.println("-> Turning off vibrato");
  synth.setVibrato(voice, 0, 0);
  delay(1000);

  Serial.println("-> Note Off");
  synth.noteOff(voice);
}


/**
 * @brief Demonstrates the Slide effect.
 * Slide creates a smooth transition from one pitch to another (portamento).
 */
void demonstrateSlide() {
  uint8_t voice = 0;
  uint8_t volume = 100;

  Serial.println("\n[3] Slide Demo");

  // --- Using slide() ---
  // slide() only prepares the slide parameters. noteOn() must be called to start the sound.
  Serial.println("-> Sliding up one octave (C4 to C5) over 800ms");
  synth.slide(voice, c4, c5, 800);
  synth.noteOn(voice, c4, volume); // This triggers the voice and starts the slide.
  delay(1000); // Let the note sustain a bit after the slide
  synth.noteOff(voice);
  delay(1000); // Wait for the release to finish

  // --- Using slideTo() ---
  // slideTo() is used to slide a note that is already playing.
  Serial.println("-> Playing a high note (G5)...");
  synth.noteOn(voice, g5, volume);
  delay(1000);

  Serial.println("-> Sliding down to G4 over 1500ms");
  // slideTo(voice, endFreq, durationMs)
  synth.slideTo(voice, g4, 1500);
  delay(2000); // Wait for the slide to complete and hold the note
  
  Serial.println("-> Note Off");
  synth.noteOff(voice);
}

/**
 * @brief Demonstrates combining effects.
 * A note can have vibrato and tremolo, and can also be slid.
 */
void demonstrateCombined() {
  uint8_t voice = 0;
  uint8_t volume = 100;

  Serial.println("\n[4] Combined Effects Demo");
  Serial.println("Playing a note (E4) with vibrato and tremolo...");
  
  synth.noteOn(voice, e4, volume);
  
  // Apply both effects at once
  synth.setVibrato(voice, 600, 4000);  // 6Hz rate, 40 cent depth
  synth.setTremolo(voice, 400, 100);   // 4Hz rate, depth 100

  delay(2000);

  // Now, slide the note up to B4 while the effects are still active
  Serial.println("-> Sliding up to B4 while effects are active");
  synth.slideTo(voice, b4, 1200);
  delay(2000);

  // Turn off the effects
  Serial.println("-> Turning off effects");
  synth.setVibrato(voice, 0, 0);
  synth.setTremolo(voice, 0, 0);
  delay(1500);

  Serial.println("-> Note Off");
  synth.noteOff(voice);
}
