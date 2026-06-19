/**
 * @file      Virtual_Guitar.ino
 * @author    Danilo Gabriel
 * @brief     Advanced Physical Modeling Guitar & Virtual Pedalboard
 * 
 * This example demonstrates the extreme DSP capabilities of the ESP32Synth library.
 * Instead of using standard oscillators, it builds an Extended Karplus-Strong (EKS) 
 * string synthesis engine entirely inside a custom wave hook. 
 * 
 * It also features a complete Master DSP chain running in real-time:
 * - Helmholtz Acoustic Resonator
 * - VCA Compressor (Envelope Tracker)
 * - Tube Overdrive with Asymmetrical Clipping
 * - 4x12 Cabinet Simulator (3-Pole IIR Filter)
 * - Analog Chorus & Tape Delay
 * 
 * Hardware: Requires an I2S DAC (e.g., PCM5102A) for 32-bit audio fidelity.
 */

#pragma GCC optimize("O3,unroll-loops")

#include <Arduino.h>
#include <ESP32Synth.h>
#include <ESP32SynthNotes.h>
#include <esp_heap_caps.h>

// --- I2S DAC PINS ---
#define I2S_BCK_PIN  4
#define I2S_WS_PIN   15
#define I2S_DATA_PIN 2

ESP32Synth synth;

// ====================================================================================
// 1. EXTENDED KARPLUS-STRONG ENGINE
// ====================================================================================
#define MAX_GUITAR_STRINGS 8
#define KS_BUFFER_SIZE 1024

static int16_t** ks_delay_lines = nullptr;
static Voice*    active_string_ptrs[MAX_GUITAR_STRINGS] = {nullptr};

void IRAM_ATTR renderGuitarVoice(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int stringIdx = -1;
    for (int i = 0; i < MAX_GUITAR_STRINGS; i++) {
        if (active_string_ptrs[i] == vo) { stringIdx = i; break; }
        if (active_string_ptrs[i] == nullptr) { active_string_ptrs[i] = vo; stringIdx = i; break; }
    }
    if (stringIdx == -1) stringIdx = 0;

    int16_t* delayLine = ks_delay_lines[stringIdx];
    
    // cw[0]: Init Flag
    // cw[1]: Period
    // cw[2]: Index
    // cw[3]: Last Sample
    if (vo->cw[0] == 0) { 
        uint32_t period = (4800000 / vo->freqVal); 
        if (period > KS_BUFFER_SIZE - 1) period = KS_BUFFER_SIZE - 1;
        if (period < 2) period = 2;
        
        vo->cw[1] = period; vo->cw[2] = 0; vo->cw[3] = 0;
        
        uint32_t rng = vo->rngState;
        int32_t lp = 0;
        
        int32_t pick_energy = 32767;
        int32_t pick_decay  = (period < 150) ? (32767 / (period / 3 + 1)) : (32767 / period);
        int32_t lp_coef     = (period < 150) ? 220 : 120;

        for (uint32_t i = 0; i < period; i++) {
            rng = (rng * 1664525) + 1013904223;
            int32_t noise = (int32_t)(rng >> 16) - 32768; 
            
            noise = (noise * pick_energy) >> 15;
            if (pick_energy > pick_decay) pick_energy -= pick_decay; else pick_energy = 0;

            if (i < 3 && period < 150) noise += 24000; // Add pick transient to high notes

            lp += ((noise - lp) * lp_coef) >> 8; 
            delayLine[i] = (int16_t)(lp); 
        }
        vo->rngState = rng;
        vo->cw[0] = 1;
    }

    uint32_t period = vo->cw[1];
    uint32_t idx    = vo->cw[2];
    int32_t  lastS  = vo->cw[3];
    int32_t  currentEnv = startEnv;
    int32_t  volBase    = ((uint32_t)vo->vol * vo->trmModGain) >> 8;

    int32_t decay_factor = (vo->envState == ENV_RELEASE) ? 210 : 255; // Palm mute physical damping
    int32_t stretch      = (period < 150) ? 170 : 150; 

    for (int i = 0; i < samples; i++) {
        int16_t currentS = delayLine[idx];
        
        int32_t filtered = ((currentS * stretch) + (lastS * (256 - stretch))) >> 8;
        int32_t newVal   = (filtered * decay_factor) >> 8; 
        
        delayLine[idx] = newVal;
        lastS = newVal;
        
        idx++;
        if (idx >= period) idx = 0;

        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);

        mixBuffer[i] += (currentS * finalVol) >> 16;
        if (envStep != 0) currentEnv += envStep;
    }

    vo->cw[2] = idx;
    vo->cw[3] = lastS;
}

// ====================================================================================
// 2. VIRTUAL PEDALBOARD (MASTER DSP)
// ====================================================================================
bool pedal_chorus     = false;
bool pedal_distortion = false;
bool pedal_delay      = false;

#define CHORUS_SIZE 2048
#define DELAY_SIZE  16000

static int32_t* chorus_buffer = nullptr;
static int16_t* delay_buffer  = nullptr;
static uint32_t chorus_idx = 0;
static uint32_t chorus_lfo = 0;
static uint32_t delay_idx  = 0;

static int32_t body_lp  = 0;  
static int32_t comp_env = 0;  
static int32_t dist_hp  = 0;  
static int32_t cab_lp1  = 0;  
static int32_t cab_lp2  = 0;  
static int32_t cab_lp3  = 0;  

void IRAM_ATTR masterPedalboard(int32_t* mixBuffer, int samples) {
    for (int i = 0; i < samples; i++) {
        int32_t smp = mixBuffer[i];

        if (!pedal_distortion) {
            // Acoustic Resonator
            body_lp += ((smp - body_lp) * 12) >> 8; 
            smp = smp + body_lp; 
        } 
        else {
            // VCA Compressor
            int32_t rect = abs(smp);
            if (rect > comp_env) comp_env += ((rect - comp_env) * 10) >> 8; 
            else                 comp_env += ((rect - comp_env) * 1) >> 8;  

            int32_t gain = 256; 
            int32_t threshold = 2000;
            if (comp_env > threshold) {
                int32_t over = comp_env - threshold;
                gain = 256 - (over >> 5); 
                if (gain < 64) gain = 64; 
            }
            
            smp = (smp * gain) >> 8; 
            smp = smp * 4;           

            // Tube Overdrive
            dist_hp += ((smp - dist_hp) * 8) >> 8; 
            int32_t pre_smp = smp - dist_hp;

            int32_t driven = pre_smp * 14; 

            int32_t lim_p = 16000;
            int32_t lim_n = -12000;
            if (driven > lim_p)       driven = lim_p + ((driven - lim_p) >> 2);
            else if (driven < lim_n)  driven = lim_n + ((driven - lim_n) >> 2);
            
            if (driven > 24000)       driven = 24000;
            else if (driven < -24000) driven = -24000;
            
            smp = driven;

            // 4x12 Cabinet Simulator
            cab_lp1 += ((smp - cab_lp1) * 110) >> 8; 
            cab_lp2 += ((cab_lp1 - cab_lp2) * 110) >> 8;
            cab_lp3 += ((cab_lp2 - cab_lp3) * 110) >> 8;
            smp = cab_lp3; 
        }

        // Chorus
        if (pedal_chorus) {
            chorus_buffer[chorus_idx] = smp;
            chorus_lfo += 60000; 
            int32_t lfo_val = sineLUT[(chorus_lfo >> SINE_SHIFT) & SINE_LUT_MASK]; 
            
            int32_t mod_delay = 480 + ((lfo_val * 200) >> 15); 
            int32_t read_idx = chorus_idx - mod_delay;
            if (read_idx < 0) read_idx += CHORUS_SIZE;
            
            smp = (smp + chorus_buffer[read_idx]) >> 1; 
            
            chorus_idx++;
            if (chorus_idx >= CHORUS_SIZE) chorus_idx = 0;
        }

        // Tape Delay
        if (pedal_delay) {
            int32_t delayed_smp = delay_buffer[delay_idx];
            smp = smp + ((delayed_smp * 102) >> 8); 
            delay_buffer[delay_idx] = (int16_t)(smp >> 1); 
            delay_idx++;
            if (delay_idx >= DELAY_SIZE) delay_idx = 0;
        } else {
            delay_buffer[delay_idx] = 0; 
            delay_idx++;
            if (delay_idx >= DELAY_SIZE) delay_idx = 0;
        }

        mixBuffer[i] = smp;
    }
}

// ====================================================================================
// 3. SEQUENCER
// ====================================================================================

void setup() {
    Serial.begin(115200);

    // Dynamic heap allocation for large delay lines
    ks_delay_lines = (int16_t**)heap_caps_malloc(MAX_GUITAR_STRINGS * sizeof(int16_t*), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (int i = 0; i < MAX_GUITAR_STRINGS; i++) {
        ks_delay_lines[i] = (int16_t*)heap_caps_calloc(KS_BUFFER_SIZE, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    chorus_buffer = (int32_t*)heap_caps_calloc(CHORUS_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    delay_buffer  = (int16_t*)heap_caps_calloc(DELAY_SIZE, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if(!ks_delay_lines || !chorus_buffer || !delay_buffer) {
        Serial.println("Memory Allocation Failed!");
        while(1);
    }

    synth.setMasterVolume(255);
    
    if (!synth.begin(I2S_BCK_PIN, I2S_WS_PIN, I2S_DATA_PIN, I2S_32BIT)) {
        Serial.println("Failed to start ESP32Synth!"); 
        while(1);
    }

    synth.setCustomDSP(masterPedalboard);

    // Initialize custom physics voices
    for (int i = 0; i < MAX_GUITAR_STRINGS; i++) {
        synth.setCustomWave(i, renderGuitarVoice);
        synth.setEnv(i, 1, 0, 255, 1000); 
    }
}

void pluck(int voice, uint32_t note, uint16_t vol) {
    synth.noteOn(voice, note, vol);
}

uint32_t lastTickMs = 0;
uint32_t currentTick = 0;

void loop() {
    if (millis() - lastTickMs >= 125) {
        lastTickMs = millis();
        
        int section = (currentTick / 64) % 4; 
        int step = currentTick % 64;          
        int barStep = step % 16;              
        int bar = step / 16;                  

        // SECTION 0: Acoustic Fingerpicking
        if (section == 0) {
            pedal_chorus = false; pedal_distortion = false; pedal_delay = false;
            uint32_t root = e2, third = g2, fifth = b2, oct = e3, high = g3; 
            if (bar == 1) { root = c2; third = g2; fifth = c3; oct = e3; high = g3; } 
            if (bar == 2) { root = d2; third = fs2; fifth = a2; oct = d3; high = fs3; } 

            if (barStep == 0)  pluck(0, root, 255);
            if (barStep == 2)  pluck(1, third, 200);
            if (barStep == 4)  pluck(2, fifth, 180);
            if (barStep == 6)  pluck(3, oct, 160);
            if (barStep == 8)  pluck(4, high, 150);
            if (barStep == 10) pluck(3, oct, 140);
            if (barStep == 12) pluck(2, fifth, 130);
            if (barStep == 14) pluck(1, third, 120);
        }
        
        // SECTION 1: Clean Chorus Arpeggio
        else if (section == 1) {
            pedal_chorus = true; pedal_distortion = false; pedal_delay = false;
            uint32_t root = e2, third = g2, fifth = b2, oct = e3, high = g3; 
            if (bar == 1) { root = c2; third = g2; fifth = c3; oct = e3; high = g3; } 
            if (bar == 2) { root = d2; third = fs2; fifth = a2; oct = d3; high = fs3; } 

            if (barStep == 0)  pluck(0, root, 255);
            if (barStep == 2)  pluck(1, third, 220);
            if (barStep == 4)  pluck(2, fifth, 200);
            if (barStep == 6)  pluck(3, oct, 190);
            if (barStep == 8)  pluck(4, high, 180);
            if (barStep == 10) pluck(3, oct, 170);
            if (barStep == 12) pluck(2, fifth, 160);
            if (barStep == 14) pluck(1, third, 150);
        }
        
        // SECTION 2: Heavy Metal Chugging (Palm Mutes)
        else if (section == 2) {
            pedal_chorus = false; pedal_distortion = true; pedal_delay = false;
            uint32_t root = e2, fifth = b2, oct = e3;
            if (bar == 1) { root = c2; fifth = g2; oct = c3; }
            if (bar == 2) { root = d2; fifth = a2; oct = d3; }

            if (barStep == 0 || barStep == 3 || barStep == 6 || barStep == 8 || barStep == 11 || barStep == 14) {
                pluck(0, root, 255); pluck(1, fifth, 255); pluck(2, oct, 255);
            }
            if (barStep == 1 || barStep == 4 || barStep == 7 || barStep == 9 || barStep == 12 || barStep == 15) {
                synth.noteOff(0); synth.noteOff(1); synth.noteOff(2);
            }
        }
        
        // SECTION 3: Epic Solo
        else if (section == 3) {
            pedal_chorus = true; pedal_distortion = true; pedal_delay = true;
            uint32_t root = e2, fifth = b2, oct = e3;
            if (bar == 1) { root = c2; fifth = g2; oct = c3; }
            if (bar == 2) { root = d2; fifth = a2; oct = d3; }

            if (barStep == 0 || barStep == 6 || barStep == 12) {
                pluck(0, root, 255); pluck(1, fifth, 255); pluck(2, oct, 255);
            }
            if (barStep == 2 || barStep == 8 || barStep == 14) {
                synth.noteOff(0); synth.noteOff(1); synth.noteOff(2);
            }

            uint32_t soloNote = 0;
            if (step == 0)  soloNote = e4;
            if (step == 8)  soloNote = fs4;
            if (step == 16) soloNote = g4;
            if (step == 24) soloNote = a4;
            if (step == 32) soloNote = b4;
            if (step == 40) soloNote = e5;
            if (step == 48) soloNote = d5;
            if (step == 56) soloNote = b4;

            if (soloNote != 0) pluck(5, soloNote, 255);
        }

        currentTick++;
    }
}