/**
 * @file WavetableSynth.ino
 * @author Danilo Gabriel
 * @brief Demonstrates using multiple custom wavetables.
 * 
 * This example defines two different 8-bit wavetables:
 * 1. A "buzzy" wave made from only a few sine harmonics.
 * 2. A "hollow" square-like wave.
 * 
 * It registers them with IDs 10 and 11, then plays a simple melody,
 * alternating between the two sounds.
 * 
 * It uses the I2S output. Make sure you have an I2S DAC connected.
 * - BCK_PIN:  Your I2S bit clock pin
 * - WS_PIN:   Your I2S word select (LRC) pin
 * - DATA_PIN: Your I2S data out pin
 */

#include <Arduino.h>
#include <ESP32Synth.h>
#include "WavetableData.h" // Our custom wavetables are here

// --- Pin Configuration for I2S DAC ---
// --- You MUST change these pins to match your setup ---
const int BCK_PIN = 26;
const int WS_PIN = 25;
const int DATA_PIN = 22;

ESP32Synth synth;

// A simple melody using notes from the C minor scale
uint32_t melody[] = { c4, ds4, g4, c5, g4, f4, ds4, c4 };
// Which wavetable to use for each note (10 or 11)
uint16_t wave_for_note[] = { 10, 10, 10, 11, 11, 10, 11, 10 };

// Define the wavetable IDs
const uint16_t BUZZY_WAVE_ID = 10;
const uint16_t HOLLOW_WAVE_ID = 11;

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32Synth Wavetable Example");

  // --- Initialize Synth ---
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }
  
  synth.setMasterVolume(200);

  // --- Register Wavetables ---
  // The data for these tables is in WavetableData.h
  synth.registerWavetable(BUZZY_WAVE_ID, buzzy_wavetable, 256, BITS_8);
  synth.registerWavetable(HOLLOW_WAVE_ID, hollow_wavetable, 256, BITS_8);
  
  Serial.println("Synth initialized and custom wavetables registered!");
}

void loop() {
  Serial.println("\nPlaying melody with custom wavetables...");

  uint8_t voice = 0;
  // Set a generic envelope for the voice
  synth.setEnv(voice, 20, 150, 110, 250);

  // Play the melody
  for (int i = 0; i < 8; i++) {
    uint16_t current_wave_id = wave_for_note[i];
    
    // Set the voice to use the desired wavetable
    // This is done by first setting the wave type, then the specific data.
    // The synth remembers the last data set for WAVE_WAVETABLE type.
    // A better way is to use Instruments, but this shows the basic principle.
    synth.setWave(voice, WAVE_WAVETABLE);
    if(current_wave_id == BUZZY_WAVE_ID) {
        synth.setWavetable(voice, buzzy_wavetable, 256, BITS_8);
        Serial.printf("Note %d: Freq %.2f Hz, Wave: Buzzy\n", i, (float)melody[i] / 100.0f);
    } else {
        synth.setWavetable(voice, hollow_wavetable, 256, BITS_8);
        Serial.printf("Note %d: Freq %.2f Hz, Wave: Hollow\n", i, (float)melody[i] / 100.0f);
    }

    synth.noteOn(voice, melody[i], 127);
    delay(300);
    synth.noteOff(voice);
    delay(100);
  }
  
  delay(3000); // Wait before repeating
}
