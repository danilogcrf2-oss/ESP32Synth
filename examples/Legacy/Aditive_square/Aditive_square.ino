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
 * This example is "legacy" because it pushes the synth with many voices.
 * The number of harmonics is limited by MAX_VOICES in the library (default 6).
 */

#include "ESP32Synth.h"

// --- CHOOSE YOUR OUTPUT MODE ---
#define USE_I2S 1 // 1 for I2S DAC, 0 for internal DAC (classic ESP32 only)

#if USE_I2S
  const int BCK_PIN  = 26;
  const int WS_PIN   = 25;
  const int DATA_PIN = 22;
#else
  const int DAC_PIN = 25;
#endif

ESP32Synth synth;

// --- Synthesis Parameters ---
#define BASE_FREQ_CENTI HZ c4 // Base note for the square wave (e.g., C4 from ESP32SynthNotes.h)

// IMPORTANT: The number of voices used for harmonics cannot exceed MAX_VOICES
// defined in ESP32Synth.h (which defaults to 6).
// We will use all available voices to build the wave.
#define MAX_VOICES_USE 6

#define STEP_DELAY 250      // Time between adding/removing each harmonic (ms)
#define HOLD_TIME 3000      // Time to hold the full wave before deconstructing (ms)

// --- State Control ---
unsigned long lastTime = 0;
int currentVoice = 0;
bool building = true;       // Are we building (true) or deconstructing (false) the wave?
bool holding = false;       // Are we holding the wave at the start or end of a cycle?

void setup() {
    Serial.begin(115200);
    delay(1000); 

    Serial.println("ESP32Synth Additive Square Wave Example");
    
    // --- Initialize Synth ---
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
      while(1);
    }
    
    synth.setMasterVolume(200);
    Serial.printf("Using %d voices (harmonics) for synthesis.\n", MAX_VOICES_USE);

    // Initial setup for all voices that will be used
    for(int i = 0; i < MAX_VOICES_USE; i++) {
        synth.setWave(i, WAVE_SINE);
        // Set an organ-like envelope (no attack, full sustain, quick release)
        synth.setEnv(i, 0, 0, 255, 50); 
    }
    Serial.println("Synth initialized. Starting synthesis cycle...");
}

void loop() {
    unsigned long now = millis();

    if (holding) {
        if (now - lastTime > HOLD_TIME) {
            holding = false;
            lastTime = now;
            
            if (building) {
                Serial.println("\n>>> STATE: FULL WAVE (Holding...) <<<\n");
                building = false; // Reverse direction to deconstruct
            } else {
                Serial.println("\n>>> STATE: SILENCE (Restarting Cycle...) <<<\n");
                building = true;  // Reverse direction to build again
                currentVoice = 0; 
                synth.noteOff(0); // Ensure fundamental is turned off
            }
        }
        return; // Do nothing else while holding
    }

    if (now - lastTime > STEP_DELAY) {
        lastTime = now;

        if (building) {
            if (currentVoice < MAX_VOICES_USE) {
                addHarmonic(currentVoice);
                currentVoice++;
            } else {
                holding = true; // Reached the top, start holding
            }
        } else {
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
 */
void addHarmonic(int voiceIndex) {
    // A square wave is made of odd harmonics: 1, 3, 5, 7...
    int harmonicNum = (voiceIndex * 2) + 1;

    // Frequency = Fundamental Frequency * Harmonic Number
    uint32_t freqCentiHz = BASE_FREQ_CENTI_HZ * harmonicNum;
    
    // Volume = Max Volume / Harmonic Number (This is the 1/n decay)
    int vol = 255 / harmonicNum;
    
    synth.noteOn(voiceIndex, freqCentiHz, vol);

    // --- Logging ---
    Serial.printf("[+] ADD Voice %02d | Harmonic: #%02d | Freq: %6.2f Hz | Vol: %03d | ", 
                  voiceIndex, harmonicNum, freqCentiHz / 100.0, vol);
    
    Serial.print("Energy: ");
    int bars = vol / 8; 
    if(bars == 0 && vol > 0) bars = 1;
    for(int i=0; i<bars; i++) Serial.print("█");
    Serial.println();
}

/**
 * @brief Removes a harmonic from the wave.
 */
void removeHarmonic(int voiceIndex) {
    synth.noteOff(voiceIndex);
    
    int harmonicNum = (voiceIndex * 2) + 1;
    Serial.printf("[-] DEL Voice %02d | Harmonic: #%02d | Turning Off...\n", 
                  voiceIndex, harmonicNum);
}
