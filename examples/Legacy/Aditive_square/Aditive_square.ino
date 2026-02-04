/**
 * @file Aditive_square.ino
 * @author Danilo Gabriel
 * @brief Additive synthesizer that builds a square wave from sine waves.
 * 
 * This example demonstrates the principles of additive synthesis by constructing
 * a square wave one harmonic at a time. It uses multiple voices, each playing a
 * sine wave at an odd harmonic of the fundamental frequency.
 * 
 * The volume of each harmonic is inversely proportional to its number (1/n),
 * which is characteristic of a square wave's Fourier series.
 * 
 * It uses the I2S output. Make sure you have an I2S DAC connected.
 * - BCK_PIN:  Your I2S bit clock pin
 * - WS_PIN:   Your I2S word select (LRC) pin
 * - DATA_PIN: Your I2S data out pin
 */

#include "ESP32Synth.h"

ESP32Synth synth;

// --- Pin Configuration for I2S DAC ---
// --- You MUST change these pins to match your setup ---
const int BCK_PIN = 26;
const int WS_PIN = 25;
const int DATA_PIN = 22;

// --- Synthesis Parameters ---
#define BASE_FREQ 110       // 110 Hz (A2)
#define MAX_VOICES_USE 32   // Maximum number of voices (harmonics) to use
#define STEP_DELAY 200      // Time between adding/removing each harmonic (ms)
#define HOLD_TIME 3000      // Time to hold the full wave before deconstructing (ms)

// --- State Control ---
unsigned long lastTime = 0;
int currentVoice = 0;
bool building = true;       // Are we building (true) or deconstructing (false) the wave?
bool holding = false;       // Are we holding the wave at the start or end of a cycle?

void setup() {
    Serial.begin(115200);
    delay(1000); 

    Serial.printf("ESP32Synth Additive Square Wave Example\n");
    Serial.printf("Using ESP32 core %s | Sample Rate: %d Hz\n", synth.getChipModel(), (int)synth.getSampleRate());
    Serial.println("Square Wave Formula: Volume = 1 / Harmonic Number");
    Serial.println("-------------------------------------------------");

    // Initialize the synth for I2S output
    if (!synth.begin(BCK_PIN, WS_PIN, DATA_PIN)) {
        Serial.println("!!! ERROR: Failed to initialize synthesizer.");
        while(1) delay(1000);
    }
    
    synth.setMasterVolume(200);

    // Initial setup for all voices that will be used
    for(int i = 0; i < MAX_VOICES_USE; i++) {
        synth.setWave(i, WAVE_SINE);
        // Set an organ-like envelope (no attack, full sustain, quick release)
        synth.setEnv(i, 0, 0, 127, 50); 
    }
    Serial.println("Synth initialized. Starting synthesis cycle...");
}

void loop() {
    unsigned long now = millis();

    // If we are in the 'holding' state, just wait until the hold time is over.
    if (holding) {
        if (now - lastTime > HOLD_TIME) {
            holding = false;
            lastTime = now;
            
            if (building) {
                // We just finished building the wave. Now we will start deconstructing it.
                Serial.println("\n>>> STATE: FULL SQUARE WAVE (Holding...) <<<\n");
                building = false; // Reverse direction to deconstruct
            } else {
                // We just finished deconstructing. Now we will start a new cycle.
                Serial.println("\n>>> STATE: SILENCE (Restarting Cycle...) <<<\n");
                building = true;  // Reverse direction to build again
                currentVoice = 0; 
                synth.noteOff(0); // Ensure fundamental is turned off
            }
        }
        return; // Do nothing else while holding
    }

    // This block handles the step-by-step building and deconstruction of the wave.
    if (now - lastTime > STEP_DELAY) {
        lastTime = now;

        if (building) {
            // If building, add one harmonic at a time until we reach the max.
            if (currentVoice < MAX_VOICES_USE) {
                addHarmonic(currentVoice);
                currentVoice++;
            } else {
                holding = true; // Reached the top, start holding
            }
        } else {
            // If deconstructing, remove one harmonic at a time until we reach silence.
            if (currentVoice > 0) {
                currentVoice--;
                removeHarmonic(currentVoice);
            } else {
                holding = true; // Reached the bottom, start holding
            }
        }
    }
}

/**
 * @brief Adds a harmonic to the wave.
 * @param voiceIndex The voice to use for this harmonic.
 */
void addHarmonic(int voiceIndex) {
    // A square wave is made of odd harmonics: 1, 3, 5, 7...
    int harmonicNum = (voiceIndex * 2) + 1;

    // Frequency = Fundamental Frequency * Harmonic Number
    uint32_t freqHz = BASE_FREQ * harmonicNum;
    
    // Volume = Max Volume / Harmonic Number (This is the 1/n decay)
    // We use 127 as max volume for a voice.
    int vol = 127 / harmonicNum;
    
    // Turn the note on for the corresponding voice.
    // The frequency must be converted to CentiHertz (*100).
    synth.noteOn(voiceIndex, freqHz * 100, vol);

    // --- Logging ---
    // Formatted print for easy reading in the Serial Monitor.
    Serial.printf("[+] ADD Voice %02d | Harmonic: #%02d | Freq: %5d Hz | Vol: %03d | ", 
                  voiceIndex, harmonicNum, freqHz, vol);
    
    // Simple bar graph to visualize the volume (energy) of the harmonic.
    Serial.print("Energy: ");
    int bars = vol / 5; 
    if(bars == 0 && vol > 0) bars = 1; // Show at least one bar if there's any sound.
    for(int i=0; i<bars; i++) Serial.print("█");
    Serial.println();
}

/**
 * @brief Removes a harmonic from the wave.
 * @param voiceIndex The voice to turn off.
 */
void removeHarmonic(int voiceIndex) {
    // Turn off the voice, which will trigger its release envelope.
    synth.noteOff(voiceIndex);
    
    int harmonicNum = (voiceIndex * 2) + 1;
    
    // --- Logging ---
    Serial.printf("[-] DEL Voice %02d | Harmonic: #%02d | Turning Off... | ", 
                  voiceIndex, harmonicNum);
    Serial.println("State: OFF");
}
