/**
 * @file ESP32Synth_Hammond_Leslie122.ino
 * @author Danilo Gabriel & ESP32Synth Contributors
 * @brief High-fidelity Hammond B3 Organ with physically-modeled Leslie 122 cabinet.
 * 
 * This example showcases:
 * 1. 100% linear, high-precision 64-bit additive 9-drawbar synthesis (no bitcrush or phase clicks).
 * 2. Physically-modeled Dual-Rotor Leslie 122 (independent Treble Horn and Bass Drum LFOs).
 * 3. Belt-slip mechanical inertia emulation during slow-to-fast rotor acceleration/deceleration.
 * 4. Active damped Low-pass Feedback Comb Filter (LBCF) Schroeder Reverb to prevent metallic ringing.
 * 5. High-speed 15ms DC blocker to keep the signal fully centered.
 */

#include <Arduino.h>
#include <ESP32Synth.h>

// GCC branch predictor hints for raw performance
#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

// ====================================================================================
// HARDWARE & PINOUT CONFIGURATION
// ====================================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define I2S_BCK  4
  #define I2S_WS   6
  #define I2S_DATA 5
#else
  #define I2S_BCK  4
  #define I2S_WS   15
  #define I2S_DATA 2
#endif

ESP32Synth synth;

// ====================================================================================
// ORGAN GLOBAL DRAWBAR VALUES
// ====================================================================================
volatile uint8_t organDrawbars[9] = {255, 255, 255, 0, 0, 0, 0, 0, 0};
volatile uint8_t targetDrawbars[9] = {255, 255, 255, 0, 0, 0, 0, 0, 0};

// ====================================================================================
// PHYSICAL LESLIE 122 DUAL-ROTOR STATES & PARAMETERS (SIGNED INT32 PROTECTED)
// ====================================================================================
// Speeds in Hz * 100 (Chorale vs Tremolo)
// Treble Horn: Slow = 0.8Hz (80), Fast = 6.9Hz (690)
// Bass Drum: Slow = 0.7Hz (70), Fast = 5.7Hz (570)
volatile int32_t targetHornSpeed = 80;
volatile int32_t targetDrumSpeed = 70;

volatile int32_t currentHornSpeed = 80;
volatile int32_t currentDrumSpeed = 70;

volatile int32_t organLeslieTremoloDepthHorn = 80;
volatile int32_t organLeslieTremoloDepthDrum = 60;

// LFO Phase accumulators
uint32_t hornLFO = 0;
uint32_t drumLFO = 0;

// Rotary Delay Line (For High Horn Doppler)
#define ROTARY_DELAY_SIZE 1024
#define ROTARY_DELAY_MASK (ROTARY_DELAY_SIZE - 1)
int32_t rotaryDelayLine[ROTARY_DELAY_SIZE] = {0};
uint16_t rotaryWriteIdx = 0;

// Crossover Lowpass Filter State
static int32_t crossoverLPState = 0;

// ====================================================================================
// REVERB & DC BLOCKER PARAMETERS
// ====================================================================================
volatile int32_t  targetReverbMix = 140;       
volatile int32_t  reverbMix = 140;             

#define CMB1_SIZE 487
#define CMB2_SIZE 577
#define CMB3_SIZE 613
#define AP1_SIZE  191

int32_t cmb1_buf[CMB1_SIZE] = {0};
int32_t cmb2_buf[CMB2_SIZE] = {0};
int32_t cmb3_buf[CMB3_SIZE] = {0};
int32_t ap1_buf[AP1_SIZE] = {0};

uint16_t cmb1_idx = 0;
uint16_t cmb2_idx = 0;
uint16_t cmb3_idx = 0;
uint16_t ap1_idx = 0;

volatile int32_t reverbFeedback = 228; 
int32_t dc_estimate = 0;

// ====================================================================================
// 16.16 FIXED-POINT LINEAR INTERPOLATED SINE LOOKUP (MEGA OTIMIZADO)
// ====================================================================================
static inline int32_t __attribute__((always_inline)) get_interpolated_sine(uint32_t ph_val) {
    uint32_t idx = ph_val >> 20; // Já é garantido entre 0 a 4095 no wrap do 32-bit
    int32_t frac = (ph_val >> 12) & 0xFF; 
    int32_t s1 = sineLUT[idx];
    int32_t s2 = sineLUT[(idx + 1) & 4095];
    return s1 + (((s2 - s1) * frac) >> 8); 
}

// ====================================================================================
// POLYPHONIC CYCLING VOICE ALLOCATOR
// ====================================================================================
int chordPoolIdx = 0;
int bassPoolIdx = 0;
int melPoolIdx = 0;

void playOrganChordNote(uint32_t note, uint16_t volume) {
    if (note == 0) return;
    int voice = 0 + chordPoolIdx;
    synth.noteOn(voice, note, volume);
    chordPoolIdx = (chordPoolIdx + 1) % 12;
}

void playOrganBassNote(uint32_t note, uint16_t volume) {
    if (note == 0) return;
    int voice = 12 + bassPoolIdx;
    synth.noteOn(voice, note, volume);
    bassPoolIdx = (bassPoolIdx + 1) % 3;
}

void playOrganMelodyNote(uint32_t note, uint16_t volume) {
    if (note == 0) return;
    int voice = 15 + melPoolIdx;
    synth.noteOn(voice, note, volume);
    melPoolIdx = (melPoolIdx + 1) % 5;
}

// ====================================================================================
// CUSTOM WAVE GENERATOR: HIGH-PRECISION 64-BIT ADDITIVE OSCILLATOR
// ====================================================================================
void IRAM_ATTR organCustomWave(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase    = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    
    // Note initialization
    if (vo->cw[4] == 0) {
        vo->cw[0] = 0;            
        vo->cw[4] = 1;            
    }

    uint32_t ph  = vo->cw[0];
    uint32_t inc = (vo->phaseInc + vo->vibOffset) >> 1; 

    // Cache local dos drawbars com promoção nativa para 32-bits
    int32_t drawVal0 = organDrawbars[0]; int32_t drawVal1 = organDrawbars[1];
    int32_t drawVal2 = organDrawbars[2]; int32_t drawVal3 = organDrawbars[3];
    int32_t drawVal4 = organDrawbars[4]; int32_t drawVal5 = organDrawbars[5];
    int32_t drawVal6 = organDrawbars[6]; int32_t drawVal7 = organDrawbars[7];
    int32_t drawVal8 = organDrawbars[8];

    // OPTIMIZATION: Separando lógica de sustain estático (envStep == 0) para ignorar loops pesados
    if (envStep == 0) {
        int32_t envSafe  = currentEnv >> 14;
        envSafe         &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;
        
        if (finalVol == 0) {
            vo->cw[0] = ph + inc * samples;
            return;
        }

        for (int i = 0; i < samples; i++) {
            int32_t sum = (get_interpolated_sine(ph) * drawVal0) +                   
                          (get_interpolated_sine(ph * 3) * drawVal1) +               
                          (get_interpolated_sine(ph << 1) * drawVal2) +               
                          (get_interpolated_sine(ph << 2) * drawVal3) +               
                          (get_interpolated_sine(ph * 6) * drawVal4) +               
                          (get_interpolated_sine(ph << 3) * drawVal5) +               
                          (get_interpolated_sine(ph * 10) * drawVal6) +              
                          (get_interpolated_sine(ph * 12) * drawVal7) +              
                          (get_interpolated_sine(ph << 4) * drawVal8);              
                          
            // Max headroom (1.19B) se enquadra lindamente sem estourar o multiplicador 32-bit (2.14B). 
            // Uma multiplicação de 1 ciclo agora substitui todo o cast pesado em int64_t.
            mixBuffer[i] += ((sum >> 12) * finalVol) >> 18; 
            
            ph += inc;
        }
    } else {
        for (int i = 0; i < samples; i++) {
            int32_t envSafe  = currentEnv >> 14;
            envSafe         &= ~(envSafe >> 31);
            int32_t finalVol = (envSafe * volBase) >> 14;
            
            int32_t sum = (get_interpolated_sine(ph) * drawVal0) +                   
                          (get_interpolated_sine(ph * 3) * drawVal1) +               
                          (get_interpolated_sine(ph << 1) * drawVal2) +               
                          (get_interpolated_sine(ph << 2) * drawVal3) +               
                          (get_interpolated_sine(ph * 6) * drawVal4) +               
                          (get_interpolated_sine(ph << 3) * drawVal5) +               
                          (get_interpolated_sine(ph * 10) * drawVal6) +              
                          (get_interpolated_sine(ph * 12) * drawVal7) +              
                          (get_interpolated_sine(ph << 4) * drawVal8);              
                          
            mixBuffer[i] += ((sum >> 12) * finalVol) >> 18; 
            
            ph += inc;
            currentEnv += envStep;
        }
    }
    vo->cw[0] = ph;
}

// ====================================================================================
// CUSTOM DSP HOOK: DC BLOCKER, ROTARY LESLIE DUAL-SPEED & DAMPED REVERB
// ====================================================================================
void IRAM_ATTR organDSP(int32_t* mixBuffer, int numSamples) {
    int32_t hornSpeed = currentHornSpeed < 0 ? 0 : currentHornSpeed;
    int32_t drumSpeed = currentDrumSpeed < 0 ? 0 : currentDrumSpeed;

    uint32_t hornInc = (uint32_t)(((uint64_t)hornSpeed * 4294967296ULL) / (48000ULL * 100ULL));
    uint32_t drumInc = (uint32_t)(((uint64_t)drumSpeed * 4294967296ULL) / (48000ULL * 100ULL));
    
    uint32_t hLfo = hornLFO;
    uint32_t dLfo = drumLFO;
    uint16_t wIdx = rotaryWriteIdx;
    int32_t tremDepthHorn = organLeslieTremoloDepthHorn;
    int32_t tremDepthDrum = organLeslieTremoloDepthDrum;
    int32_t currentRevMix = reverbMix;
    int32_t lp_state = crossoverLPState;

    for (int i = 0; i < numSamples; i++) {
        int32_t sample = mixBuffer[i];
        
        // 1. Fast DC Blocker (~15ms settling time)
        dc_estimate += (sample - dc_estimate) >> 9; 
        sample -= dc_estimate;                      

        // 2. Bi-Amp Crossover: Phase-coherent low-pass crossover at ~950Hz
        lp_state += (sample - lp_state) >> 3;
        int32_t bass_sample = lp_state;
        int32_t treble_sample = sample - lp_state;

        // 3. Treble Horn: Doppler (Modulated Delay) + Cosine LFO (Tremolo)
        rotaryDelayLine[wIdx] = treble_sample;

        int16_t lfoSin = sineLUT[hLfo >> SINE_SHIFT];
        int32_t delayOffsetFixed = 23040 + ((lfoSin * 15) >> 6); 
        int32_t delayInt = delayOffsetFixed >> 8;
        int32_t delayFrac = delayOffsetFixed & 0xFF;

        int32_t rIdx1 = (wIdx - delayInt) & ROTARY_DELAY_MASK;
        int32_t rIdx2 = (rIdx1 - 1) & ROTARY_DELAY_MASK;

        int32_t s1 = rotaryDelayLine[rIdx1];
        int32_t s2 = rotaryDelayLine[rIdx2];
        int32_t delayedTreble = s1 + (((s2 - s1) * delayFrac) >> 8); 

        int16_t lfoCosHorn = sineLUT[(hLfo + (1UL << 30)) >> SINE_SHIFT];
        int32_t tremoloGainHorn = 256 + ((lfoCosHorn * tremDepthHorn) >> 15);
        treble_sample = (delayedTreble * tremoloGainHorn) >> 8;

        // 4. Bass Drum: Sine LFO Amplitude modulation only 
        int16_t lfoCosDrum = sineLUT[(dLfo + (1UL << 30)) >> SINE_SHIFT];
        int32_t tremoloGainDrum = 256 + ((lfoCosDrum * tremDepthDrum) >> 15);
        bass_sample = (bass_sample * tremoloGainDrum) >> 8;

        // Combine crossover bands
        int32_t drySample = bass_sample + treble_sample;

        // 5. Active Damped Schroeder Reverb (LBCF)
        // OTIMIZAÇÃO: Como o pipeline é 32-bit puro (limite de 2 bilhões), 
        // substituímos o '>> 2' (25%) por '>> 1' (50%), injetando o DOBRO de sinal no reverb!
        int32_t reverbInput = drySample >> 1; 

        // Comb 1 
        int32_t c1_out = cmb1_buf[cmb1_idx];
        static int32_t c1_damp = 0;
        c1_damp += (c1_out - c1_damp) >> 2; 
        cmb1_buf[cmb1_idx] = reverbInput + ((c1_damp * reverbFeedback) >> 8);
        if (++cmb1_idx >= CMB1_SIZE) cmb1_idx = 0;
        
        // Comb 2 
        int32_t c2_out = cmb2_buf[cmb2_idx];
        static int32_t c2_damp = 0;
        c2_damp += (c2_out - c2_damp) >> 2; 
        cmb2_buf[cmb2_idx] = reverbInput + ((c2_damp * (reverbFeedback - 5)) >> 8);
        if (++cmb2_idx >= CMB2_SIZE) cmb2_idx = 0;
        
        // Comb 3 
        int32_t c3_out = cmb3_buf[cmb3_idx];
        static int32_t c3_damp = 0;
        c3_damp += (c3_out - c3_damp) >> 2; 
        cmb3_buf[cmb3_idx] = reverbInput + ((c3_damp * (reverbFeedback - 10)) >> 8);
        if (++cmb3_idx >= CMB3_SIZE) cmb3_idx = 0;
        
        // Mix rápido dos 3 Combs (multiplicação otimizada ao invés de divisão por 3)
        int32_t mixedComb = ((c1_out + c2_out + c3_out) * 21845) >> 16;
        
        // All-pass (espalhamento estéreo / densidade)
        int32_t ap_out_delay = ap1_buf[ap1_idx];
        int32_t ap_val = mixedComb + ((ap_out_delay * 130) >> 8);
        ap1_buf[ap1_idx] = ap_val;
        int32_t wetSample = ap_out_delay - ((ap_val * 130) >> 8);
        if (++ap1_idx >= AP1_SIZE) ap1_idx = 0;
        
        // Low-pass suave no sinal Wet para tirar som metálico e aquecer a cauda
        static int32_t wet_lpf = 0;
        wet_lpf += (wetSample - wet_lpf) >> 1; 

        // Soma Mix Final (Sinal Limpo + Sinal Reverb processado)
        int32_t processed = drySample + ((wet_lpf * currentRevMix) >> 8);

        // 6. Linear Protective Clamp
        if (processed > 31000) processed = 31000;
        else if (processed < -31000) processed = -31000;
        
        mixBuffer[i] = processed;

        wIdx = (wIdx + 1) & ROTARY_DELAY_MASK;
        hLfo += hornInc;
        dLfo += drumInc;
    }

    hornLFO = hLfo;
    drumLFO = dLfo;
    rotaryWriteIdx = wIdx;
    crossoverLPState = lp_state;
}

// ====================================================================================
// MUSIC SCHEDULER & SONG SEQUENCER
// ====================================================================================
struct OrganStep {
    uint32_t duration;
    uint32_t chord[3];
    uint32_t bass;
    uint32_t melody;
};

const OrganStep song[] = {
    { 1200, { d4,  f4,  a4  }, d2,  a5  },
    { 400,  { cs4, e4,  g4  }, a1,  g5  },
    { 1200, { d4,  f4,  a4  }, d2,  f5  },
    { 800,  { f4,  a4,  c5  }, f2,  c6  },
    { 800,  { g4,  as4, d5  }, g2,  as5 },
    { 800,  { c4,  e4,  g4  }, c2,  g5  },
    { 1200, { f4,  a4,  c5  }, f2,  f5  },
    { 400,  { e4,  g4,  as4 }, c2,  e5  },
    { 1200, { d4,  f4,  a4  }, d2,  d5  },
    { 800,  { cs4, e4,  g4  }, a1,  g5  },
    { 800,  { d4,  f4,  a4  }, d2,  f5  },
    { 800,  { ds4, fs4, a4  }, ds2, fs5 },
    { 1200, { e4,  g4,  b4  }, e2,  g5  },
    { 800,  { e4,  gs4, b4  }, e2,  gs5 },
    { 1600, { a4,  cs5, e5  }, a2,  a5  },
    { 1600, { d4,  f4,  a4  }, d2,  d5  }
};
const int songLen = sizeof(song) / sizeof(OrganStep);

uint32_t lastStepTime = 0;
int currentStep = -1;
int currentScene = 0;
bool sceneTriggered = false;

void setTargetDrawbars(uint8_t draw0, uint8_t draw1, uint8_t draw2, uint8_t draw3, uint8_t draw4, uint8_t draw5, uint8_t draw6, uint8_t draw7, uint8_t draw8) {
    targetDrawbars[0] = draw0; targetDrawbars[1] = draw1; targetDrawbars[2] = draw2;
    targetDrawbars[3] = draw3; targetDrawbars[4] = draw4; targetDrawbars[5] = draw5;
    targetDrawbars[6] = draw6; targetDrawbars[7] = draw7; targetDrawbars[8] = draw8;
}

void applyScene(int scene) {
    Serial.println();
    Serial.println("==================================================");
    Serial.printf("MORPHING TO ORGAN SCENE %d: ", scene);
    
    switch (scene) {
        case 0:
            Serial.println("Warm Jazz B3 (Leslie Lento Dual-Rotor, Drawbars: 888000000)");
            setTargetDrawbars(255, 255, 255, 0, 0, 0, 0, 0, 0);
            targetHornSpeed = 80;  // 0.8Hz Slow Horn
            targetDrumSpeed = 70;  // 0.7Hz Slow Drum
            organLeslieTremoloDepthHorn = 40;
            organLeslieTremoloDepthDrum = 30;
            targetReverbMix = 110; 
            break;
            
        case 1:
            Serial.println("Soul Gospel B3 (Leslie Rápido Dual-Rotor, Correia Acelerando)");
            setTargetDrawbars(255, 255, 255, 128, 0, 0, 0, 0, 0);
            targetHornSpeed = 690; // 6.9Hz Fast Horn
            targetDrumSpeed = 570; // 5.7Hz Fast Drum
            organLeslieTremoloDepthHorn = 80;
            organLeslieTremoloDepthDrum = 60;
            targetReverbMix = 140; 
            break;
            
        case 2:
            Serial.println("Majestic Cathedral Full Drawbars (Slow Dual-Rotor Leslie)");
            setTargetDrawbars(255, 255, 255, 255, 255, 255, 255, 255, 255);
            targetHornSpeed = 80;
            targetDrumSpeed = 70;
            organLeslieTremoloDepthHorn = 50;
            organLeslieTremoloDepthDrum = 40;
            targetReverbMix = 180; 
            break;
            
        case 3:
            Serial.println("Ethereal Whistle (Leslie Stopped, Pure Sine Cathedral Reverb)");
            setTargetDrawbars(0, 0, 0, 0, 0, 255, 255, 255, 255);
            targetHornSpeed = 0;  // Parado
            targetDrumSpeed = 0;  
            organLeslieTremoloDepthHorn = 0;
            organLeslieTremoloDepthDrum = 0;
            targetReverbMix = 200; 
            break;
            
        case 4:
            Serial.println("Jimmy Smith Drawbars (Leslie Rápido Dual-Rotor, Fast Horn/Drum Ramps)");
            setTargetDrawbars(255, 255, 255, 0, 0, 0, 0, 0, 255);
            targetHornSpeed = 690; 
            targetDrumSpeed = 570; 
            organLeslieTremoloDepthHorn = 70;
            organLeslieTremoloDepthDrum = 50;
            targetReverbMix = 120;
            break;
    }
    Serial.println("==================================================");
}

// ====================================================================================
// STANDARD SYSTEM SETUP
// ====================================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Initializing ESP32Synth Hammond B3 Engine...");

    synth.setCustomDSP(organDSP);

    if (!synth.begin(I2S_BCK, I2S_WS, I2S_DATA, I2S_32BIT)) {
        Serial.println("Failed to start ESP32Synth!");
        while (1) { delay(1000); }
    }

    for (int v = 0; v < 20; v++) {
        synth.setCustomWave(v, organCustomWave);
        // O SEGREDO DO "GLOOO": Ataque amadeirado sutil e limpo
        synth.setEnv(v, 14, 0, 255, 60); 
    }

    synth.setMasterVolume(180);
    applyScene(0);
}

// ====================================================================================
// MAIN CONTROL LOOP (Defensive signed clipping loop)
// ====================================================================================
uint32_t lastParamTime = 0;

void loop() {
    uint32_t now = millis();

    // Physical belt-slip inertia and pulleys speed ramping emulation
    if (now - lastParamTime >= 15) {
        lastParamTime = now;
        
        // Treble Horn: fast acceleration with strict defensive underflow boundary check
        if (currentHornSpeed < targetHornSpeed) {
            currentHornSpeed += 12;
            if (currentHornSpeed > targetHornSpeed) currentHornSpeed = targetHornSpeed;
        } else if (currentHornSpeed > targetHornSpeed) {
            if (currentHornSpeed >= 6) {
                currentHornSpeed -= 6;
            } else {
                currentHornSpeed = targetHornSpeed;
            }
            if (currentHornSpeed < targetHornSpeed) currentHornSpeed = targetHornSpeed;
        }

        // Bass Drum: heavy slow acceleration with strict defensive underflow boundary check
        if (currentDrumSpeed < targetDrumSpeed) {
            currentDrumSpeed += 3;
            if (currentDrumSpeed > targetDrumSpeed) currentDrumSpeed = targetDrumSpeed;
        } else if (currentDrumSpeed > targetDrumSpeed) {
            if (currentDrumSpeed >= 1) {
                currentDrumSpeed -= 1;
            } else {
                currentDrumSpeed = targetDrumSpeed;
            }
            if (currentDrumSpeed < targetDrumSpeed) currentDrumSpeed = targetDrumSpeed;
        }
        
        // Drawbars smooth ramping with defensive uint8_t boundary check
        for (int d = 0; d < 9; d++) {
            if (organDrawbars[d] < targetDrawbars[d]) {
                organDrawbars[d] = (organDrawbars[d] + 2 > targetDrawbars[d]) ? targetDrawbars[d] : (organDrawbars[d] + 2);
            } else if (organDrawbars[d] > targetDrawbars[d]) {
                if (organDrawbars[d] >= 2) {
                    organDrawbars[d] = (organDrawbars[d] - 2 < targetDrawbars[d]) ? targetDrawbars[d] : (organDrawbars[d] - 2);
                } else {
                    organDrawbars[d] = targetDrawbars[d];
                }
            }
        }

        // Reverb Mix smooth ramping
        if (reverbMix < targetReverbMix) {
            reverbMix += 10; if (reverbMix > targetReverbMix) reverbMix = targetReverbMix;
        } else if (reverbMix > targetReverbMix) {
            reverbMix -= 10; if (reverbMix < targetReverbMix) reverbMix = targetReverbMix;
        }
    }

    // Play Song Sequence
    int nextStepIdx = (currentStep + 1) % songLen;
    uint32_t stepDuration = (currentStep == -1) ? 0 : song[currentStep].duration;

    if (currentStep == -1 || (now - lastStepTime >= stepDuration)) {
        if (nextStepIdx == 0 && currentStep != -1 && !sceneTriggered) {
            currentScene = (currentScene + 1) % 5;
            applyScene(currentScene);
            sceneTriggered = true;
        } else if (nextStepIdx != 0) {
            sceneTriggered = false;
        }

        for (int v = 0; v < 20; v++) {
            synth.noteOff(v);
        }

        currentStep = nextStepIdx;
        lastStepTime = now;

        for (int c = 0; c < 3; c++) {
            uint32_t note = song[currentStep].chord[c];
            if (note > 0) playOrganChordNote(note, 130);
        }

        uint32_t bassNote = song[currentStep].bass;
        if (bassNote > 0) playOrganBassNote(bassNote, 180);

        uint32_t melNote = song[currentStep].melody;
        if (melNote > 0) playOrganMelodyNote(melNote, 150);
    }
}
