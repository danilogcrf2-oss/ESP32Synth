/**
 * @file Square_pwm.ino
 * @author Danilo Gabriel
 * @brief Demonstrates Pulse-Width Modulation (PWM) on a square wave.
 * 
 * This example plays a constant low note (C2) with a pulse waveform. It then
 * continuously sweeps the pulse width from narrow to wide and back again.
 * This modulation of the pulse width changes the harmonic content of the sound,
 * creating a classic "sweeping" or "phasing" effect.
 * 
 * It uses the default PDM output on pin 5.
 */
#include "ESP32Synth.h"
#include "ESP32SynthNotes.h"

ESP32Synth synth;

// --- PWM Control Variables ---
int currentPW = 128; // Current pulse width (0-255). Starts at 50% (128).
int direction = 1;   // Sweep direction: 1 for increasing width, -1 for decreasing.

const int BCK_PIN = 19;
const int WS_PIN = 5;
const int DATA_PIN = 18;

/**
 * @brief Initializes the synthesizer and sets up the sound.
 */
void setup() {
  Serial.begin(115200);
  delay(1000); 

  // Initialize the synth on the default PDM output (pin 5) and run on core 1.
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)s) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while(1) delay(1000);
  }
  
  // --- Configure Voice 0 for the PWM effect ---
  synth.setWave(0, WAVE_PULSE);       // Set waveform to Pulse.
  synth.setEnv(0, 10, 0, 127, 100);   // Set a basic organ-like envelope (instant attack, full sustain).
  synth.setVolume(0, 200);            // Set a high volume.
  
  // Play a low C2 note to make the PWM effect clearly audible.
  synth.noteOn(0, c2, 200);
  Serial.println("Synth initialized. Playing C2 note with PWM sweep.");
}

/**
 * @brief Main loop that continuously modulates the pulse width.
 */
void loop() {
  // Update the pulse width value.
  currentPW += direction;

  // Reverse the sweep direction when the pulse width reaches its limits.
  // We avoid the absolute extremes (0 and 255) because the sound would disappear.
  if (currentPW >= 254) {
    direction = -1; // Start decreasing.
  } 
  else if (currentPW <= 2) {
    direction = 1;  // Start increasing.
  }

  // Apply the new pulse width to voice 0.
  synth.setPulseWidth(0, currentPW);

  // A small delay controls the speed of the sweep effect.
  // - A smaller delay (e.g., 10) creates a fast, aggressive sweep.
  // - A larger delay (e.g., 30) creates a slow, detailed sweep.
  delay(15); 
}
