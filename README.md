# ESP32Synth v2.4.2 — Highly Optimized Bare-Metal Synth Engine for Embedded Polyphony

<p align="center">
  <img src="https://raw.githubusercontent.com/danilogcrf2-oss/ESP32Synth/main/banner.jpg" alt="ESP32Synth banner" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-2.4.2-green.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-ESP32 Family-orange.svg" alt="Platform">
  <img src="https://img.shields.io/badge/framework-Arduino%20%7C%20ESP--IDF-blue.svg" alt="Framework">
  <img src="https://img.shields.io/badge/license-MIT-yellow.svg" alt="License">
</p>

A high-performance, polyphonic audio synthesis library for the ESP32 series (including S3). Engineered for extreme bare-metal optimization, zero-latency rendering, massive voice density, custom DSP hooks, and direct filesystem/SD-card streaming. Dual-framework support ensures compilation in both Arduino IDE and VS Code (PlatformIO) under either Arduino or native ESP-IDF.

---

## 📖 Table of Contents

1. [Architectural Philosophy: Why 500 Voices?](#1-architectural-philosophy-why-500-voices)
2. [PlatformIO (VS Code) & ESP-IDF Integration](#2-platformio-vs-code--esp-idf-integration)
3. [Memory Footprint & Hardware Isolation](#3-memory-footprint--hardware-isolation)
4. [Unified API Reference](#4-unified-api-reference)
5. [The Power of `SMODE_PWM` (LEDC Bare-Metal Audio)](#5-the-power-of-smode_pwm-ledc-bare-metal-audio)
6. [Dual-Framework Filesystem Streaming (SD Card)](#6-dual-framework-filesystem-streaming-sd-card)
7. [External Protocol Pull Mode (A2DP Bluetooth & Wi-Fi)](#7-external-protocol-pull-mode-a2dp-bluetooth--wi-fi)
8. [Fixed-Point Advanced DSP & Custom Synthesis Blocks](#8-fixed-point-advanced-dsp--custom-synthesis-blocks)
9. [Development Tools & Advanced Troubleshooting](#9-development-tools--advanced-troubleshooting)

---

## 1. Architectural Philosophy: Why 500 Voices?

The extreme polyphony achievements of ESP32Synth (300+ voices on classic ESP32 chips, up to 500 on ESP32-S3) are not merely for playback metrics. This density serves as a **mathematical proof of efficiency**. 

By eliminating `float` operations, hardware divisions, and branch instructions from the hot audio rendering path, we achieve extreme CPU headroom. This unused processing power allows developers to build highly complex synthesis blocks, such as:
* **6-Operator FM Synthesis** (emulating hardware like the Yamaha DX7)
* **Acoustic Physical Modeling** (string, waveguide, and drum-head modeling)
* **Adaptive Multi-Pole Resonant Filters**
* **Dynamic Waveshaping & Phase Distortion Engines**

To implement these blocks, you must maintain this performance philosophy: **use strictly 16.16 or 32.32 fixed-point math, look-up tables (LUTs), and bitwise shifts (`>>`)**.

---

## 2. PlatformIO (VS Code) & ESP-IDF Integration

With **v2.4.2**, PlatformIO integration is native. File system abstractions are unified, allowing you to run identical synth files under both Arduino and ESP-IDF frameworks.

### PlatformIO Configuration (`platformio.ini`)

For **Arduino Framework**:
```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
build_flags = 
    -O3
    -funroll-loops
```

For **ESP-IDF Framework**:
Make sure to add ESP32Synth to your project's components or `src` directory.
```ini
[env:esp32s3-idf]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 115200
build_flags =
    -O3
    -funroll-loops
```

---

## 3. Memory Footprint & Hardware Isolation

### Voice Structure Optimization
To maximize RAM availability, ESP32Synth employs an extreme structure alignment strategy. Mutual exclusion is achieved via an explicit `union` block inside the `Voice` structure:

```cpp
struct Voice {
    int64_t slideVolCurr; // 8-byte alignment for fast Xtensa pipeline execution
    int64_t slideVolInc;

    union {
        // Mode: WAVE_SAMPLE & WAVE_STREAM
        struct {
            uint64_t samplePos1616;
            uint32_t sampleInc1616;
            uint32_t sampleLoopStart;
            uint32_t sampleLoopEnd;
            uint32_t streamFracAccum;
        };
        // Mode: WAVE_WAVETABLE
        struct {
            const void* wtData;
            uint32_t    wtSize;
        };
        // Mode: WAVE_CUSTOM
        uint32_t cw[6]; 
    };
    // ...
};
```
This union guarantees that regardless of your voice configuration, the core footprint of each voice does not exceed memory constraints, keeping cache misses at an absolute minimum.

---

## 4. Unified API Reference

The engine dynamically switches compiler directives to accommodate Arduino or native C filesystems.

### 1. Engine Initialization

The library supports several physical output layouts. Choose the initialization method that corresponds to your hardware routing:

```cpp
#include "ESP32Synth.h"

ESP32Synth synth;

void setup_audio() {
    // Standard I2S Mode (External DAC like PCM5102A - BCK, WS, DATA)
    // Parameters: dataPin, mode, clkPin, wsPin, BitDepth
    synth.begin(4, 15, 2, I2S_32BIT);

    // Or: Single-Pin Hardware PWM Mode (10-bit audio on pin 25)
    // synth.begin(25, SMODE_PWM, -1, -1, I2S_16BIT);

    // Or: PDM Mode (High-Frequency 1-bit oversampled audio on pin 2)
    // synth.begin(2, SMODE_PDM, 4, -1, I2S_16BIT);

    // Set engine-wide volume (0-255 scaling)
    synth.setMasterVolume(255);
}
```

### 2. Basic Voice & Pitch Control

Pitch is controlled in hundredths of a Hz ("CentiHz") to achieve fine tuning without utilizing slow floating-point types.

```cpp
// Triggers voice 0 at C4 (Middle C), Volume 255
synth.noteOn(0, c4, 255);

// Update frequency and pulse-width dynamically
synth.setFrequency(0, cs4); // Shift pitch up to C#4
synth.setWave(0, WAVE_PULSE);
synth.setPulseWidth(0, 128); // 50% square duty cycle (0-255 scale)

// Set custom bitcrush resolution (0-32 bits, 0 means disabled)
synth.setMasterBitcrush(8); // Lo-fi 8-bit output reduction

// Triggers envelope release stage
synth.noteOff(0);
```

### 3. Modulations, Slides, and Arpeggios

We use Bresenham's algorithm for pitch slides to perform high-resolution portamento without hardware divisions inside the control rate routine.

```cpp
// Per-voice ADSR (Attack: 10ms, Decay: 150ms, Sustain Lvl: 120, Release: 1200ms)
synth.setEnv(0, 10, 150, 120, 1200);

// Vibrato (Frequency Modulation): LFO Rate 6.5Hz (650 cHz), LFO Depth 30Hz (3000 cHz)
synth.setVibrato(0, 650, 3000);

// Tremolo (Amplitude Modulation): LFO Rate 4Hz (400 cHz), LFO Depth 80
synth.setTremolo(0, 400, 80);

// Slide pitch to C5 over exactly 500 milliseconds
synth.slideFreqTo(0, c5, 500);

// Multi-step Arpeggiator (Voice 0, Step duration: 120ms, Notes: C4, E4, G4, C5)
synth.setArpeggio(0, 120, c4, e4, g4, c5);
```

---

## 5. The Power of `SMODE_PWM` (LEDC Bare-Metal Audio)

No external DAC? No problem. In version 2.4.2, the PWM mode (`SMODE_PWM`) runs completely decoupled from traditional timers. We attach our interrupt handler (`ledc_ovf_isr`) directly to the LEDC timer's hardware overflow event:

```cpp
// From ESP32Synth_Begins.hpp
esp_intr_alloc(ETS_LEDC_INTR_SOURCE, ESP_INTR_FLAG_IRAM, ledc_ovf_isr, this, (intr_handle_t*)&pwm_timer);
```

### Auto-Synchronized LEDC ISR
The interrupt handler is written in high-priority Assembly-level IRAM, directly feeding duty-cycle updates to hardware registers. This bypasses FreeRTOS scheduling overhead completely:

```cpp
#if defined(CONFIG_IDF_TARGET_ESP32)
    // Classic ESP32 - High Speed Channel 0 Timer 0
    if (int_st & LEDC_HSTIMER0_OVF_INT_ST) {
        REG_WRITE(LEDC_INT_CLR_REG, LEDC_HSTIMER0_OVF_INT_CLR);
        if (synth->_running && synth->pwm_ping_pong_buf[synth->pwm_active_buf]) {
            int16_t sample = synth->pwm_ping_pong_buf[synth->pwm_active_buf][synth->pwm_read_idx];
            synth->pwm_read_idx = synth->pwm_read_idx + 1;
            uint32_t duty_val = ((uint32_t)(sample + 32768) >> 6) << 4; // Precise 10-bit shift
            REG_WRITE(LEDC_HSCH0_DUTY_REG, duty_val);
            REG_WRITE(LEDC_HSCH0_CONF1_REG, REG_READ(LEDC_HSCH0_CONF1_REG) | (1U << 31)); // Hardware Latch
        }
    }
#endif
```
This is the closest you can get to dedicated hardware performance, producing a clean carrier frequency locked to **47,962 Hz** with 10-bit duty cycle resolution.

---

## 6. Dual-Framework Filesystem Streaming (SD Card)

ESP32Synth v2.4.2 natively translates filesystem calls based on the active compiler toolchain.

### Arduino Framework Stream (Uses `fs::FS`)
```cpp
#ifdef ARDUINO
#include <SD.h>
#include <SPI.h>

void play_background_track() {
    // Voice, FS Handle, Filepath, Volume, RootPitch, Loop
    synth.playStream(1, SD, "/ambient_music.wav", 255, c4, true);
    
    // Position/Loop controls
    synth.setStreamLoopPointsMs(1, 2000, 24000); // Loops segment between 2s and 24s
}
#endif
```

### Native ESP-IDF Stream (Uses Standard C `FILE*` and VFS)
```cpp
#ifndef ARDUINO
void play_background_track_idf() {
    // ESP-IDF abstracts the filesystem using POSIX. Pass the direct path.
    synth.playStream(1, "/sdcard/ambient_music.wav", 255, c4, true);
}
#endif
```

The underlying file IO decoder runs on Core 0 inside a lower-priority background thread, loading and feeding a **Ring Buffer** (`STREAM_BUF_SAMPLES`) to prevent SD card read stalls from blocking audio rendering.

---

## 7. External Protocol Pull Mode (A2DP Bluetooth & Wi-Fi)

To output audio over wireless connections (Bluetooth A2DP, ESP-NOW, or WebSockets), configure the engine in `SMODE_CUSTOM`. This turns off internal DMA timers and relies on a "Pull Mode" architecture.

```cpp
ESP32Synth synth;

void setup() {
    // Setup at 44.1kHz or 48kHz with no automatic timer (customOutput = nullptr)
    synth.beginCustom(44100, nullptr);
    synth.noteOn(0, c4, 255);
}

// Your wireless network or Bluetooth stack audio callback
void write_bluetooth_packet(uint8_t *stream_buffer, int buffer_length) {
    int samplePairs = buffer_length / 4; // Each 16-bit stereo frame is 4 bytes (L + R)

    // Under the hood, this converts, scales, and copies rendered frames directly
    synth.generateSamplesStereo((int16_t*)stream_buffer, samplePairs);
}
```

---

## 8. Fixed-Point Advanced DSP & Custom Synthesis Blocks

Inject complex physical effects and waveshapes into the engine using the global and local callback structures.

### Custom Voice Engine (FM 2-Operator Voice Block)
Write a completely new synthesis generator block, assign it to a specific voice, and use standard ADSR controls:

```cpp
// Highly optimized 2-Op FM Oscillator callback
void IRAM_ATTR fmTwoOpOscillator(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    
    uint32_t carrierPhase = vo->phase;
    uint32_t carrierInc = vo->phaseInc + vo->vibOffset;
    
    // Modulator tracks at 2.0x carrier frequency (simple harmonic relationship)
    uint32_t modulatorPhase = vo->cw[0];
    uint32_t modulatorInc = carrierInc * 2;
    int16_t prevSample = (int16_t)vo->cw[1]; // Feedback storage

    for (int i = 0; i < samples; i++) {
        // Modulator outputs sine wave with phase feedback (~12.5% scale)
        uint32_t feedbackPhase = modulatorPhase + (prevSample << 12);
        int32_t modSample = sineLUT[feedbackPhase >> SINE_SHIFT];
        prevSample = (int16_t)modSample;

        // Modulate Carrier Phase by Modulator Output
        uint32_t finalCarrierPhase = carrierPhase + (modSample * 16); // Mod index
        int32_t signal = sineLUT[(finalCarrierPhase >> SINE_SHIFT) & SINE_LUT_MASK];

        // Apply 32-bit Envelope & Volume Scale
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31); // Absolute protection against negative clipping
        int32_t finalVol = (int32_t)((envSafe * volBase) >> 14);

        mixBuffer[i] += (signal * finalVol) >> 16;

        carrierPhase += carrierInc;
        modulatorPhase += modulatorInc;
        currentEnv += envStep;
    }

    // Save states back to custom voice array registers
    vo->phase = carrierPhase;
    vo->cw[0] = modulatorPhase;
    vo->cw[1] = (uint32_t)prevSample;
}

void play_fm_lead() {
    synth.setCustomWave(0, fmTwoOpOscillator);
    synth.noteOn(0, c4, 255);
}
```

### Global Master Hook (Feedback Comb Delay Filter)
Apply echo or spatial filters directly on the Master 32-bit mix bus before the signal is formatted for physical DAC registers:

```cpp
#define DECAY_LINE_SIZE 4096
#define DECAY_MASK (DECAY_LINE_SIZE - 1)

int32_t delayLine[DECAY_LINE_SIZE];
int32_t delayWriteIndex = 0;

// High-speed fixed-point Comb Filter
void IRAM_ATTR globalDelayDSP(int32_t* mixBuffer, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        int32_t inputSample = mixBuffer[i];
        
        // Retrieve delayed sample from memory
        int32_t delayedSample = delayLine[(delayWriteIndex - 3000) & DECAY_MASK];
        
        // Comb-filtering logic (Feedback scale: ~62.5% or 5/8)
        int32_t newSample = inputSample + ((delayedSample * 5) >> 3);
        
        // Write to ring buffer
        delayLine[delayWriteIndex] = newSample;
        delayWriteIndex = (delayWriteIndex + 1) & DECAY_MASK;

        // Mix back into active master channel
        mixBuffer[i] = newSample;
    }
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_16BIT);
    synth.setCustomDSP(globalDelayDSP);
}
```

---

## 9. Development Tools & Advanced Troubleshooting

### Utility Scripts (`/tools`)
The repository contains two high-speed python utilities:
*   `WavetableMaker.py`: Converts complex sound mathematical equations or wave segments directly into static aligned C tables (`.h`) mapped as `WAVE_WAVETABLE`.
*   `WavToEsp32SynthConverter.py`: Converts short single-cycle audio files into 4-bit, 8-bit, or 16-bit aligned static memory arrays, avoiding the need for SD cards for transient instruments.

### Core-Level Debugging
* **WDT Reset / Starvation Jitter:** If you hear digital clicking or trigger Core Watchdog Resets, verify that the Xtensa processor is operating at **240MHz**. Standard ESP32 boards default to 160MHz in some configurations, which significantly reduces the available processing headroom.
* **FPU Contention on S3:** ESP32-S3 uses advanced vector SIMD registers on Core 1. If other intensive tasks (such as image analysis, cameras, or complex math) run concurrently on Core 1, task contention will occur. In these scenarios, configure standard tasks on Core 0 and preserve Core 1 exclusively for the synth engine.
* **Flickering PWM Audio:** Under `SMODE_PWM`, make sure that no other task attempts to access LEDC Channel 0 or write to Timer 0 registers. This breaks the latch alignment of the overflow ISR. If there is high-frequency carrier whistle on the pin, route the signal through a simple passive RC low-pass reconstruction filter (a 150-ohm resistor with a 100nF capacitor).

---

# Feel free to make and post videos, code, or suggestions; I'll be happy to see/read them!

<p align="center"><i>Belive in Jesus Crist❤️</i></p>
