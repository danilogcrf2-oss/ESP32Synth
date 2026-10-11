// Core Synth Limits
// Any values ​​changed here can positively or negatively impact performance.
// Changing them is not dangerous; choose your ideal values.
#pragma once
#include <stdint.h>
#ifndef ESP32_SYNTH_CONFIG_HPP
#define ESP32_SYNTH_CONFIG_HPP

/*
    To test and confirm the maximum polyphony on your ESP32,
    increase MAX_VOICES and set everything else to 0 so it
    doesn't consume the RAM needed for the voices.
    This issue was only found on the ESP32-DOWD-Q6, not on the S3,
    but it's good to leave a note here.
*/

/* Maximum Tested MAX_VOICES Values (ESP32 & ESP32-S3):

    ESP32:
     RAM-safe max: 140
     Default:       80
     Absolute max: 340
     FreeRTOS task starvation / jitter at: 346

    ESP32-S3:
     RAM-safe max: 150
     Default:       80
     Absolute max: 500
     FreeRTOS task starvation / jitter at: 515

    Note: These limits are based on testing with the default settings and may vary
    depending on the complexity of the patches, use of wavetables, samples, and SD
    streaming. Always test with your specific use case to find the optimal balance
    between polyphony and performance. And with each voice, more memory is allocated.
*/

// /* // Default:
#define MAX_VOICES      80   // Max simultaneous voices. (CPU/RAM limited, see note above).
#define MAX_WAVETABLES  20   // Default 20
#define MAX_SAMPLES     20   // Default 20
#define MAX_ARP_NOTES   16   // Default 16
#define MAX_STREAMS      4   // Max concurrent SD streams (RAM/CPU limited).
#define STREAM_BUF_SAMPLES 4096 // Ring buffer size (Must be power of 2).
// */

 /* // bare minimum / Low RAM usage (for LVGL or other high-memory libs / tasks):
#define MAX_VOICES         1
#define MAX_WAVETABLES     1
#define MAX_SAMPLES        1
#define MAX_ARP_NOTES      7
#define MAX_STREAMS        1
#define STREAM_BUF_SAMPLES 2048
// */

/*
  ====================================================================================
   CUSTOM SYNTHESIS PARAMETERS
   Increasing these values allows for more complex custom waves/DSPs, 
   but increases the RAM footprint per voice. Default values are highly optimized.
  ====================================================================================
*/ 
#ifndef SYNTH_CUSTOM_WAVE_STATES
#define SYNTH_CUSTOM_WAVE_STATES      6 // cw array size (uint32_t). 6 keeps the Union at exactly 24 bytes.
#endif

#ifndef SYNTH_CUSTOM_PARAMS_PER_VOICE
#define SYNTH_CUSTOM_PARAMS_PER_VOICE 8 // cp array size (int16_t).
#endif

#ifndef SYNTH_CUSTOM_PARAMS_GLOBAL
#define SYNTH_CUSTOM_PARAMS_GLOBAL    4 // dp array size (int16_t).
#endif

/*
  ====================================================================================
   MODULAR FX ROUTING & BUSES
  ====================================================================================
*/
#ifndef MAX_BUSES
#define MAX_BUSES         4  // 0 = Master Dry, 1+ = FX Buses. (Keep it low to save RAM)
#endif

#ifndef MAX_FX_PER_BUS
#define MAX_FX_PER_BUS    3  // Max effect slots chained per bus
#endif

#ifndef SYNTH_FX_PARAMS
#define SYNTH_FX_PARAMS   8  // Effect Parameters array size (ep)
#endif

#ifndef SYNTH_FX_STATES
#define SYNTH_FX_STATES   6  // Effect States array size (es)
#endif

// --- SD Recording Buffer ---
// 16384 samples = ~32KB of RAM (Only allocated while recording!)
// MUST be a power of 2 for extremely fast bitwise mask wrap-around.
#ifndef RECORD_BUF_SAMPLES
#define RECORD_BUF_SAMPLES 16384 
#define RECORD_BUF_MASK    (RECORD_BUF_SAMPLES - 1)
#endif

/*
  ====================================================================================
   HELIX MP3 STREAMING ENGINE (Fixed-Point, Zero-Floats)
   Enables on-the-fly MP3 streaming directly from PROGMEM or SD Card on Stream core.
   Preserves 100% of polyphony on DSP core.
  ====================================================================================
*/
#ifndef ENABLE_MP3
#define ENABLE_MP3 1
#endif
#ifndef SYNTH_MP3_IN_BUF_SIZE
#define SYNTH_MP3_IN_BUF_SIZE 2048
#endif

/*
    DMA buffers to control latency vs polyphony:
    - Smaller buffers reduce latency but increase CPU load and risk of audio dropouts
      if processing can't keep up.
    - Larger buffers allow for more stable audio at high polyphony or complex processing,
      but increase latency.

    Latency formula (ms) = (buf_len * buf_count) / sample_rate * 1000

    [Default] LEN: 512 | COUNT: 6  (high polyphony, slight delay)
    [Balance] LEN: 256 | COUNT: 4  (good for most MIDI keyboards)
    [LIVE]    LEN: 128 | COUNT: 2  (imperceptible latency, slightly less polyphony)

    [You have an audio analyzer in mind] LEN: 64 | COUNT: 2 (Impressive!)

    It's a joke, please don't take it seriously (works but possible low polyphony, not recommended):
    [You have something against the ESP32's CPU] LEN: 32 | COUNT: 2 (ESP32: "INCREASE THE BUFFER, THIS IS TORTURE!!!")
    [You want to see the limits of the ESP32]    LEN: 16 | COUNT: 2 ("Results are in and your ESP32 is in a coma.")
*/

#ifndef SYNTH_DMA_BUF_LEN
#define SYNTH_DMA_BUF_LEN 512
#endif

#ifndef SYNTH_DMA_BUF_COUNT
#define SYNTH_DMA_BUF_COUNT 6
#endif

// Core Task Pinning
// Stream / Decoding core
#define SYNTH_STREAM_TASK_CORE 0
#define SYNTH_SD_TASK_CORE     SYNTH_STREAM_TASK_CORE // Backward compatibility
// DSP core
#define SYNTH_AUDIO_TASK_CORE  1

/* 
  ====================================================================================
      SINE WAVE LOOK-UP TABLE
  ====================================================================================
*/ 
#define SINE_LUT_SIZE 4096
#define SINE_LUT_MASK (SINE_LUT_SIZE - 1)
#define SINE_SHIFT    20

// 0 = Maximum Polyphony (300+ voices, truncated phase LUT)
// 1 = Studio Quality (Interpolated LUT, >130 dBc SFDR, pristine FM / Overdrive)
#ifndef SYNTH_INTERPOLATE_SINE
#define SYNTH_INTERPOLATE_SINE 1
#endif

// Shared sine LUT (+1 guard sample for zero-branch, single-cycle interpolation wrap)
extern int16_t sineLUT[SINE_LUT_SIZE + 1];

#define STREAM_BUF_MASK (STREAM_BUF_SAMPLES - 1)
#define ENV_MAX 268435456

#endif // ESP32_SYNTH_CONFIG_HPP