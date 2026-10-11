/*
    ESP32Synth_Patches.hpp

    A comprehensive collection of highly optimized, 32-bit integer DSP algorithms and 
    Synthesizer Patches for ESP32Synth. 
    
    Zero floats. Zero compromises. Maximum Polyphony.
*/

#pragma once
#include "ESP32Synth.h"
#include <esp_heap_caps.h>

// ====================================================================================
// GCC Branch Predictor Hints 
// ====================================================================================
#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
// ====================================================================================



    /*
        ====================================================================================
        IMPORTANT COMPILER NOTE: "dangerous relocation: l32r: literal placed after use"
        ====================================================================================
        Why are there no IRAM_ATTR tags on these functions?
        
        When we use aggressive optimizations like `#pragma GCC optimize("O3,unroll-loops")`, 
        the compiler generates massive, unrolled assembly blocks for maximum speed. 
        The Xtensa CPU uses an instruction called `l32r` to load constants from a memory 
        area called the 'literal pool'. The maximum distance `l32r` can reach is very short. 
        
        If we force massive unrolled functions into IRAM using `IRAM_ATTR`, the code block 
        gets larger than the reach of `l32r`, causing a fatal linker error.
        
        Solution: We let these functions live in standard Flash. Because they process arrays 
        of samples in tight loops, the ESP32's Instruction Cache (ICache) loads the loop 
        into the CPU cache instantly. The performance remains identical, the RAM is saved, 
        and the linker doesn't crash!
        ====================================================================================
    */


class ESP32Patches {
public:

    // ====================================================================================
    // POLY-BLEP ANTI-ALIASING ENGINE (Fixed-Point, 32-bit)
    // ====================================================================================
    
    // Core PolyBLEP smoothing function. 
    // Branch prediction (UNLIKELY) ensures the division is skipped 99% of the time, 
    // executing only at the exact sample of the waveform's discontinuity.
    static inline int32_t poly_blep(uint32_t ph, uint32_t inc) {
        if (UNLIKELY(inc == 0)) return 0; // Safety guard
        if (UNLIKELY(ph < inc)) {
            int32_t p = (int32_t)(((uint64_t)ph << 15) / inc); 
            int32_t one_minus_p = 32768 - p;
            return (one_minus_p * one_minus_p) >> 15;
        } else if (UNLIKELY(ph > (uint32_t)-inc)) {
            uint32_t diff = (uint32_t)-ph;
            int32_t p = (int32_t)(((uint64_t)diff << 15) / inc);
            int32_t one_minus_p = 32768 - p;
            return -((one_minus_p * one_minus_p) >> 15);
        }
        return 0;
    }

    // Anti-Aliased Sawtooth
    static void AA_Saw(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        int32_t currentEnv = startEnv;
        int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
        uint32_t ph = vo->phase;
        uint32_t inc = vo->phaseInc + vo->vibOffset;

        if (envStep == 0) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (envSafe * volBase) >> 14;
            if (finalVol == 0) { vo->phase += inc * samples; return; }

            for (int i = 0; i < samples; i++) {
                int32_t val = (int32_t)(ph >> 16) - 32768;
                val += poly_blep(ph, inc);
                mixBuffer[i] += (int32_t)(((int64_t)val * finalVol) >> 16);
                ph += inc;
            }
        } else {
            for (int i = 0; i < samples; i++) {
                int32_t envSafe = currentEnv >> 14;
                envSafe &= ~(envSafe >> 31);
                int32_t finalVol = (envSafe * volBase) >> 14;

                int32_t val = (int32_t)(ph >> 16) - 32768;
                val += poly_blep(ph, inc);
                mixBuffer[i] += (int32_t)(((int64_t)val * finalVol) >> 16);

                ph += inc;
                currentEnv += envStep;
            }
        }
        vo->phase = ph;
    }

    // Anti-Aliased Pulse / Square
    static void AA_Pulse(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        int32_t currentEnv = startEnv;
        int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
        uint32_t ph = vo->phase;
        uint32_t inc = vo->phaseInc + vo->vibOffset;
        uint32_t pw = vo->pulseWidth;

        if (envStep == 0) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (envSafe * volBase) >> 14;
            if (finalVol == 0) { vo->phase += inc * samples; return; }

            for (int i = 0; i < samples; i++) {
                int32_t val = (ph < pw) ? 32767 : -32768;
                val -= poly_blep(ph, inc);
                val += poly_blep((uint32_t)(ph - pw), inc);
                mixBuffer[i] += (int32_t)(((int64_t)val * finalVol) >> 16);
                ph += inc;
            }
        } else {
            for (int i = 0; i < samples; i++) {
                int32_t envSafe = currentEnv >> 14;
                envSafe &= ~(envSafe >> 31);
                int32_t finalVol = (envSafe * volBase) >> 14;

                int32_t val = (ph < pw) ? 32767 : -32768;
                val -= poly_blep(ph, inc);
                val += poly_blep((uint32_t)(ph - pw), inc);
                mixBuffer[i] += (int32_t)(((int64_t)val * finalVol) >> 16);

                ph += inc;
                currentEnv += envStep;
            }
        }
        vo->phase = ph;
    }

    // ====================================================================================
    // 1. BIQUAD OSCILLATOR: FAST (Direct Form I - Ultra Low CPU, Max Polyphony)
    // ====================================================================================
    // Optimized for maximum voice count (~3.45% CPU/voice).
    //
    // cp[0]: Wave Type (0 = Saw, 1 = Pulse, 2 = Triangle)
    // cp[1]: Cutoff Frequency (Hz) [20 to 20000]
    // cp[2]: Resonance/Q (Q * 100) [Ex: 70 = 0.7, 100 = 1.0, 450 = 4.5]
    // cp[3]: Filter Mode (0 = LPF, 1 = HPF, 2 = BPF, 3 = Notch)
    static void DSP_BiquadOscFast(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        int32_t currentEnv = startEnv;
        int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
        uint32_t ph = vo->phase;
        uint32_t inc = vo->phaseInc + vo->vibOffset;
        uint32_t pw = vo->pulseWidth;
        
        int16_t waveType = vo->cp[0];
        int32_t fc = vo->cp[1];
        if (fc < 20) fc = 20;
        else if (fc > 20000) fc = 20000;
        
        int32_t q_fixed = vo->cp[2];
        if (q_fixed < 10) q_fixed = 10; 
        
        int16_t filterMode = vo->cp[3];
        
        int32_t idx = (fc * 32) / 375;
        if (idx > 2047) idx = 2047; 
        
        int32_t sin_w0 = sineLUT[idx];
        int32_t cos_w0 = sineLUT[(idx + 1024) & 4095];
        
        int32_t alpha = (sin_w0 * 50) / q_fixed;
        
        int32_t k_a0 = 32768 + alpha;
        int32_t k_a1 = -2 * cos_w0;
        int32_t k_a2 = 32768 - alpha;
        
        int32_t k_b0, k_b1, k_b2;
        if (filterMode == 1) {
            k_b1 = -(32768 + cos_w0);
            k_b0 = (32768 + cos_w0) >> 1;
            k_b2 = k_b0;
        } else if (filterMode == 2) {
            k_b0 = alpha;
            k_b1 = 0;
            k_b2 = -alpha;
        } else if (filterMode == 3) {
            k_b0 = 32768;
            k_b1 = -2 * cos_w0;
            k_b2 = 32768;
        } else {
            k_b1 = 32768 - cos_w0;
            k_b0 = k_b1 >> 1;
            k_b2 = k_b0;
        }
        
        int32_t c_b0 = (int32_t)(((int64_t)k_b0 << 24) / k_a0);
        int32_t c_b1 = (int32_t)(((int64_t)k_b1 << 24) / k_a0);
        int32_t c_b2 = (int32_t)(((int64_t)k_b2 << 24) / k_a0);
        int32_t c_a1 = (int32_t)(((int64_t)k_a1 << 24) / k_a0);
        int32_t c_a2 = (int32_t)(((int64_t)k_a2 << 24) / k_a0);
        
        int32_t x1 = (int32_t)vo->cw[0];
        int32_t x2 = (int32_t)vo->cw[1];
        int32_t y1 = (int32_t)vo->cw[2];
        int32_t y2 = (int32_t)vo->cw[3];

        if (startEnv < 16384 && vo->envState == ENV_ATTACK) {
            x1 = 0; x2 = 0; y1 = 0; y2 = 0;
        }

        if (envStep == 0) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (envSafe * volBase) >> 14;

            if (waveType == 1) {
                for (int i = 0; i < samples; i++) {
                    int32_t x0 = (ph < pw) ? 32767 : -32768;
                    x0 -= poly_blep(ph, inc);
                    x0 += poly_blep((uint32_t)(ph - pw), inc);
                    
                    int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2) 
                                - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                    int32_t y0 = (int32_t)(acc >> 24);
                    
                    y0 -= (y0 >> 9);
                    if (UNLIKELY(y0 > 262143)) y0 = 262143;
                    else if (UNLIKELY(y0 < -262143)) y0 = -262143;
                    
                    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                    mixBuffer[i] += (int32_t)(((int64_t)y0 * finalVol) >> 16); 
                    ph += inc;
                }
            } else if (waveType == 2) {
                for (int i = 0; i < samples; i++) {
                    int16_t saw = (int16_t)(ph >> 16);
                    int32_t x0 = (int32_t)(((saw ^ (saw >> 15)) * 2) - 32767);
                    
                    int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2) 
                                - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                    int32_t y0 = (int32_t)(acc >> 24);
                    
                    y0 -= (y0 >> 9);
                    if (UNLIKELY(y0 > 262143)) y0 = 262143;
                    else if (UNLIKELY(y0 < -262143)) y0 = -262143;
                    
                    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                    mixBuffer[i] += (int32_t)(((int64_t)y0 * finalVol) >> 16);
                    ph += inc;
                }
            } else {
                for (int i = 0; i < samples; i++) {
                    int32_t x0 = (int32_t)(ph >> 16) - 32768; 
                    x0 += poly_blep(ph, inc);
                    
                    int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2) 
                                - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                    int32_t y0 = (int32_t)(acc >> 24);
                    
                    y0 -= (y0 >> 9);
                    if (UNLIKELY(y0 > 262143)) y0 = 262143;
                    else if (UNLIKELY(y0 < -262143)) y0 = -262143;
                    
                    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                    mixBuffer[i] += (int32_t)(((int64_t)y0 * finalVol) >> 16);
                    ph += inc;
                }
            }
        } else {
            if (waveType == 1) {
                for (int i = 0; i < samples; i++) {
                    int32_t x0 = (ph < pw) ? 32767 : -32768;
                    x0 -= poly_blep(ph, inc);
                    x0 += poly_blep((uint32_t)(ph - pw), inc);
                    
                    int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2) 
                                - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                    int32_t y0 = (int32_t)(acc >> 24);
                    
                    y0 -= (y0 >> 9);
                    if (UNLIKELY(y0 > 262143)) y0 = 262143;
                    else if (UNLIKELY(y0 < -262143)) y0 = -262143;
                    
                    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    
                    mixBuffer[i] += (int32_t)(((int64_t)y0 * finalVol) >> 16); 
                    ph += inc; currentEnv += envStep;
                }
            } else if (waveType == 2) {
                for (int i = 0; i < samples; i++) {
                    int16_t saw = (int16_t)(ph >> 16);
                    int32_t x0 = (int32_t)(((saw ^ (saw >> 15)) * 2) - 32767);
                    
                    int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2) 
                                - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                    int32_t y0 = (int32_t)(acc >> 24);
                    
                    y0 -= (y0 >> 9);
                    if (UNLIKELY(y0 > 262143)) y0 = 262143;
                    else if (UNLIKELY(y0 < -262143)) y0 = -262143;
                    
                    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    
                    mixBuffer[i] += (int32_t)(((int64_t)y0 * finalVol) >> 16); 
                    ph += inc; currentEnv += envStep;
                }
            } else {
                for (int i = 0; i < samples; i++) {
                    int32_t x0 = (int32_t)(ph >> 16) - 32768; 
                    x0 += poly_blep(ph, inc);
                    
                    int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2) 
                                - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                    int32_t y0 = (int32_t)(acc >> 24);
                    
                    y0 -= (y0 >> 9);
                    if (UNLIKELY(y0 > 262143)) y0 = 262143;
                    else if (UNLIKELY(y0 < -262143)) y0 = -262143;
                    
                    x2 = x1; x1 = x0; y2 = y1; y1 = y0;
                    int32_t envSafe = currentEnv >> 14;
                    envSafe &= ~(envSafe >> 31);
                    int32_t finalVol = (envSafe * volBase) >> 14;
                    
                    mixBuffer[i] += (int32_t)(((int64_t)y0 * finalVol) >> 16); 
                    ph += inc; currentEnv += envStep;
                }
            }
        }
        
        vo->phase = ph;
        vo->cw[0] = (uint32_t)x1;
        vo->cw[1] = (uint32_t)x2;
        vo->cw[2] = (uint32_t)y1;
        vo->cw[3] = (uint32_t)y2;
    }

    // ====================================================================================
    // 2. BIQUAD OSCILLATOR: HIGH-PRECISION (TDF-II Q28 - Studio Quality, Zero Drift)
    // ====================================================================================
    // Full 20 Hz to 20000 Hz precision with 64-bit internal states (~4.35% CPU/voice).
    //
    // cp[0]: Wave Type (0 = Saw, 1 = Pulse, 2 = Triangle)
    // cp[1]: Cutoff Frequency (Hz) [20 to 20000]
    // cp[2]: Resonance/Q (Q * 100) [Ex: 70 = 0.7, 100 = 1.0, 450 = 4.5]
    // cp[3]: Filter Mode (0 = LPF, 1 = HPF, 2 = BPF, 3 = Notch)
    static void DSP_BiquadOsc(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        int32_t currentEnv = startEnv;
        int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
        uint32_t ph = vo->phase;
        uint32_t inc = vo->phaseInc + vo->vibOffset;
        uint32_t pw = vo->pulseWidth;

        int16_t waveType = vo->cp[0];
        int32_t fc = vo->cp[1];
        if (fc < 20) fc = 20;
        else if (fc > 20000) fc = 20000;

        int32_t q_fixed = vo->cp[2];
        if (q_fixed < 10) q_fixed = 10;

        int16_t filterMode = vo->cp[3];

        int32_t sin_q28, cos_q28, omc_q28;

        if (fc < 350) {
            int32_t w0_q24 = fc * 2195;
            int32_t w0_sq_q24 = (int32_t)(((int64_t)w0_q24 * w0_q24) >> 24);
            int32_t w0_quad_q24 = (int32_t)(((int64_t)w0_sq_q24 * w0_sq_q24) >> 24);

            omc_q28 = (w0_sq_q24 << 3) - (int32_t)(((int64_t)w0_quad_q24 * 8) / 24);
            cos_q28 = (1 << 28) - omc_q28;

            int32_t w0_cube_q24 = (int32_t)(((int64_t)w0_q24 * w0_sq_q24) >> 24);
            sin_q28 = (w0_q24 << 4) - (int32_t)(((int64_t)w0_cube_q24 * 16) / 6);
        } else {
            uint32_t ph_fixed = (uint32_t)(((uint64_t)fc * 5726623ULL) >> 10);
            uint32_t idx = (ph_fixed >> 16) & SINE_LUT_MASK;
            uint32_t frac = ph_fixed & 0xFFFF;

            int32_t s0_val = sineLUT[idx];
            int32_t s1_val = sineLUT[(idx + 1) & SINE_LUT_MASK];
            int32_t sin_interp = s0_val + (int32_t)(((int64_t)(s1_val - s0_val) * frac) >> 16);
            sin_q28 = sin_interp << 13;

            uint32_t idx_c = (idx + (SINE_LUT_SIZE / 4)) & SINE_LUT_MASK;
            int32_t c0_val = sineLUT[idx_c];
            int32_t c1_val = sineLUT[(idx_c + 1) & SINE_LUT_MASK];
            int32_t cos_interp = c0_val + (int32_t)(((int64_t)(c1_val - c0_val) * frac) >> 16);
            cos_q28 = cos_interp << 13;
            omc_q28 = (1 << 28) - cos_q28;
        }

        int32_t alpha_q28 = (int32_t)(((int64_t)sin_q28 * 50) / q_fixed);
        if (alpha_q28 < 1) alpha_q28 = 1;

        int32_t k_a0 = (1 << 28) + alpha_q28;
        int32_t k_a2 = (1 << 28) - alpha_q28;

        int32_t k_b0, k_b1, k_b2;
        int64_t a1_val = -((int64_t)cos_q28 << 1);

        if (filterMode == 1) {
            int32_t one_p_cos = (1 << 28) + cos_q28;
            k_b0 = one_p_cos >> 1;
            k_b1 = -one_p_cos;
            k_b2 = k_b0;
        } else if (filterMode == 2) {
            k_b0 = alpha_q28;
            k_b1 = 0;
            k_b2 = -alpha_q28;
        } else if (filterMode == 3) {
            k_b0 = (1 << 28);
            k_b1 = (int32_t)a1_val;
            k_b2 = k_b0;
        } else {
            k_b0 = omc_q28 >> 1;
            k_b1 = omc_q28;
            k_b2 = k_b0;
        }

        int32_t c_b0 = (int32_t)(((int64_t)k_b0 << 28) / k_a0);
        int32_t c_b1 = (int32_t)(((int64_t)k_b1 << 28) / k_a0);
        int32_t c_b2 = (int32_t)(((int64_t)k_b2 << 28) / k_a0);
        int32_t c_a1 = (int32_t)((a1_val << 28) / k_a0);
        int32_t c_a2 = (int32_t)(((int64_t)k_a2 << 28) / k_a0);

        int64_t s1 = *(int64_t*)&vo->cw[0];
        int64_t s2 = *(int64_t*)&vo->cw[2];

        if (startEnv < 16384 && vo->envState == ENV_ATTACK) {
            s1 = 0; s2 = 0;
        }

        if (envStep == 0) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (envSafe * volBase) >> 14;

            for (int i = 0; i < samples; i++) {
                int32_t x0;
                if (waveType == 1) {
                    x0 = (ph < pw) ? 32767 : -32768;
                    x0 -= poly_blep(ph, inc);
                    x0 += poly_blep((uint32_t)(ph - pw), inc);
                } else if (waveType == 2) {
                    int16_t saw = (int16_t)(ph >> 16);
                    x0 = (int32_t)(((saw ^ (saw >> 15)) * 2) - 32767);
                } else {
                    x0 = (int32_t)(ph >> 16) - 32768;
                    x0 += poly_blep(ph, inc);
                }

                int64_t y_scaled = (int64_t)c_b0 * x0 + s1;
                int32_t y_int = (int32_t)(y_scaled >> 28);
                int32_t y_frac = (int32_t)(y_scaled & 0x0FFFFFFF);

                if (UNLIKELY(y_int > 262143)) { y_int = 262143; y_frac = 0; }
                else if (UNLIKELY(y_int < -262143)) { y_int = -262143; y_frac = 0; }

                int64_t fb_a1 = ((int64_t)c_a1 * y_int) + (((int64_t)c_a1 * y_frac) >> 28);
                int64_t fb_a2 = ((int64_t)c_a2 * y_int) + (((int64_t)c_a2 * y_frac) >> 28);

                s1 = ((int64_t)c_b1 * x0) - fb_a1 + s2;
                s2 = ((int64_t)c_b2 * x0) - fb_a2;

                mixBuffer[i] += (int32_t)(((int64_t)y_int * finalVol) >> 16);
                ph += inc;
            }
        } else {
            for (int i = 0; i < samples; i++) {
                int32_t x0;
                if (waveType == 1) {
                    x0 = (ph < pw) ? 32767 : -32768;
                    x0 -= poly_blep(ph, inc);
                    x0 += poly_blep((uint32_t)(ph - pw), inc);
                } else if (waveType == 2) {
                    int16_t saw = (int16_t)(ph >> 16);
                    x0 = (int32_t)(((saw ^ (saw >> 15)) * 2) - 32767);
                } else {
                    x0 = (int32_t)(ph >> 16) - 32768;
                    x0 += poly_blep(ph, inc);
                }

                int64_t y_scaled = (int64_t)c_b0 * x0 + s1;
                int32_t y_int = (int32_t)(y_scaled >> 28);
                int32_t y_frac = (int32_t)(y_scaled & 0x0FFFFFFF);

                if (UNLIKELY(y_int > 262143)) { y_int = 262143; y_frac = 0; }
                else if (UNLIKELY(y_int < -262143)) { y_int = -262143; y_frac = 0; }

                int64_t fb_a1 = ((int64_t)c_a1 * y_int) + (((int64_t)c_a1 * y_frac) >> 28);
                int64_t fb_a2 = ((int64_t)c_a2 * y_int) + (((int64_t)c_a2 * y_frac) >> 28);

                s1 = ((int64_t)c_b1 * x0) - fb_a1 + s2;
                s2 = ((int64_t)c_b2 * x0) - fb_a2;

                int32_t envSafe = currentEnv >> 14;
                envSafe &= ~(envSafe >> 31);
                int32_t finalVol = (envSafe * volBase) >> 14;

                mixBuffer[i] += (int32_t)(((int64_t)y_int * finalVol) >> 16);
                ph += inc;
                currentEnv += envStep;
            }
        }

        vo->phase = ph;
        *(int64_t*)&vo->cw[0] = s1;
        *(int64_t*)&vo->cw[2] = s2;
    }

    // High-Precision Alias for explicit naming
    static inline void DSP_BiquadOscHQ(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        DSP_BiquadOsc(vo, mixBuffer, samples, startEnv, envStep);
    }

    static void FM_Custom(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        if (vo->cw[2] == 0) { vo->cw[0] = 0; vo->cw[1] = ENV_MAX; vo->cw[2] = 1; }
        
        int32_t currentEnv = startEnv;
        int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
        
        uint32_t carPh = vo->phase;
        uint32_t carInc = vo->phaseInc + vo->vibOffset;
        uint32_t modPh = vo->cw[0];
        int32_t modEnv = vo->cw[1];
        
        int32_t ratioInt  = vo->cp[0] > 0 ? vo->cp[0] : 1;
        int32_t ratioFrac = vo->cp[1] > 0 ? vo->cp[1] : 0;
        uint32_t modInc = (carInc * ratioInt) + ((carInc * ratioFrac) / 100);
        
        int32_t modIndexBase = vo->cp[2] > 0 ? vo->cp[2] : 30;
        int32_t modDecay     = vo->cp[3] > 0 ? vo->cp[3] : 0;
        
        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t modEnvSafe = modEnv >> 14;
            modEnvSafe &= ~(modEnvSafe >> 31);
            
            uint32_t modIndex = (modEnvSafe * modIndexBase) >> 3;
            int32_t modVal = sineLUT[modPh >> SINE_SHIFT];
            modPh += modInc;
            
            uint32_t fmPhase = carPh + (uint32_t)((int64_t)modVal * modIndex);
            int32_t carVal = sineLUT[fmPhase >> SINE_SHIFT];
            carPh += carInc;
            
            int32_t finalVol = (envSafe * volBase) >> 14;
            mixBuffer[i] += (carVal * finalVol) >> 16;
            
            currentEnv += envStep;
            if (modDecay > 0) { modEnv -= modDecay; if (modEnv < 0) modEnv = 0; }
        }
        vo->phase = carPh; vo->cw[0] = modPh; vo->cw[1] = modEnv;
    }

    static void HammondB3(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        int32_t currentEnv = startEnv;
        int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
        uint32_t ph = vo->phase;
        uint32_t inc = (vo->phaseInc + vo->vibOffset) >> 1; 

        int32_t db0, db1, db2, db3, db4, db5, db6, db7, db8;
        switch (vo->cp[0]) {
            case 1:  db0=255; db1=255; db2=255; db3=128; db4=0;   db5=0;   db6=0;   db7=0;   db8=0;   break;
            case 2:  db0=255; db1=255; db2=255; db3=255; db4=255; db5=255; db6=255; db7=255; db8=255; break;
            case 3:  db0=0;   db1=0;   db2=0;   db3=0;   db4=0;   db5=255; db6=255; db7=255; db8=255; break;
            case 4:  db0=255; db1=255; db2=255; db3=0;   db4=0;   db5=0;   db6=0;   db7=0;   db8=255; break;
            default: db0=255; db1=255; db2=255; db3=0;   db4=0;   db5=0;   db6=0;   db7=0;   db8=0;   break;
        }

        for (int i = 0; i < samples; i++) {
            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (envSafe * volBase) >> 14; // Padronizado com os outros (0 a 65280)
            
            int32_t sum = (sineLUT[ph >> 20] * db0) +                   
                          (sineLUT[((uint32_t)(ph * 3)) >> 20] * db1) +               
                          (sineLUT[((uint32_t)(ph << 1)) >> 20] * db2) +               
                          (sineLUT[((uint32_t)(ph << 2)) >> 20] * db3) +               
                          (sineLUT[((uint32_t)(ph * 6)) >> 20] * db4) +               
                          (sineLUT[((uint32_t)(ph << 3)) >> 20] * db5) +               
                          (sineLUT[((uint32_t)(ph * 10)) >> 20] * db6) +              
                          (sineLUT[((uint32_t)(ph * 12)) >> 20] * db7) +              
                          (sineLUT[((uint32_t)(ph << 4)) >> 20] * db8);              
                          
            // Segurança Matemática: Evita overflow de int32 (sum >> 12 reduz o max 75.2M para 18359)
            // 18359 * 65280 = 1.19B (seguro no limite de 2.14B). Em seguida voltamos pro range de aúdio >> 15
            mixBuffer[i] += ((sum >> 12) * finalVol) >> 15; 
            ph += inc;
            currentEnv += envStep;
        }
        vo->phase = ph;
    }

    static void EKS_Guitar(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        #define MAX_GUITAR_STRINGS 12
        #define KS_BUFFER_SIZE 2048
        
        static int16_t** ks_delay_lines = nullptr;
        static Voice* active_string_ptrs[MAX_GUITAR_STRINGS] = {nullptr};
        
        if (ks_delay_lines == nullptr) {
            ks_delay_lines = (int16_t**)heap_caps_malloc(MAX_GUITAR_STRINGS * sizeof(int16_t*), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!ks_delay_lines) return;
            for (int i = 0; i < MAX_GUITAR_STRINGS; i++) {
                ks_delay_lines[i] = (int16_t*)heap_caps_calloc(KS_BUFFER_SIZE, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (!ks_delay_lines[i]) return;
            }
        }

        int stringIdx = -1;
        for (int i = 0; i < MAX_GUITAR_STRINGS; i++) { if (active_string_ptrs[i] == vo) { stringIdx = i; break; } }
        if (stringIdx == -1) {
            for (int i = 0; i < MAX_GUITAR_STRINGS; i++) {
                if (active_string_ptrs[i] == nullptr || !active_string_ptrs[i]->active) { active_string_ptrs[i] = vo; stringIdx = i; break; }
            }
        }
        if (stringIdx == -1) stringIdx = 0;

        int16_t* __restrict__ delayLine = ks_delay_lines[stringIdx];
        
        if (vo->cw[0] == 0) { 
            uint32_t period = (vo->phaseInc > 0) ? (uint32_t)(4294967296ULL / vo->phaseInc) : 109;
            if (period > KS_BUFFER_SIZE - 1) period = KS_BUFFER_SIZE - 1;
            if (period < 2) period = 2;
            
            vo->cw[1] = period; vo->cw[2] = 0; vo->cw[3] = 0;
            
            uint32_t rng = vo->rngState; int32_t lp = 0;
            int32_t pick_energy = 32767;
            int32_t pick_decay  = (period < 150) ? (32767 / (period / 3 + 1)) : (32767 / period);
            int32_t lp_coef     = (period < 150) ? 220 : 120;

            for (uint32_t i = 0; i < period; i++) {
                rng = (rng * 1664525) + 1013904223;
                int32_t noise = (int32_t)(rng >> 16) - 32768; 
                noise = (noise * pick_energy) >> 15;
                if (pick_energy > pick_decay) pick_energy -= pick_decay; else pick_energy = 0;
                if (i < 3 && period < 150) noise += 24000; 
                lp += ((noise - lp) * lp_coef) >> 8; 
                delayLine[i] = (int16_t)(lp); 
            }
            vo->rngState = rng; vo->cw[0] = 1;
        }

        const uint32_t period = vo->cw[1];
        uint32_t idx    = vo->cw[2];
        int32_t  lastS  = vo->cw[3];
        int32_t  currentEnv = startEnv;
        const int32_t  volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;

        const int32_t decay_factor = (vo->envState == ENV_RELEASE) ? 210 : 255; 
        const int32_t stretch      = (period < 150) ? 170 : 150; 
        const int32_t inv_stretch  = 256 - stretch;

        for (int i = 0; i < samples; i++) {
            int16_t currentS = delayLine[idx];
            int32_t filtered = ((currentS * stretch) + (lastS * inv_stretch)) >> 8;
            int32_t newVal   = (filtered * decay_factor) >> 8; 
            
            delayLine[idx] = (int16_t)newVal; lastS = newVal;
            idx++; if (idx >= period) idx = 0;

            int32_t envSafe = currentEnv >> 14;
            envSafe &= ~(envSafe >> 31);
            int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);

            mixBuffer[i] += (currentS * finalVol) >> 16;
            currentEnv += envStep;
        }

        vo->cw[2] = idx; vo->cw[3] = lastS;
    }


    // ====================================================================================
    // PRE-MADE DSP EFFECTS (Custom DSP Hooks)
    // ====================================================================================

    static void DSP_HammondLeslie(int32_t* mixBuffer, int numSamples, int16_t* dp) {
        #define ROTARY_DELAY_SIZE 1024
        #define ROTARY_DELAY_MASK (ROTARY_DELAY_SIZE - 1)
        #define CMB1_SIZE 487
        #define CMB2_SIZE 577
        #define CMB3_SIZE 613
        #define AP1_SIZE  191
        
        static int32_t* rotaryDelayLine = nullptr;
        static int32_t* cmb1_buf = nullptr; static int32_t* cmb2_buf = nullptr;
        static int32_t* cmb3_buf = nullptr; static int32_t* ap1_buf = nullptr;
        static uint16_t rotaryWriteIdx=0, cmb1_idx=0, cmb2_idx=0, cmb3_idx=0, ap1_idx=0;
        static int32_t currentHornSpeed=0, currentDrumSpeed=0, crossoverLPState=0, dc_estimate=0, currentRevMix=0;
        static uint32_t hornLFO=0, drumLFO=0;

        if (rotaryDelayLine == nullptr) {
            rotaryDelayLine = (int32_t*)heap_caps_calloc(ROTARY_DELAY_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            cmb1_buf = (int32_t*)heap_caps_calloc(CMB1_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            cmb2_buf = (int32_t*)heap_caps_calloc(CMB2_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            cmb3_buf = (int32_t*)heap_caps_calloc(CMB3_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            ap1_buf  = (int32_t*)heap_caps_calloc(AP1_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!rotaryDelayLine || !cmb1_buf || !cmb2_buf || !cmb3_buf || !ap1_buf) return;
        }
        
        int speedState = dp[0];
        int32_t targetHornSpeed = (speedState == 2) ? 690 : (speedState == 1) ? 80 : 0;
        int32_t targetDrumSpeed = (speedState == 2) ? 570 : (speedState == 1) ? 70 : 0;
        int32_t targetReverb = dp[1] > 0 ? dp[1] : 140;
        
        if (currentHornSpeed < targetHornSpeed) { currentHornSpeed += 12; if (currentHornSpeed > targetHornSpeed) currentHornSpeed = targetHornSpeed; } 
        else if (currentHornSpeed > targetHornSpeed) { if (currentHornSpeed >= 6) currentHornSpeed -= 6; else currentHornSpeed = targetHornSpeed; }

        if (currentDrumSpeed < targetDrumSpeed) { currentDrumSpeed += 3; if (currentDrumSpeed > targetDrumSpeed) currentDrumSpeed = targetDrumSpeed; } 
        else if (currentDrumSpeed > targetDrumSpeed) { if (currentDrumSpeed >= 1) currentDrumSpeed -= 1; else currentDrumSpeed = targetDrumSpeed; }
        
        if (currentRevMix < targetReverb) { currentRevMix += 2; if (currentRevMix > targetReverb) currentRevMix = targetReverb; } 
        else if (currentRevMix > targetReverb) { currentRevMix -= 2; if (currentRevMix < targetReverb) currentRevMix = targetReverb; }

        int32_t tremDepthHorn = (speedState == 2) ? 80 : (speedState == 1) ? 40 : 0;
        int32_t tremDepthDrum = (speedState == 2) ? 60 : (speedState == 1) ? 30 : 0;

        uint32_t hornInc = (uint32_t)(((uint64_t)currentHornSpeed * 4294967296ULL) / 4800000ULL);
        uint32_t drumInc = (uint32_t)(((uint64_t)currentDrumSpeed * 4294967296ULL) / 4800000ULL);
        
        for (int i = 0; i < numSamples; i++) {
            int32_t sample = mixBuffer[i];
            
            dc_estimate += (sample - dc_estimate) >> 9; sample -= dc_estimate;                      

            crossoverLPState += (sample - crossoverLPState) >> 3;
            int32_t bass_sample = crossoverLPState;
            int32_t treble_sample = sample - crossoverLPState;

            rotaryDelayLine[rotaryWriteIdx] = treble_sample;

            int16_t lfoSin = sineLUT[hornLFO >> SINE_SHIFT];
            int32_t delayOffsetFixed = 23040 + ((lfoSin * 15) >> 6); 
            int32_t rIdx1 = (rotaryWriteIdx - (delayOffsetFixed >> 8)) & ROTARY_DELAY_MASK;
            int32_t s1 = rotaryDelayLine[rIdx1];
            int32_t delayedTreble = s1 + (((rotaryDelayLine[(rIdx1 - 1) & ROTARY_DELAY_MASK] - s1) * (delayOffsetFixed & 0xFF)) >> 8); 

            treble_sample = (delayedTreble * (256 + ((sineLUT[(hornLFO + (1UL << 30)) >> SINE_SHIFT] * tremDepthHorn) >> 15))) >> 8;
            bass_sample = (bass_sample * (256 + ((sineLUT[(drumLFO + (1UL << 30)) >> SINE_SHIFT] * tremDepthDrum) >> 15))) >> 8;

            int32_t drySample = bass_sample + treble_sample;
            int32_t reverbInput = drySample >> 1; 

            int32_t c1_out = cmb1_buf[cmb1_idx]; static int32_t c1_damp = 0; c1_damp += (c1_out - c1_damp) >> 2; 
            cmb1_buf[cmb1_idx] = reverbInput + ((c1_damp * 228) >> 8); if (++cmb1_idx >= CMB1_SIZE) cmb1_idx = 0;
            
            int32_t c2_out = cmb2_buf[cmb2_idx]; static int32_t c2_damp = 0; c2_damp += (c2_out - c2_damp) >> 2; 
            cmb2_buf[cmb2_idx] = reverbInput + ((c2_damp * 223) >> 8); if (++cmb2_idx >= CMB2_SIZE) cmb2_idx = 0;
            
            int32_t c3_out = cmb3_buf[cmb3_idx]; static int32_t c3_damp = 0; c3_damp += (c3_out - c3_damp) >> 2; 
            cmb3_buf[cmb3_idx] = reverbInput + ((c3_damp * 218) >> 8); if (++cmb3_idx >= CMB3_SIZE) cmb3_idx = 0;
            
            int32_t ap_out_delay = ap1_buf[ap1_idx];
            int32_t ap_val = (((c1_out + c2_out + c3_out) * 21845) >> 16) + ((ap_out_delay * 130) >> 8);
            ap1_buf[ap1_idx] = ap_val;
            if (++ap1_idx >= AP1_SIZE) ap1_idx = 0;
            
            static int32_t wet_lpf = 0;
            wet_lpf += ((ap_out_delay - ((ap_val * 130) >> 8)) - wet_lpf) >> 1; 

            mixBuffer[i] = drySample + ((wet_lpf * currentRevMix) >> 8);

            rotaryWriteIdx = (rotaryWriteIdx + 1) & ROTARY_DELAY_MASK;
            hornLFO += hornInc; drumLFO += drumInc;
        }
    }

    static void DSP_Pedalboard(int32_t* mixBuffer, int numSamples, int16_t* dp) {
        #define CHORUS_SIZE 2048
        #define CHORUS_MASK (CHORUS_SIZE - 1)
        #define DELAY_SIZE  16384
        #define DELAY_MASK  (DELAY_SIZE - 1)

        static int32_t* chorus_buffer = nullptr;
        static int16_t* delay_buffer  = nullptr;
        static uint32_t chorus_idx=0, chorus_lfo=0, delay_idx=0;
        static int32_t body_lp=0, comp_env=0, dist_hp=0, cab_lp1=0, cab_lp2=0, cab_lp3=0;
        
        if (chorus_buffer == nullptr) {
            chorus_buffer = (int32_t*)heap_caps_calloc(CHORUS_SIZE, sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            delay_buffer  = (int16_t*)heap_caps_calloc(DELAY_SIZE, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!chorus_buffer || !delay_buffer) return;
        }
        
        bool pedal_distortion = (dp[0] > 0);
        bool pedal_chorus     = (dp[1] > 0);
        bool pedal_delay      = (dp[2] > 0);

        for (int i = 0; i < numSamples; i++) {
            int32_t smp = mixBuffer[i];

            if (!pedal_distortion) {
                body_lp += ((smp - body_lp) * 12) >> 8; smp += body_lp; 
            } else {
                int32_t mask = smp >> 31; int32_t rect = (smp ^ mask) - mask;
                comp_env += ((rect - comp_env) * (rect > comp_env ? 10 : 1)) >> 8;  
                int32_t gain = 256; 
                if (comp_env > 2000) { gain = 256 - ((comp_env - 2000) >> 5); if (gain < 64) gain = 64; }
                
                dist_hp += ((((smp * gain) >> 8) * 4) - dist_hp) * 8 >> 8; 
                int32_t driven = (((smp * gain) >> 8) * 4 - dist_hp) * 14; 

                if (driven > 16000) driven = 16000 + ((driven - 16000) >> 2);
                else if (driven < -12000) driven = -12000 + ((driven + 12000) >> 2);
                
                cab_lp1 += ((driven - cab_lp1) * 110) >> 8; cab_lp2 += ((cab_lp1 - cab_lp2) * 110) >> 8; cab_lp3 += ((cab_lp2 - cab_lp3) * 110) >> 8;
                smp = cab_lp3; 
            }

            if (pedal_chorus) {
                chorus_buffer[chorus_idx] = smp; chorus_lfo += 60000; 
                smp = (smp + chorus_buffer[(chorus_idx - (480 + ((sineLUT[(chorus_lfo >> SINE_SHIFT) & SINE_LUT_MASK] * 200) >> 15))) & CHORUS_MASK]) >> 1; 
                chorus_idx = (chorus_idx + 1) & CHORUS_MASK;
            }

            if (pedal_delay) {
                int32_t delayed_smp = delay_buffer[delay_idx];
                smp += (delayed_smp * 102) >> 8; 
                delay_buffer[delay_idx] = (int16_t)(smp >> 1); 
            } else {
                delay_buffer[delay_idx] = 0; 
            }
            delay_idx = (delay_idx + 1) & DELAY_MASK;

            mixBuffer[i] = (smp * 180) >> 8;
        }
    }

    // ====================================================================================
    // PRE-MADE CUSTOM EFFECTS (Custom FX Hooks)
    // ====================================================================================

    // ====================================================================================
    // MODULAR CFX: PURE BIQUAD TDF-II FILTER
    // ====================================================================================
    // Pure bus filter slot. Zero oscillators, pure in-place audio filtering.
    //
    // ep[0]: Cutoff / Center Frequency (Hz) [20 to 20000]
    // ep[1]: Filter Mode (0 = LPF, 1 = HPF, 2 = BPF, 3 = Notch)
    // ep[2]: Resonance (Q * 100) OR Bandwidth in Hz
    // ep[3]: Unit Mode:
    //        0 = Standard Q mode (ep[2] is Q * 100, ex: 70 = 0.707, 100 = 1.0, 450 = 4.5)
    //        1 = Bandwidth in Hz mode (ep[2] is BW in Hz, ex: 60 = 60Hz, 200 = 200Hz)
    // ep[4]: Dry/Wet Mix (0 or 255 = 100% Wet, 1 to 254 = Dry/Wet blend)
    static void FX_BiquadFilter(int32_t* busBuffer, int samples, int16_t* ep, int32_t* es) {
        int32_t fc = ep[0];
        if (fc < 20) fc = 20;
        else if (fc > 20000) fc = 20000;

        int16_t filterMode = ep[1];
        int32_t q_param = ep[2];
        int16_t bwMode = ep[3];
        int16_t mix = ep[4];
        if (mix <= 0 || mix > 255) mix = 255;

        int32_t sin_q28, cos_q28, omc_q28;

        if (fc < 350) {
            int32_t w0_q24 = fc * 2195;
            int32_t w0_sq_q24 = (int32_t)(((int64_t)w0_q24 * w0_q24) >> 24);
            int32_t w0_quad_q24 = (int32_t)(((int64_t)w0_sq_q24 * w0_sq_q24) >> 24);

            omc_q28 = (w0_sq_q24 << 3) - (int32_t)(((int64_t)w0_quad_q24 * 8) / 24);
            cos_q28 = (1 << 28) - omc_q28;

            int32_t w0_cube_q24 = (int32_t)(((int64_t)w0_q24 * w0_sq_q24) >> 24);
            sin_q28 = (w0_q24 << 4) - (int32_t)(((int64_t)w0_cube_q24 * 16) / 6);
        } else {
            uint32_t ph_fixed = (uint32_t)(((uint64_t)fc * 5726623ULL) >> 10);
            uint32_t idx = (ph_fixed >> 16) & SINE_LUT_MASK;
            uint32_t frac = ph_fixed & 0xFFFF;

            int32_t s0_val = sineLUT[idx];
            int32_t s1_val = sineLUT[(idx + 1) & SINE_LUT_MASK];
            int32_t sin_interp = s0_val + (int32_t)(((int64_t)(s1_val - s0_val) * frac) >> 16);
            sin_q28 = sin_interp << 13;

            uint32_t idx_c = (idx + (SINE_LUT_SIZE / 4)) & SINE_LUT_MASK;
            int32_t c0_val = sineLUT[idx_c];
            int32_t c1_val = sineLUT[(idx_c + 1) & SINE_LUT_MASK];
            int32_t cos_interp = c0_val + (int32_t)(((int64_t)(c1_val - c0_val) * frac) >> 16);
            cos_q28 = cos_interp << 13;
            omc_q28 = (1 << 28) - cos_q28;
        }

        int32_t alpha_q28;
        if (bwMode == 1) {
            int32_t bw_hz = (q_param < 1) ? 1 : q_param;
            alpha_q28 = (int32_t)(((int64_t)sin_q28 * bw_hz) / ((int64_t)fc << 1));
        } else {
            int32_t q_fixed = (q_param < 10) ? 10 : q_param;
            alpha_q28 = (int32_t)(((int64_t)sin_q28 * 50) / q_fixed);
        }
        if (alpha_q28 < 1) alpha_q28 = 1;

        int32_t k_a0 = (1 << 28) + alpha_q28;
        int32_t k_a2 = (1 << 28) - alpha_q28;

        int32_t k_b0, k_b1, k_b2;
        int64_t a1_val = -((int64_t)cos_q28 << 1);

        if (filterMode == 1) {
            int32_t one_p_cos = (1 << 28) + cos_q28;
            k_b0 = one_p_cos >> 1;
            k_b1 = -one_p_cos;
            k_b2 = k_b0;
        } else if (filterMode == 2) {
            k_b0 = alpha_q28;
            k_b1 = 0;
            k_b2 = -alpha_q28;
        } else if (filterMode == 3) {
            k_b0 = (1 << 28);
            k_b1 = (int32_t)a1_val;
            k_b2 = k_b0;
        } else {
            k_b0 = omc_q28 >> 1;
            k_b1 = omc_q28;
            k_b2 = k_b0;
        }

        int32_t c_b0 = (int32_t)(((int64_t)k_b0 << 28) / k_a0);
        int32_t c_b1 = (int32_t)(((int64_t)k_b1 << 28) / k_a0);
        int32_t c_b2 = (int32_t)(((int64_t)k_b2 << 28) / k_a0);
        int32_t c_a1 = (int32_t)((a1_val << 28) / k_a0);
        int32_t c_a2 = (int32_t)(((int64_t)k_a2 << 28) / k_a0);

        int64_t s1 = *(int64_t*)&es[0];
        int64_t s2 = *(int64_t*)&es[2];

        if (mix == 255) {
            for (int i = 0; i < samples; i++) {
                int32_t x0 = busBuffer[i];
                int64_t y_scaled = (int64_t)c_b0 * x0 + s1;
                int32_t y_int = (int32_t)(y_scaled >> 28);
                int32_t y_frac = (int32_t)(y_scaled & 0x0FFFFFFF);

                if (UNLIKELY(y_int > 262143)) { y_int = 262143; y_frac = 0; }
                else if (UNLIKELY(y_int < -262143)) { y_int = -262143; y_frac = 0; }

                int64_t fb_a1 = ((int64_t)c_a1 * y_int) + (((int64_t)c_a1 * y_frac) >> 28);
                int64_t fb_a2 = ((int64_t)c_a2 * y_int) + (((int64_t)c_a2 * y_frac) >> 28);

                s1 = ((int64_t)c_b1 * x0) - fb_a1 + s2;
                s2 = ((int64_t)c_b2 * x0) - fb_a2;

                busBuffer[i] = y_int;
            }
        } else {
            for (int i = 0; i < samples; i++) {
                int32_t x0 = busBuffer[i];
                int64_t y_scaled = (int64_t)c_b0 * x0 + s1;
                int32_t y_int = (int32_t)(y_scaled >> 28);
                int32_t y_frac = (int32_t)(y_scaled & 0x0FFFFFFF);

                if (UNLIKELY(y_int > 262143)) { y_int = 262143; y_frac = 0; }
                else if (UNLIKELY(y_int < -262143)) { y_int = -262143; y_frac = 0; }

                int64_t fb_a1 = ((int64_t)c_a1 * y_int) + (((int64_t)c_a1 * y_frac) >> 28);
                int64_t fb_a2 = ((int64_t)c_a2 * y_int) + (((int64_t)c_a2 * y_frac) >> 28);

                s1 = ((int64_t)c_b1 * x0) - fb_a1 + s2;
                s2 = ((int64_t)c_b2 * x0) - fb_a2;

                busBuffer[i] = x0 + (((y_int - x0) * mix) >> 8);
            }
        }

        *(int64_t*)&es[0] = s1;
        *(int64_t*)&es[2] = s2;
    }
    
    // ====================================================================================
    // MODULAR CFX: FAST BIQUAD FILTER (Direct Form I - Ultra Low CPU)
    // ====================================================================================
    // Fast bus filter slot based on Direct Form I topology for maximum CPU efficiency.
    //
    // ep[0]: Cutoff / Center Frequency (Hz) [20 to 20000]
    // ep[1]: Filter Mode (0 = LPF, 1 = HPF, 2 = BPF, 3 = Notch)
    // ep[2]: Resonance (Q * 100) OR Bandwidth in Hz
    // ep[3]: Unit Mode:
    //        0 = Standard Q mode (ep[2] is Q * 100, ex: 70 = 0.7, 100 = 1.0, 450 = 4.5)
    //        1 = Bandwidth in Hz mode (ep[2] is BW in Hz, ex: 50 = 50Hz, 200 = 200Hz)
    // ep[4]: Dry/Wet Mix (0 or 255 = 100% Wet, 1 to 254 = Dry/Wet blend)
    static void FX_BiquadFilterFast(int32_t* busBuffer, int samples, int16_t* ep, int32_t* es) {
        int32_t fc = ep[0];
        if (fc < 20) fc = 20;
        else if (fc > 20000) fc = 20000;

        int16_t filterMode = ep[1];
        int32_t q_param = ep[2];
        int16_t bwMode = ep[3];
        int16_t mix = ep[4];
        if (mix <= 0 || mix > 255) mix = 255;

        int32_t idx = (fc * 32) / 375;
        if (idx > 2047) idx = 2047;

        int32_t sin_w0 = sineLUT[idx];
        int32_t cos_w0 = sineLUT[(idx + 1024) & 4095];

        int32_t alpha;
        if (bwMode == 1) {
            int32_t bw_hz = (q_param < 1) ? 1 : q_param;
            alpha = (int32_t)(((int64_t)sin_w0 * bw_hz) / ((int64_t)fc << 1));
        } else {
            int32_t q_fixed = (q_param < 10) ? 10 : q_param;
            alpha = (sin_w0 * 50) / q_fixed;
        }
        if (alpha < 1) alpha = 1;

        int32_t k_a0 = 32768 + alpha;
        int32_t k_a1 = -2 * cos_w0;
        int32_t k_a2 = 32768 - alpha;

        int32_t k_b0, k_b1, k_b2;
        if (filterMode == 1) {
            k_b1 = -(32768 + cos_w0);
            k_b0 = (32768 + cos_w0) >> 1;
            k_b2 = k_b0;
        } else if (filterMode == 2) {
            k_b0 = alpha;
            k_b1 = 0;
            k_b2 = -alpha;
        } else if (filterMode == 3) {
            k_b0 = 32768;
            k_b1 = -2 * cos_w0;
            k_b2 = 32768;
        } else {
            k_b1 = 32768 - cos_w0;
            k_b0 = k_b1 >> 1;
            k_b2 = k_b0;
        }

        int32_t c_b0 = (int32_t)(((int64_t)k_b0 << 24) / k_a0);
        int32_t c_b1 = (int32_t)(((int64_t)k_b1 << 24) / k_a0);
        int32_t c_b2 = (int32_t)(((int64_t)k_b2 << 24) / k_a0);
        int32_t c_a1 = (int32_t)(((int64_t)k_a1 << 24) / k_a0);
        int32_t c_a2 = (int32_t)(((int64_t)k_a2 << 24) / k_a0);

        int32_t x1 = es[0];
        int32_t x2 = es[1];
        int32_t y1 = es[2];
        int32_t y2 = es[3];

        if (mix == 255) {
            for (int i = 0; i < samples; i++) {
                int32_t x0 = busBuffer[i];
                int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2)
                            - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                int32_t y0 = (int32_t)(acc >> 24);

                y0 -= (y0 >> 9);
                if (UNLIKELY(y0 > 262143)) y0 = 262143;
                else if (UNLIKELY(y0 < -262143)) y0 = -262143;

                x2 = x1; x1 = x0;
                y2 = y1; y1 = y0;
                busBuffer[i] = y0;
            }
        } else {
            for (int i = 0; i < samples; i++) {
                int32_t x0 = busBuffer[i];
                int64_t acc = ((int64_t)c_b0 * x0) + ((int64_t)c_b1 * x1) + ((int64_t)c_b2 * x2)
                            - ((int64_t)c_a1 * y1) - ((int64_t)c_a2 * y2);
                int32_t y0 = (int32_t)(acc >> 24);

                y0 -= (y0 >> 9);
                if (UNLIKELY(y0 > 262143)) y0 = 262143;
                else if (UNLIKELY(y0 < -262143)) y0 = -262143;

                x2 = x1; x1 = x0;
                y2 = y1; y1 = y0;
                busBuffer[i] = x0 + (((y0 - x0) * mix) >> 8);
            }
        }

        es[0] = x1;
        es[1] = x2;
        es[2] = y1;
        es[3] = y2;
    }
};