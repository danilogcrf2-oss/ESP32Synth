/**
 * @file BasicWaveforms.ino
 * @author Danilo Gabriel
 * @brief Demonstrates the basic waveforms of the ESP32Synth library.
 * 
 * This example plays a C major scale, cycling through the four fundamental
 * waveforms: Sine, Saw, Pulse, and Triangle.
 * 
 * It uses the I2S output. Make sure you have an I2S DAC connected.
 * - BCK_PIN:  Your I2S bit clock pin
 * - WS_PIN:   Your I2S word select (LRC) pin
 * - DATA_PIN: Your I2S data out pin
 */

#include <Arduino.h>
#include <ESP32Synth.h>

// --- Pin Configuration for I2S DAC ---
// --- You MUST change these pins to match your setup ---
const int BCK_PIN = 26;
const int WS_PIN = 25;
const int DATA_PIN = 22;

ESP32Synth synth;

// C Major Scale in CentiHertz (Hz * 100)
uint32_t scale[] = {
  c4, d4, e4, f4, g4, a4, b4, c5
};

void setup() {
  Serial.begin(115200);  Serial.println("ESP32Synth Basic Waveforms Example");
  Serial.printf("Using ESP32 core %s\n", synth.getChipModel());

  // Initialize the synth for I2S output
  // If you are using a classic ESP32 with the internal DAC on pin 25,
  // you can use: synth.begin(25);
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }
  
  synth.setMasterVolume(200); // Set a reasonable master volume (0-255)
  Serial.println("Synth initialized!");
}

void loop() {
  WaveType waves[] = { WAVE_SINE, WAVE_SAW, WAVE_PULSE, WAVE_TRIANGLE };
  const char* waveNames[] = { "SINE", "SAW", "PULSE", "TRIANGLE" };
  
  for (int w = 0; w < 4; w++) {
    WaveType currentWave = waves[w];
    Serial.printf("\n--- Playing C Major Scale with %s wave ---\n", waveNames[w]);

    // Set up a single voice (voice 0)
    uint8_t voice = 0;
    synth.setWave(voice, currentWave);
    synth.setEnv(voice, 10, 200, 100, 300); // Attack, Decay, Sustain, Release

    // For pulse waves, set a pulse width (e.g., 50% duty cycle)
    if (currentWave == WAVE_PULSE) {
      synth.setPulseWidth(voice, 128); // 0-255
    }

    // Play the scale
    for (int i = 0; i < 8; i++) {
      Serial.printf("Note: %d, Freq: %.2f Hz\n", i, (float)scale[i] / 100.0f);
      synth.noteOn(voice, scale[i], 127); // Play note with full volume
      delay(350);
      synth.noteOff(voice);
      delay(150); // Allow time for release envelope
    }
    
    delay(1000); // Wait a second before the next waveform
  }
}
