/*
 * ESP32Synth - Direct SD Card Audio Recording Example
 * 
 * This example demonstrates how to record the live output of the synthesizer
 * directly to a .wav file on an SD card in real-time, without dropping samples
 * or causing audio stutter.
 * 
 * It uses a dedicated background FreeRTOS task and a lock-free DMA Ring Buffer
 * to stream the master output to the SD card at a 16MHz SPI clock speed.
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "ESP32Synth.h"
#include "ESP32SynthNotes.h" // For note definitions (c4, ds4, etc.)

ESP32Synth synth;

// ==============================================================================
// HARDWARE PIN CONFIGURATION
// ==============================================================================

// SD Card SPI Pins (Standard ESP32 VSPI)
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS   5

// I2S DAC Pins (e.g., PCM5102A)
// Default ESP32:    BCK=4, WS=15, DATA=2
#define I2S_BCK  4
#define I2S_WS   15
#define I2S_DATA 2

// ==============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000); // Give serial monitor time to connect
    
    Serial.println("\n--- ESP32Synth SD Recording Example ---");

    // 1. Initialize SPI and SD Card at 16MHz for fast write speeds
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI, 16000000)) {
        Serial.println("ERROR: SD Card Mount Failed! Check wiring.");
        return;
    }
    Serial.println("SD Card initialized successfully at 16MHz.");

    // 2. Initialize the Synthesizer Engine
    if (!synth.begin(I2S_BCK, I2S_WS, I2S_DATA)) {
        Serial.println("ERROR: Synthesizer Initialization Failed!");
        return;
    }
    
    // Set a safe master volume (0-255) to prevent 16-bit integer clipping
    synth.setMasterVolume(120); 

    // 3. Configure Voice 0 as a Pluck Synthesizer
    synth.setWave(0, WAVE_SAW);
    // setEnv(voice, attack, decay, sustain_level, release)
    synth.setEnv(0, 2, 250, 0, 800); 

    // 4. Start Recording
    Serial.println("Starting recording to /synth_rec.wav...");
    
    // startRecording allocates the ring buffer and spawns the background writer task
    if (synth.startRecording(SD, "/synth_rec.wav")) {
        Serial.println("Recording active! Playing sequence...");

        // Play a simple arpeggio in C Minor (C, D#, G, C)
        // Notice the use of 'ds' (D sharp) instead of 'eb' (E flat)
        uint32_t sequence[] = {c4, ds4, g4, c5, g4, ds4};
        
        for (int loop = 0; loop < 4; loop++) {
            for (int i = 0; i < 6; i++) {
                synth.noteOn(0, sequence[i], 220);
                delay(150);
                synth.noteOff(0);
            }
        }

        // Allow the final release tail of the envelope to ring out
        delay(1000);

        // 5. Stop Recording
        // This flushes the remaining buffer and writes the official standard WAV header
        synth.stopRecording();
        Serial.println("Recording stopped and file saved successfully!");

    } else {
        Serial.println("ERROR: Failed to start recording.");
    }
}

void loop() {
    // The sequence is finished, nothing to do here.
    delay(1000);
}