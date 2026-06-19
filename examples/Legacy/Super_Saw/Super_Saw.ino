/**
 * @file Super_Saw.ino
 * @author Danilo Gabriel
 * @brief Creates a "Super Saw" sound by layering and detuning sawtooth waves.
 * 
 * This example demonstrates how to create a rich, thick "Super Saw" patch,
 * a sound famous in electronic music. It works by playing the same note across
 * multiple voices, all set to a sawtooth waveform.
 * 
 * The core of the effect comes from slightly detuning each voice away from the
 * fundamental frequency. This creates a chorus-like effect that makes the sound
 * feel wide and powerful.
 * 
 * It uses the default PDM output on pin 5.
 */
#include "ESP32Synth.h"
#include "ESP32SynthNotes.h"

ESP32Synth synth;

const int BCK_PIN = 19;
const int WS_PIN = 5;
const int DATA_PIN = 18;

/**
 * @brief Initializes the synthesizer and configures the Super Saw patch.
 */
void setup() {
    Serial.begin(115200);
    delay(1000); 

    // Initialize the synth on the default PDM output (pin 5) and run on core 1.
    if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
        Serial.println("!!! ERROR: Failed to initialize synthesizer.");
        while(1) delay(1000);
    }
    
    // Configure 7 voices to be part of the Super Saw patch.
    for(int i = 0; i < 7; i++) {
        synth.setWave(i, WAVE_SAW);       // Set all voices to sawtooth.
        synth.setEnv(i, 50, 0, 127, 500); // Give them a slightly soft attack and long release.
    }
    Serial.println("Synth initialized. Playing Super Saw chord progression.");
}

/**
 * @brief Main loop that plays a simple chord progression.
 */
void loop() {
  playSaw(c2);
  delay(2000);
  playSaw(e2);
  delay(2000);
  playSaw(g2);
  delay(2000);
  playSaw(a2);
  delay(2000);
}

/**
 * @brief Plays a note with the Super Saw effect.
 * @param baseNote The fundamental note to play (e.g., c2, a3).
 */
void playSaw(uint32_t baseNote){
    // Stop any previously playing notes to ensure a clean start.
    for(int i = 0; i < 7; i++) {
        synth.noteOff(i);
    }

    // --- Layer the detuned sawtooth waves ---
    // The frequency values are in CentiHertz (Hz * 100).
    // The small offsets (+150, -200, etc.) create the detuning.
    
    // Voice 0: The central, fundamental note.
    synth.noteOn(0, baseNote, 100);
    
    // Voices 1 & 2: Slightly detuned, panned slightly via volume.
    synth.noteOn(1, baseNote + 150, 80); // Detuned sharp by 1.5 Hz
    synth.noteOn(2, baseNote - 150, 80); // Detuned flat by 1.5 Hz
    
    // Voices 3 & 4: More detuned, lower volume.
    synth.noteOn(3, baseNote + 200, 70); // Detuned sharp by 2.0 Hz
    synth.noteOn(4, baseNote - 200, 70); // Detuned flat by 2.0 Hz
    
    // Voices 5 & 6: Most detuned, lowest volume.
    synth.noteOn(5, baseNote + 300, 60); // Detuned sharp by 3.0 Hz
    synth.noteOn(6, baseNote - 300, 60); // Detuned flat by 3.0 Hz
}
