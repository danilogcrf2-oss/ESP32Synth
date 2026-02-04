/**
 * @file ArpeggiatorFun.ino
 * @author Danilo Gabriel
 * @brief Demonstrates the easy-to-use Arpeggiator feature.
 *
 * This example plays a simple chord progression (Am - G - C - F).
 * For each chord, it activates a voice and uses the setArpeggio() function
 * to automatically play the notes of the chord in a rising pattern.
 *
 * This shows how to create complex, musical patterns with very little code.
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

// Define the time for each arpeggio note in milliseconds
const uint16_t arp_speed_ms = 120;

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32Synth Arpeggiator Fun Example");

  // --- Initialize Synth ---
  if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
    Serial.println("!!! ERROR: Failed to initialize synthesizer.");
    while (1) delay(1000);
  }
  
  synth.setMasterVolume(200);
  
  // --- Configure a voice for the arpeggio ---
  uint8_t voice = 0;
  synth.setWave(voice, WAVE_PULSE);    // A pulse wave sounds nice for arps
  synth.setPulseWidth(voice, 100);     // A narrower pulse width
  synth.setEnv(voice, 5, 50, 127, 100); // Fast attack, full sustain
  
  Serial.println("Synth initialized. Starting chord progression...");
}

void playChordArpeggio(const char* chordName, uint32_t n1, uint32_t n2, uint32_t n3, uint32_t n4) {
    uint8_t voice = 0; // We'll use the same voice for all chords
    
    Serial.printf("Playing Arpeggio for: %s\n", chordName);

    // Set the arpeggio with the notes of the chord.
    // The synth will automatically play n1, n2, n3, n4 and repeat.
    synth.setArpeggio(voice, arp_speed_ms, n1, n2, n3, n4);

    // NoteOn starts the arpeggio. The frequency given here (n1) is the first
    // note that will play, but the arpeggiator takes control immediately.
    synth.noteOn(voice, n1, 127);
    
    // Let the arpeggio play for 2 seconds
    delay(2000);
    
    // noteOff will stop the sound. We could also use detachArpeggio() if
    // we wanted the note to sustain on its last value.
    synth.noteOff(voice);
    
    // A small pause between chords
    delay(200);
}

void loop() {
  // --- Play a chord progression: Am - G - C - F ---

  // Arpeggio for A minor (A3, C4, E4, A4)
  playChordArpeggio("Am", a3, c4, e4, a4);

  // Arpeggio for G major (G3, B3, D4, G4)
  playChordArpeggio("G", g3, b3, d4, g4);

  // Arpeggio for C major (C4, E4, G4, C5)
  playChordArpeggio("C", c4, e4, g4, c5);

  // Arpeggio for F major (F3, A3, C4, F4)
  playChordArpeggio("F", f3, a3, c4, f4);

  Serial.println("\nProgression finished. Repeating in 5 seconds...\n");
  delay(5000);
}
