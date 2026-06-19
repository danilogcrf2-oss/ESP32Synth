/**
 * @file InstrumentSequence.ino
 * @author Danilo Gabriel
 * @brief Demonstrates the 'Instrument' feature for tracker-like effects.
 * 
 * This example shows how to use the 'Instrument' struct to create a sound
 * that evolves over time, similar to instruments in old-school trackers.
 * 
 * The instrument defines a sequence of volumes and wavetable IDs that are
 * automatically played when a note is held. This one creates a simple
 * "plucked" sound that fades out.
 * 
 * It uses the I2S output. Make sure you have an I2S DAC connected.
 * - BCK_PIN:  Your I2S bit clock pin
 * - WS_PIN:   Your I2S word select (LRC) pin
 * - DATA_PIN: Your I2S data out pin
 */

#include <Arduino.h>
#include <ESP32Synth.h>
#include "Wavetables.h" // Include custom wavetable data

// --- Pin Configuration for I2S DAC ---
// --- You MUST change these pins to match your setup ---
const int BCK_PIN = 26;
const int WS_PIN = 25;
const int DATA_PIN = 22;

ESP32Synth synth;

// --- Define the Instrument Sequence ---

// Volume envelope for the attack/sustain part of the sound.
// This creates a fast fade-out effect.
const uint8_t attackVolumes[] = { 127, 100, 80, 60, 40, 20, 10, 0 };

// We will use the same simple sine-like wavetable for the whole sound.
// Using W_SINE would also work. Here we use a custom one for demonstration.
const int16_t attackWaves[] = { 1, 1, 1, 1, 1, 1, 1, 1 };

// Define the main instrument structure
Instrument pluckyInstrument = {
  .seqVolumes    = attackVolumes,     // Pointer to the volume sequence
  .seqWaves      = attackWaves,       // Pointer to the wavetable ID sequence
  .seqLen        = 8,                 // Number of steps in the sequence
  .seqSpeedMs    = 30,                // Time in ms for each step
  
  .susVol        = 0,                 // Sustain volume (0, since it fades out)
  .susWave       = W_SINE,            // Wavetable to hold on if sustain > 0
  
  .relVolumes    = nullptr,           // No separate release sequence
  .relWaves      = nullptr,
  .relLen        = 0,
  .relSpeedMs    = 0,

  .smoothMorph   = false              // Don't morph between wavetable steps
};


void setup() {
  Serial.begin(115200);
  Serial.println("ESP32Synth Instrument Sequence Example");

  // --- Initialize Synth ---
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }
  
  synth.setMasterVolume(200);

  // --- Register Wavetables ---
  // We need to register the wavetable that our instrument uses (ID '1').
  // The data is in Wavetables.h
  synth.registerWavetable(1, sine_wavetable, 256, BITS_8);
  
  Serial.println("Synth initialized and wavetable registered!");
}

void loop() {
  uint8_t voice = 0;
  
  // Attach the instrument to voice 0
  synth.setInstrument(voice, &pluckyInstrument);
  
  Serial.println("\nPlaying C4 with the custom instrument.");
  synth.noteOn(voice, c4, 127); // Volume here is max, the instrument handles the rest
  
  // The instrument has a total duration of 8 steps * 30ms/step = 240ms.
  // We'll hold the note longer to show it has faded out and stopped.
  delay(500); 

  // The note technically ends when the sequence is over, but we call noteOff
  // to ensure the voice is freed correctly.
  synth.noteOff(voice);
  
  delay(1000); // Wait before repeating

  // Now play a higher note
  Serial.println("Playing G4 with the custom instrument.");
  synth.noteOn(voice, g4, 127);
  delay(500);
  synth.noteOff(voice);
  
  delay(2000); // Longer pause
}
