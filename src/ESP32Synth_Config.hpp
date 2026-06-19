// Core Synth Limits

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
    between polyphony and performance.
*/

// /* // Default:
#define MAX_VOICES      80   // Max simultaneous voices. (CPU/RAM limited, see note above).
#define MAX_WAVETABLES  50   // Default 50
#define MAX_SAMPLES     50   // Default 50
#define MAX_ARP_NOTES   16   // Default 16
#define MAX_STREAMS      4   // Max concurrent SD streams (RAM/CPU limited).
#define STREAM_BUF_SAMPLES 2048 // Ring buffer size (Must be power of 2).
// */

 /* // Low RAM usage (for LVGL or other high-memory libs / tasks):
#define MAX_VOICES         1
#define MAX_WAVETABLES     1
#define MAX_SAMPLES        1
#define MAX_ARP_NOTES      7
#define MAX_STREAMS        1
#define STREAM_BUF_SAMPLES 2048
// */


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

    It's a joke, please don't take it seriously (works but low polyphony, not recommended):
    [You have something against the ESP32's CPU] LEN: 32 | COUNT: 2 (ESP32: "INCREASE THE BUFFER, THIS IS TORTURE!!!")
    [You want to see the limits of the ESP32]    LEN: 16 | COUNT: 2 ("Results are in and your ESP32 is in a coma.")
*/

#ifndef SYNTH_DMA_BUF_LEN
#define SYNTH_DMA_BUF_LEN 512
#endif

#ifndef SYNTH_DMA_BUF_COUNT
#define SYNTH_DMA_BUF_COUNT 6
#endif

// ====================================================================================
//    SINE WAVE LOOK-UP TABLE
// ====================================================================================
#define SINE_LUT_SIZE 4096
#define SINE_LUT_MASK (SINE_LUT_SIZE - 1)
#define SINE_SHIFT    20

// Shared sine LUT
extern int16_t sineLUT[SINE_LUT_SIZE];

#define STREAM_BUF_MASK (STREAM_BUF_SAMPLES - 1)
#define ENV_MAX 268435456

#endif // ESP32_SYNTH_CONFIG_HPP