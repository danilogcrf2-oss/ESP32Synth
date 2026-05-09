// Core Synth Limits

#pragma once
#ifndef ESP32_SYNTH_CONFIG_HPP
#define ESP32_SYNTH_CONFIG_HPP
/*
 Para testar e confirmar a polifonia maxima no seu ESP32,
 aumente o MAX_VOICES e deixe o resto todo em 0 para não 
 ocupar a ram nessesária para as vozes.
 eu só encontrei esse problema no ESP32-DOWD-Q6, não no s3,
 mas é bom deixar um aviso aqui.
*/
/* Maximum Tested MAX_VOICES Values (ESP32 & ESP32-S3):

  ESP32:
   ram safe max 140,
   default 80, 
   max 340, 
   jitter and FreeRTOS no time for tasks 346.

   ESP32-S3:
   ram safe max 150,
   default 80,
   max 500,
   jitter and FreeRTOS no time for tasks 515.

    Note: These limits are based on testing with the default settings and may vary depending on the complexity of the patches, use of wavetables, samples, and SD streaming. Always test with your specific use case to find the optimal balance between polyphony and performance.

*/

// /* // default:
#define MAX_VOICES 80 // Max simultaneous voices. (CPU/RAM limited, see note above).
#define MAX_WAVETABLES 50 // default 50
#define MAX_SAMPLES 50 // default 50
#define MAX_ARP_NOTES 16 // default 16 
#define MAX_STREAMS 4 // Max concurrent SD streams (RAM/CPU limited).
#define STREAM_BUF_SAMPLES 2048 // Ring buffer size (Must be power of 2).
// */ 

 /* // Low ram usage (for LVGL, hi ram usage libs):
#define MAX_VOICES 1 
#define MAX_WAVETABLES 1 
#define MAX_SAMPLES 1
#define MAX_ARP_NOTES 3 
#define MAX_STREAMS 1 
#define STREAM_BUF_SAMPLES 2048 
// */



/*
    DMA buffers to control latency vs polyphony:
    - Smaller buffers reduce latency but increase CPU load and risk of audio dropouts if processing can't keep up.
    - Larger buffers allow for more stable audio at high polyphony or complex processing but increase latency.

    delay (ms) = (buf_len * buf_count) / sample_rate * 1000

    [Default] LEN: 512 | COUNT: 6 (high polyphony, a little delay)
    [Balance] LEN: 256 | COUNT: 4 (Good for most MIDI keyboards)
    [LIVE]    LEN: 128 | COUNT: 2 (imperceptible latency, little bit less polyphony)

    [You have an audio analyzer in mind] LEN: 64 | COUNT: 2 (Impressive!)

    It's a joke, please don't take it seriously (works but low polifony, not recommended, but hey, it's your synth!):
    [You have something against the ESP32'CPU] LEN: 32 | COUNT: 2 (ESP32: "INCREASE THE BUFFER, THIS IS TORTURE!!!")
    [You want to see the limits of the ESP32] LEN: 16 | COUNT: 2 ("the results are out and your ESP32 is in a coma.")
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