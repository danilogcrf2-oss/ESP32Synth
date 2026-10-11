/* ====================================================================================
   ESP32Synth_FLT.hpp
   Float Translator (FLT) & Fixed-Point (FIP) Mathematical Engine for ESP32Synth
   Translates floating-point synthesizer concepts into fast 32-bit integer DSP.
   Format: Q8.24 (8-bit integer, 24-bit fractional). Numerical Range: ~ -128.0 to 127.999
   ====================================================================================
*/

/*
    ================================================================================================
    HOW TO USE THE FLT/FIP ENGINE (The 3 Golden Rules)
    ================================================================================================
    The `fip` class is a transparent translator. It allows you to write DSP code using standard 
    floating-point logic while the CPU executes everything in ultra-fast 32-bit hardware integers. 
    You do NOT need to know bit-shifting or fixed-point math to use it. Just write normal math!
    
    However, to prevent audio glitches and CPU spikes, you must follow these 3 Golden Rules:

    --- RULE 1: THE Q8.24 RANGE LIMIT (-128.0 to 127.999) ---
    The engine uses the Q8.24 format. This means the absolute maximum value a `fip` can hold is 
    around 127.9. Audio signals usually stay between -1.0 and 1.0, and modulation/drive parameters 
    usually range from 0.0 to 10.0, which is perfectly safe.
    WARNING: Never put raw Frequency values (e.g., 48000.0 or 440.0) into a `fip`! It will overflow.
    SOLUTION: Always work with Normalized Ratios (Hz / SampleRate) which fit safely between 0.0 and 0.5.
        [WRONG] fip cutoff = fip(4000.0f); 
        [RIGHT] fip cutoffRatio = fip(4000.0f / 48000.0f); // Equals 0.083f (Safe!)

    --- RULE 2: ALWAYS WRAP YOUR FLOATS ---
    Never mix naked floats with `fip` objects in your math. Always wrap your float constants in `fip()`.
    Thanks to GCC optimizations, wrapping a literal float (e.g., fip(0.5f)) costs ZERO CPU cycles 
    at runtime, as the compiler converts it to an integer during compilation.
        [WRONG] fip result = mySignal * 0.5f;        // Forces slow FPU division at runtime!
        [RIGHT] fip result = mySignal * fip(0.5f);   // 100% pure integer math at runtime!

    --- RULE 3: THE I/O GATEWAYS (Bridging the Synth and FIP) ---
    The core synth engine operates in raw 16-bit/32-bit hardware integers. You must translate 
    the inputs when they enter your custom block, and translate the output before leaving.
        - INPUTS: Use `fip::fromPhase(vo->phase)`, `fip::fromParam(val, max_val)`, `fip::fromEnv(env)`.
        - OUTPUT: Use `.toAudio16()` to safely return the signal to the mixBuffer.

    Example of a perfect custom oscillator:
    ------------------------------------------------------------------------------------------------
    void myCustomOsc(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
        fip phase = fip::fromPhase(vo->phase);                // Gate IN: Engine Phase -> 0.0 to 1.0
        fip inc   = fip::fromPhase(vo->phaseInc);             
        fip lfoAmount = fip::fromParam(vo->cp[0], 100);       // Gate IN: LFO (0-100) -> 0.0 to 1.0
        
        for (int i = 0; i < samples; i++) {
            fip myWave = fip::sin(phase); // Phase is already normalized (0.0, 1.0)
            
            // ... apply your DSP math here ...
            
            mixBuffer[i] += (myWave.toAudio16() * volume) >> 16;  // Gate OUT: fip -> Audio Int16
            phase += inc;
        }
        vo->phase = (uint32_t)(phase.val << 8);               // Save phase back to engine
    }
    ================================================================================================
*/

#pragma once
#include <stdint.h>
#include <math.h>
#include "ESP32Synth.h"

#define FIP_SHIFT 24
#define FIP_ONE   (1 << FIP_SHIFT)
#define FIP_HALF  (1 << (FIP_SHIFT - 1))
#define FIP_FRAC_MASK 0xFFFFFF

// ====================================================================================
// 1. CORE FIXED-POINT CLASS (fip)
// ====================================================================================
class fip {
public:
    int32_t val;

    // Constructors
    inline __attribute__((always_inline)) fip() : val(0) {}
    inline __attribute__((always_inline)) fip(int32_t raw, bool isRaw) : val(raw) {}
    inline __attribute__((always_inline)) fip(float f_val) : val((int32_t)(f_val * 16777216.0f)) {}
    inline __attribute__((always_inline)) fip(double d_val) : val((int32_t)(d_val * 16777216.0)) {}
    inline __attribute__((always_inline)) fip(int i_val) : val(i_val << FIP_SHIFT) {}

    // Mathematical Constants (Exact Q8.24 Calibration)
    static inline __attribute__((always_inline)) fip pi()      { return fip(52707179, true); }   
    static inline __attribute__((always_inline)) fip two_pi()  { return fip(105414357, true); }  
    static inline __attribute__((always_inline)) fip half_pi() { return fip(26353589, true); }   

    // Fundamental Arithmetic Operators
    inline __attribute__((always_inline)) fip operator+(const fip& o) const { return fip(val + o.val, true); }
    inline __attribute__((always_inline)) fip operator-(const fip& o) const { return fip(val - o.val, true); }
    inline __attribute__((always_inline)) fip operator*(const fip& o) const { return fip((int32_t)(((int64_t)val * o.val) >> FIP_SHIFT), true); }
    inline __attribute__((always_inline)) fip operator/(const fip& o) const {
        if (o.val == 0) return fip(0x7FFFFFFF, true);
        return fip((int32_t)((((int64_t)val) << FIP_SHIFT) / o.val), true);
    }
    inline __attribute__((always_inline)) fip operator%(const fip& o) const {
        if (o.val == 0) return fip(0, true);
        return fip(val % o.val, true);
    }
    inline __attribute__((always_inline)) fip operator-() const { return fip(-val, true); }

    // Bitwise & Shift Operators
    inline __attribute__((always_inline)) fip operator&(const fip& o) const { return fip(val & o.val, true); }
    inline __attribute__((always_inline)) fip operator|(const fip& o) const { return fip(val | o.val, true); }
    inline __attribute__((always_inline)) fip operator<<(const int shift) const { return fip(val << shift, true); }
    inline __attribute__((always_inline)) fip operator>>(const int shift) const { return fip(val >> shift, true); }

    // Compound Assignments
    inline __attribute__((always_inline)) fip& operator=(const fip& o)  { val = o.val; return *this; }
    inline __attribute__((always_inline)) fip& operator+=(const fip& o) { val += o.val; return *this; }
    inline __attribute__((always_inline)) fip& operator-=(const fip& o) { val -= o.val; return *this; }
    inline __attribute__((always_inline)) fip& operator*=(const fip& o) { val = (int32_t)(((int64_t)val * o.val) >> FIP_SHIFT); return *this; }
    inline __attribute__((always_inline)) fip& operator/=(const fip& o) {
        if (o.val != 0) val = (int32_t)((((int64_t)val) << FIP_SHIFT) / o.val);
        return *this;
    }

    // Comparison Operators
    inline __attribute__((always_inline)) bool operator>(const fip& o)  const { return val > o.val; }
    inline __attribute__((always_inline)) bool operator<(const fip& o)  const { return val < o.val; }
    inline __attribute__((always_inline)) bool operator>=(const fip& o) const { return val >= o.val; }
    inline __attribute__((always_inline)) bool operator<=(const fip& o) const { return val <= o.val; }
    inline __attribute__((always_inline)) bool operator==(const fip& o) const { return val == o.val; }
    inline __attribute__((always_inline)) bool operator!=(const fip& o) const { return val != o.val; }

    inline float toFloat() const { return (float)val / 16777216.0f; }

    // Core Fast DSP Functions (Safe for 48kHz audio render loops)
    static inline __attribute__((always_inline)) fip sin(fip phase) {
        uint32_t idx = ((uint32_t)phase.val >> (FIP_SHIFT - 12)) & SINE_LUT_MASK;
        return fip((int32_t)sineLUT[idx] << 9, true);
    }

    static inline __attribute__((always_inline)) fip cos(fip phase) {
        uint32_t idx = (((uint32_t)phase.val >> (FIP_SHIFT - 12)) + (SINE_LUT_SIZE / 4)) & SINE_LUT_MASK;
        return fip((int32_t)sineLUT[idx] << 9, true);
    }

    static inline __attribute__((always_inline)) fip tan(fip phase) {
        fip cos_val = cos(phase);
        if (cos_val.val == 0) return fip(0x7FFFFFFF, true);
        return sin(phase) / cos_val;
    }

    static inline __attribute__((always_inline)) fip abs(fip x) {
        int32_t mask = x.val >> 31;
        return fip((x.val + mask) ^ mask, true);
    }

    static inline __attribute__((always_inline)) fip clamp(fip x, fip minVal, fip maxVal) {
        if (x.val > maxVal.val) return maxVal;
        if (x.val < minVal.val) return minVal;
        return x;
    }

    static inline __attribute__((always_inline)) fip min(fip v0, fip v1) { return (v0.val < v1.val) ? v0 : v1; }
    static inline __attribute__((always_inline)) fip max(fip v0, fip v1) { return (v0.val > v1.val) ? v0 : v1; }
    static inline __attribute__((always_inline)) fip lerp(fip v0, fip v1, fip t_val) { return v0 + ((v1 - v0) * t_val); }
    static inline __attribute__((always_inline)) fip floor(fip x) { return fip(x.val & ~FIP_FRAC_MASK, true); }
    static inline __attribute__((always_inline)) fip frac(fip x) { return fip(x.val & FIP_FRAC_MASK, true); }

    static inline __attribute__((always_inline)) fip sqrt(fip x) {
        if (x.val <= 0) return fip(0, true);
        uint32_t rem = 0, root = 0, v = x.val;
        for (int i = 0; i < 16; i++) {
            root <<= 1;
            rem = ((rem << 2) + (v >> 30));
            v <<= 2;
            root++;
            if (root <= rem) { rem -= root; root++; }
            else { root--; }
        }
        return fip((int32_t)(root << 11), true);
    }

    static inline __attribute__((always_inline)) fip fast_tanh(fip x) {
        // Fast rational Pade approximation: x * (27 + x^2) / (27 + 9*x^2)
        fip safe_x = clamp(x, fip(-50331648, true), fip(50331648, true)); // Clamp to [-3.0, 3.0]
        fip x2 = safe_x * safe_x;
        fip k27 = fip(452984832, true); // Constant 27.0 in Q8.24
        fip k9  = fip(150994944, true); // Constant 9.0 in Q8.24
        fip num = safe_x * (k27 + x2);
        fip den = k27 + (k9 * x2);
        return num / den;
    }

    // Heavy Functions (Setup/Init only. Avoid using inside the Audio Render Task!)
    static inline fip pow_f(fip base, fip exp) { return fip(::powf(base.toFloat(), exp.toFloat())); }
    static inline fip exp_f(fip x) { return fip(::expf(x.toFloat())); }
    static inline fip log_f(fip x) { return fip(::logf(x.toFloat())); }

    // Inter-Engine Data Converters
    inline __attribute__((always_inline)) int16_t toAudio16() const {
        int32_t out = val >> (FIP_SHIFT - 15);
        if (out > 32767) return 32767;
        if (out < -32768) return -32768;
        return (int16_t)out;
    }

    static inline __attribute__((always_inline)) fip fromPhase(uint32_t synthPhase) {
        return fip((int32_t)(synthPhase >> 8), true);
    }

    // Pass maxValue as a literal number (e.g., 100) to allow GCC to optimize out the division.
    static inline __attribute__((always_inline)) fip fromParam(int16_t paramVal, int16_t maxValue = 1) {
        if (maxValue == 1) return fip((int32_t)paramVal << FIP_SHIFT, true);
        return fip((int32_t)((((int64_t)paramVal) << FIP_SHIFT) / maxValue), true);
    }

    static inline __attribute__((always_inline)) fip fromEnv(uint32_t envVal) {
        return fip((int32_t)(envVal >> 4), true); // Maps ENV_MAX (2^28) to 1.0 (2^24)
    }
};

// ====================================================================================
// 2. THE BAKER (Pre-Compilation Wavetable Engine)
// ====================================================================================
class FLT_Baker {
public:
    typedef fip (*FipWaveCallback)(fip phase);

    /**
     * @brief Pre-calculates and registers custom mathematical waveforms directly into synth RAM.
     * @param synth Pointer to the main ESP32Synth instance.
     * @param tableId Slot index (0 to MAX_WAVETABLES - 1).
     * @param cb Mathematical wave formula callback receiving a normalized phase (0.0 to 1.0).
     * @param size Number of sample points (Default: SINE_LUT_SIZE).
     * @param depth Bit depth resolution: BITS_16 (highest quality), BITS_8 (50% RAM), BITS_4 (75% RAM).
     * @return const void* Pointer to the allocated wavetable memory in internal RAM.
     */
    static const void* bakeWavetable(ESP32Synth* synth, uint16_t tableId, FipWaveCallback cb, uint32_t size = SINE_LUT_SIZE, BitDepth depth = BITS_16) {
        if (!synth || !cb || tableId >= MAX_WAVETABLES || size == 0) return nullptr;

        void* bakedData = nullptr;

        if (depth == BITS_16) {
            int16_t* data16 = (int16_t*)heap_caps_malloc(size * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!data16) return nullptr;
            for (uint32_t i = 0; i < size; i++) {
                fip phase((float)i / (float)size);
                data16[i] = cb(phase).toAudio16();
            }
            bakedData = data16;
        } 
        else if (depth == BITS_8) {
            uint8_t* data8 = (uint8_t*)heap_caps_malloc(size * sizeof(uint8_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!data8) return nullptr;
            for (uint32_t i = 0; i < size; i++) {
                fip phase((float)i / (float)size);
                int32_t val = cb(phase).toAudio16();
                data8[i] = (uint8_t)((val >> 8) + 128); // Maps signed [-32768, 32767] to unsigned [0, 255]
            }
            bakedData = data8;
        } 
        else if (depth == BITS_4) {
            uint32_t bytesNeeded = (size + 1) / 2;
            uint8_t* data4 = (uint8_t*)heap_caps_calloc(bytesNeeded, sizeof(uint8_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!data4) return nullptr;

            for (uint32_t i = 0; i < size; i++) {
                fip phase((float)i / (float)size);
                int32_t val = cb(phase).toAudio16();
                uint8_t nibble = (uint8_t)(((val >> 12) + 8) & 0x0F); // Maps to 4-bit unsigned [0, 15]

                uint32_t byteIdx = i >> 1;
                if ((i & 1) == 0) {
                    data4[byteIdx] = (data4[byteIdx] & 0xF0) | nibble;
                } else {
                    data4[byteIdx] = (data4[byteIdx] & 0x0F) | (nibble << 4);
                }
            }
            bakedData = data4;
        }

        synth->registerWavetable(tableId, bakedData, size, depth);
        return bakedData;
    }
};