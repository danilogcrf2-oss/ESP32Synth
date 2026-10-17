# ESP32Synth v2.5.2 — Bare-Metal Polyphonic DSP Engine & Multi-Core Streaming for ESP32

<p align="center">
  <img src="https://raw.githubusercontent.com/danilogcrf2-oss/ESP32Synth/main/banner.jpg" alt="ESP32Synth banner" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-2.5.2-green.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3%20%7C%20ESP32--P4%20%7C%20ESP32--S2%20%7C%20ESP32--C3%20%7C%20ESP32--C6-orange.svg" alt="Platform">
  <img src="https://img.shields.io/badge/framework-Arduino%20%7C%20ESP--IDF-blue.svg" alt="Framework">
  <img src="https://img.shields.io/badge/license-MIT-yellow.svg" alt="License">
</p>

A bare-metal, polyphonic synthesizer and audio streaming engine for the Espressif ESP32 family (ESP32, ESP32-S3, ESP32-P4, ESP32-S2, ESP32-C3, ESP32-C6). Built for raw integer execution, ultra-low latency, high voice density, modular bus DSP effects, and multi-core streaming. Supports both Arduino IDE and PlatformIO (Arduino and native ESP-IDF).

### ✨ What's New in v2.5.2
* **PWM Thermal & Pop Protection Engine (`armPWM` / `unArmPWM`)**: Zero-float, half-cosine S-curve ramping using `sineLUT` to transition duty cycle smoothly between 0% (0V / coil rest) and 50% (1.65V / audio baseline). Prevents coil overheating, thermal runaway, and mechanical cone pops on filterless hardware such as the Cheap Yellow Display (CYD / ESP32-2432S028R) and direct SC8002B/PAM8302 amplifiers.
* **Unified LEDC Hardware Layer (ESP32-P4 & C-Series Support)**: Universal register mapping dynamically adapts across Classic ESP32, S-Series, and unified peripheral architectures without HS/LS distinctions (`LEDC_TIMER0_*`, `LEDC_CH0_*`, and `LEDC_AUTO_CLK`).
* **Official ESP32-P4 Target Identification**: `getChipModel()` now natively identifies and returns `"ESP32-P4"`.
* **Helix MP3 Toolchain & C99 Standard Fixes**: Fixed `ALWAYS_INLINE` macro resolution in `buffers.c` and missing `<stdint.h>` integer definitions in `polyphase.c` for clean builds across all Xtensa and RISC-V toolchains.
* **Zero-Warning Stream Pipeline**: Replaced aggregate assignment in `ESP32Synth.cpp` with explicit member initialization, eliminating Arduino Core 3.x `StreamTrack` / `fs::File` uninitialized warnings.

<p align="center">
  <a href="https://ko-fi.com/G3E323KDZC">
    <img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="ko-fi" />
  </a>
  <br/>
  <i>plz 🙏🥺</i>
</p>

---

## 📖 Table of Contents

1. [Architectural Highlights: Polyphony Without Compromise](#1-architectural-highlights-polyphony-without-compromise)
2. [Dual-Framework Setup (Arduino IDE & PlatformIO / ESP-IDF)](#2-dual-framework-setup-arduino-ide--platformio--esp-idf)
3. [Multi-Core Distribution: Core 0 vs Core 1](#3-multi-core-distribution-core-0-vs-core-1)
4. [Engine Configuration & Latency Tuning](#4-engine-configuration--latency-tuning)
5. [Memory Architecture & Hardware Alignment](#5-memory-architecture--hardware-alignment)
6. [Unified API Reference](#6-unified-api-reference)
7. [Hardware Output Modes (`SMODE`)](#7-hardware-output-modes-smode)
8. [PWM Thermal & Click Protection (CYD & Direct Speakers)](#8-pwm-thermal--click-protection-cyd--direct-speakers)
9. [SD Card & Flash Audio Streaming (WAV & MP3)](#9-sd-card--flash-audio-streaming-wav--mp3)
10. [External Protocol Pull Mode (A2DP Bluetooth & Wi-Fi)](#10-external-protocol-pull-mode-a2dp-bluetooth--wi-fi)
11. [Fixed-Point Advanced DSP & Patches](#11-fixed-point-advanced-dsp--patches)
12. [The FLT Engine (Float Translator & Fixed-Point Math)](#12-the-flt-engine-float-translator--fixed-point-math)
13. [Troubleshooting & Best Practices](#13-troubleshooting--best-practices)

---

## 1. Architectural Highlights: Polyphony Without Compromise

ESP32Synth delivers high polyphonic voice counts (300+ voices on standard ESP32 at 240 MHz, up to 500 on ESP32-S3) while maintaining sample accuracy at 48,000 Hz.

* **Zero Floats in Audio Path**: All oscillators, filters, envelope generators, modulators, and MP3 decoders use integer arithmetic, LUTs, and bitwise logic.
* **SIMD Vectorization**: ESP32-S3 LX7 instructions process 4 voices simultaneously per clock cycle using 128-bit vector registers (`v4i32`).
* **Zero-Branch PolyBLEP**: Band-limited step functions reduce aliasing on sawtooth and pulse waves with minimal branch overhead.
* **Sub-Block Envelope Processing**: ADSR curves evaluate sample-accurately without per-sample division.

---

## 2. Dual-Framework Setup (Arduino IDE & PlatformIO / ESP-IDF)

ESP32Synth automatically detects the target build system.

### PlatformIO Configuration (`platformio.ini`)

#### Arduino Framework
```ini
[env:esp32s3-arduino]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
build_flags = 
    -O3
    -funroll-loops
```

#### Native ESP-IDF Framework
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

## 3. Multi-Core Distribution: Core 0 vs Core 1

To keep audio synthesis glitch-free under heavy disk and network access, tasks are pinned to dedicated CPU cores:

* **Core 1 (`SYNTH_AUDIO_TASK_CORE 1`)**: Pinned real-time DSP synthesis engine. Computes voice loops, custom waves, bus effects, master mix, and feeds I2S/PWM DMA buffers.
* **Core 0 (`SYNTH_STREAM_TASK_CORE 0`)**: Background streaming and audio decoding. Reads files from SD cards (SPI/SDMMC), manages ring buffers, and decodes MP3 frames via the Helix fixed-point engine.

```
┌─────────────────────────────────────────────────────────────┐
│                        ESP32 DUAL CORE                      │
├──────────────────────────────┬──────────────────────────────┤
│    CORE 0 (STREAM / DECODE)  │      CORE 1 (REAL-TIME DSP)  │
├──────────────────────────────┼──────────────────────────────┤
│  • SD Card File I/O          │  • Polyphonic Mix (80-500 v) │
│  • Ring Buffer Refill        │  • PolyBLEP Oscillators      │
│  • Helix Fixed-Point MP3     │  • Resonant Biquad Filters   │
│  • Flash/PROGMEM Streaming   │  • Multi-Bus FX Chains       │
│  • ID3v2 / Xing / VBR Parser │  • Direct DMA Output Push    │
└──────────────────────────────┴──────────────────────────────┘
```

---

## 4. Engine Configuration & Latency Tuning

Static parameters can be configured in `ESP32Synth_Config.hpp`:

```cpp
#define MAX_VOICES         80   // Max active voices (Set up to 140 on ESP32, 150+ on S3)
#define MAX_WAVETABLES     20   // Custom wavetable cache slots
#define MAX_SAMPLES        20   // PCM sample cache slots
#define MAX_ARP_NOTES      16   // Arpeggio note sequence steps
#define MAX_STREAMS         4   // Concurrent SD/Flash streams
#define STREAM_BUF_SAMPLES 4096 // Ring buffer size per stream (Must be power of 2)
#define MAX_BUSES           4   // Master Dry (0) + Auxiliary FX Buses (1-3)
#define MAX_FX_PER_BUS      3   // Chained DSP effect nodes per bus
#define ENABLE_MP3          1   // Enable Helix Fixed-Point MP3 Engine
#define SYNTH_MP3_IN_BUF_SIZE 2048
```

### DMA Latency Calculations
$$\text{Latency (ms)} = \frac{\text{Buffer Length} \times \text{Buffer Count}}{\text{Sample Rate}} \times 1000$$

* **High Polyphony (Default)**: `SYNTH_DMA_BUF_LEN 512` | `SYNTH_DMA_BUF_COUNT 6` (~64.0 ms latency).
* **Balanced (MIDI Input)**: `SYNTH_DMA_BUF_LEN 256` | `SYNTH_DMA_BUF_COUNT 4` (~21.3 ms latency).
* **Low Latency (Live)**: `SYNTH_DMA_BUF_LEN 128` | `SYNTH_DMA_BUF_COUNT 2` (~5.3 ms latency).

---

## 5. Memory Architecture & Hardware Alignment

Voice structures use unified memory unions aligned to 8-byte boundaries to reduce cache misses:

```cpp
struct Voice {
    int64_t slideVolCurr;
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
        // Mode: WAVE_CUSTOM (DSP filters, custom oscillators)
        uint32_t cw[SYNTH_CUSTOM_WAVE_STATES]; 
    };
    // ...
};
```

All audio buffers and Helix decoder contexts use 16-byte aligned allocations in internal SRAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`) via `heap_caps_aligned_alloc`.

---

## 6. Unified API Reference

The library defaults to **8-bit parameters (0–255)** for master volume, voice volume, pulse width, and dry/wet controls. Frequencies use **CentiHz** ($1\text{ Hz} = 100\text{ cHz}$) using note definitions from `ESP32SynthNotes.h`.

### 1. Initialization (I2S 32-Bit with PCM5102A)

```cpp
#include "ESP32Synth.h"

ESP32Synth synth;

void setup() {
    // Classic ESP32: BCK = GPIO 4, WS = GPIO 15, DATA = GPIO 2
    synth.begin(4, 15, 2, I2S_32BIT);

    // ESP32-S3: BCK = GPIO 4, WS = GPIO 6, DATA = GPIO 5
    // synth.begin(4, 6, 5, I2S_32BIT);

    synth.setMasterVolume(255); // Full master volume without clipping
}
```

### 2. Note and Voice Control

Frequencies are defined as sharps (`s`) without flats (e.g., `ds2` instead of `eb2`, `as1` instead of `bb1`):

```cpp
// Trigger note: Voice 0, Middle C (c4), Volume 255
synth.noteOn(0, c4, 255);

// Configure waveform and pulse width
synth.setWave(0, WAVE_PULSE);
synth.setPulseWidth(0, 128); // 50% duty cycle (0-255)

// ADSR Envelope: Attack=15ms, Decay=200ms, Sustain=180 (0-255), Release=800ms
synth.setEnv(0, 15, 200, 180, 800);

// Release note
synth.noteOff(0);
```

### 3. Modulations and Slides

```cpp
// Vibrato: Voice 0, Rate 5.5 Hz (550 cHz), Depth 25 Hz (2500 cHz)
synth.setVibrato(0, 550, 2500);

// Tremolo: Voice 0, Rate 4.0 Hz (400 cHz), Depth 60 (0-255)
synth.setTremolo(0, 400, 60);

// Smooth frequency slide to G4 over 350 ms
synth.slideFreqTo(0, g4, 350);

// Arpeggiator: Voice 0, 100 ms per step [C4 -> E4 -> G4 -> B4]
synth.setArpeggio(0, 100, c4, e4, g4, b4);
```

---

## 7. Hardware Output Modes (`SMODE`)

| Output Mode | Hardware | Pins Needed | Target Architecture |
| :--- | :--- | :--- | :--- |
| **`SMODE_I2S`** | External I2S DAC (PCM5102A, MAX98357A) | 3 Pins (BCK, WS, DATA) | All ESP32 variants (16-bit & 32-bit) |
| **`SMODE_PWM`** | Hardware LEDC Timer ISR (47,962 Hz Carrier) | 1 Pin | ESP32, ESP32-S3, P4, S2, C3, C6 |
| **`SMODE_PDM`** | Hardware PDM Sigma-Delta Modulator | 1 Pin / 2 Pins | ESP32-S3 (Recommended), ESP32 |
| **`SMODE_DAC`** | Built-in 8-bit Analog DAC | 1 Pin (GPIO 25 or 26) | Classic ESP32, S2 only |
| **`SMODE_HEADLESS`** | No hardware outputs (Offline rendering / SD recording) | None | All ESP32 variants |
| **`SMODE_CUSTOM`** | Pull-mode callback for external protocol stacks | Software defined | All ESP32 variants |

### Example: Single-Pin PWM Audio (`SMODE_PWM`)
Runs an interrupt-driven 10-bit PWM output synchronized directly with LEDC hardware:
```cpp
synth.begin(26, SMODE_PWM, -1, -1, I2S_16BIT);
```

### Example: Headless Audio Bouncing (`SMODE_HEADLESS`)
```cpp
synth.beginHeadless(48000);
synth.startRecording(SD, "/render.wav");
synth.noteOn(0, c4, 255);
// Engine renders internally to the SD card buffer without activating I2S/PWM
```

---

## 8. PWM Thermal & Click Protection (CYD & Direct Speakers)

Devices like the **CYD (ESP32-2432S028R / Cheap Yellow Display)** connect the speaker amplifier (SC8002B) directly to an output pin (GPIO 26) without AC-coupling capacitors or reconstruction filters. 

When PWM is idle at 50% duty (audio silence), continuous DC current ($\sim 1.65\text{ V}$) is dumped into the speaker coil, causing significant thermal buildup. Abruptly switching between 0% and 50% duty causes an audible mechanical speaker "POP".

ESP32Synth v2.5.2 solves this using **`armPWM()`** and **`unArmPWM()`**, applying an S-curve ramp via `sineLUT` in single-cycle fixed-point math:

```cpp
#include "ESP32Synth.h"
#include "ESP32SynthNotes.h"

ESP32Synth synth;

void setup() {
    // Initialize PWM mode on GPIO 26 (CYD onboard amplifier pin)
    synth.begin(26, SMODE_PWM);

    // Immediately disarm to 0% duty (0V GND): coil stays completely cold during idle
    synth.unArmPWM(0);
}

void playBeep() {
    // 1. Arm with a 15 ms smooth S-curve (brings coil from 0V to 1.65V silently)
    synth.armPWM(15);

    // 2. Play UI sound
    synth.setWave(0, WAVE_SINE);
    synth.setEnv(0, 5, 20, 0, 50);
    synth.noteOn(0, c5, 120);

    delay(80); // Sound duration

    // 3. Disarm with a 15 ms smooth S-curve (returns coil to 0V without pop or DC dissipation)
    synth.unArmPWM(15);
}
```

---

## 9. SD Card & Flash Audio Streaming (WAV & MP3)

ESP32Synth handles streaming tasks on Core 0 while keeping the Core 1 audio synthesizer responsive.

### 1. MP3 Streaming from Flash / PROGMEM (Zero RAM / No SD Card)
Compress sound effects, music, or speech into byte arrays and stream directly from flash memory:

```cpp
#include "ESP32Synth.h"
#include "my_mp3_track.h" // const uint8_t my_mp3_data[] PROGMEM = { ... };

ESP32Synth synth;

void setup() {
    synth.begin(4, 15, 2, I2S_32BIT);

    // Voice 0, Data pointer, Size, Volume (255), Pitch (4800000 = Original), Loop (true)
    synth.playMP3StreamFromFlash(0, my_mp3_data, sizeof(my_mp3_data), 255, 4800000, true);
}
```

### 2. MP3 Streaming from SD Card
Automatically skips ID3v2 tags, detects variable bitrates, and manages background decoding:

```cpp
#ifdef ARDUINO
#include <SD.h>

void play_sd_music() {
    // Voice 1, SD Filesystem, Path, Volume, Pitch, Loop
    synth.playMP3Stream(1, SD, "/music/track01.mp3", 255, 4800000, false);
}
#endif
```

### 3. Stream Metadata Inspection
```cpp
uint32_t bitrate = synth.getStreamBitrate(1);      // Bitrate in bps (e.g., 128000, 320000)
bool isVBR       = synth.isStreamVBR(1);          // True if Variable Bitrate detected
uint32_t durMs   = synth.getStreamDurationMs(1);  // Duration in milliseconds
uint32_t posMs   = synth.getStreamPositionMs(1);  // Current playback position
```

### 4. Background Master Audio Recording to SD
```cpp
synth.startRecording(SD, "/session_output.wav");
// ... play synthesis notes or streams ...
synth.stopRecording(); // Finalizes and writes valid WAV headers
```

---

## 10. External Protocol Pull Mode (A2DP Bluetooth & Wi-Fi)

For wireless audio transmission, initialize the engine in pull mode via `SMODE_CUSTOM`:

```cpp
ESP32Synth synth;

void setup() {
    // Pass nullptr as callback to run in Pull Mode without spawning internal audio tasks
    synth.beginCustom(44100, nullptr);
    synth.noteOn(0, c4, 255);
}

// Callback invoked by Bluetooth A2DP or network audio transmission tasks
void a2dp_audio_data_callback(uint8_t *data_buffer, size_t length_in_bytes) {
    int samplePairs = length_in_bytes / 4; // 16-bit interleaved stereo = 4 bytes per frame
    synth.generateSamplesStereo((int16_t*)data_buffer, samplePairs);
}
```

---

## 11. Fixed-Point Advanced DSP & Patches

Pre-configured DSP algorithms are available in `ESP32Synth_Patches.hpp`.

### High-Precision Resonant Biquad Oscillators

#### 1. `DSP_BiquadOsc` / `DSP_BiquadOscHQ` (TDF-II Q28)
Transposed Direct Form II with 28-bit fixed-point coefficients and 64-bit internal registers. Includes polynomial approximations for stable response down to 20 Hz:
* **CPU Load**: ~4.35% per voice.
* **Usage**: Resonant bass, filter sweeps, expressive leads.

```cpp
#include "ESP32Synth_Patches.hpp"

void trigger_filtered_synth() {
    synth.setCustomWave(0, ESP32Patches::DSP_BiquadOsc);
    synth.setCustomParam(0, 0, 0);    // Waveform: 0 = Saw, 1 = Pulse, 2 = Triangle
    synth.setCustomParam(0, 1, 650);  // Cutoff Frequency: 650 Hz (20 to 20000)
    synth.setCustomParam(0, 2, 200);  // Resonance Q: 2.00 (Q * 100)
    synth.setCustomParam(0, 3, 0);    // Filter Type: 0 = LPF, 1 = HPF, 2 = BPF, 3 = Notch

    synth.noteOn(0, c2, 255);
}
```

#### 2. `DSP_BiquadOscFast` (Direct Form I)
Fixed-point Direct Form I for high-polyphony setups:
* **CPU Load**: ~3.45% per voice.

---

### Modular Bus FX Chains

Route voices to auxiliary buses (1 to 3), run chained DSP effects, and sum down to master:

| Parameter | Function | Description / Range |
| :--- | :--- | :--- |
| `ep[0]` | Cutoff / Center Freq | $20\text{ Hz}$ to $20000\text{ Hz}$ |
| `ep[1]` | Filter Mode | `0`: LPF, `1`: HPF, `2`: BPF, `3`: Notch |
| `ep[2]` | Resonance / Bandwidth | If `ep[3] == 0`: $Q \times 100$. If `ep[3] == 1`: Bandwidth in Hz. |
| `ep[3]` | Calibration Unit Mode | `0`: Standard Q mode, `1`: Physical Bandwidth mode |
| `ep[4]` | Dry / Wet Mix | `0` or `255`: 100% Wet. `1` to `254`: Linear dry/wet blend |

```cpp
// Assign Voice 0 to Bus 1
synth.setVoiceBus(0, 1);

// Add Biquad Filter to Bus 1, Slot 0
synth.setBusFX(1, 0, FX_CUSTOM, ESP32Patches::FX_BiquadFilter);
synth.setBusFXParam(1, 0, 0, 1200); // 1200 Hz
synth.setBusFXParam(1, 0, 1, 0);    // Low-Pass Filter
synth.setBusFXParam(1, 0, 2, 180);  // Q = 1.80
synth.setBusFXParam(1, 0, 3, 0);    // Q Mode
synth.setBusFXParam(1, 0, 4, 255);  // 100% Wet

// Mix Bus 1 to Master at full level
synth.setBusMix(1, 255);
```

---

## 12. The FLT Engine (Float Translator & Fixed-Point Math)

The `fip` engine in `ESP32Synth_FLT.hpp` allows writing floating-point style DSP code that compiles into signed **Q8.24 32-bit hardware integer arithmetic**:

```cpp
#include "ESP32Synth_FLT.hpp"

void customWavetableOsc(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    fip phase = fip::fromPhase(vo->phase);
    fip inc   = fip::fromPhase(vo->phaseInc);
    
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;

    for (int i = 0; i < samples; i++) {
        fip wave = fip::sin(phase * fip::two_pi());

        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;

        mixBuffer[i] += (wave.toAudio16() * finalVol) >> 16;

        phase += inc;
        currentEnv += envStep;
    }
    vo->phase = (uint32_t)(phase.val << 8);
}
```

### Pre-Rendering Wavetables (`FLT_Baker`)
Compute complex mathematical waves at startup and store them in internal RAM tables:

```cpp
#include "ESP32Synth_FLT.hpp"

void setup() {
    FLT_Baker::bakeWavetable(&synth, 0, [](fip phase) {
        fip osc1 = fip::sin(phase * fip::two_pi());
        fip osc2 = fip::sin(phase * fip::two_pi() * fip(2.0f)) * fip(0.5f);
        return fip::fast_tanh(osc1 + osc2);
    }, 4096, BITS_16);
}
```

---

## 13. Troubleshooting & Best Practices

1. **CPU Frequency Configuration**: Ensure the CPU frequency is configured to **240 MHz** (or **360/400 MHz** on ESP32-P4) in your board options to maintain timing margins at high voice counts.
2. **I2S Pin Assignments**:
   * **Classic ESP32**: `synth.begin(4, 15, 2, I2S_32BIT);` (BCK = 4, WS = 15, DATA = 2)
   * **ESP32-S3**: `synth.begin(4, 6, 5, I2S_32BIT);` (BCK = 4, WS = 6, DATA = 5)
   * **ESP32-P4**: Configure GPIOs to available digital pins (e.g., `synth.begin(12, 13, 14, I2S_32BIT);`).
3. **CYD / Filterless PWM Audio**: Always call `synth.unArmPWM(0)` at boot. Only call `synth.armPWM(15)` when triggering audio, and `synth.unArmPWM(15)` when audio stops. This protects both the amplifier and the speaker coil.
4. **Internal RAM Headroom**: When running high voice counts (`MAX_VOICES > 100`) alongside MP3 decoders, keep `STREAM_BUF_SAMPLES` around `2048` or `4096` to avoid heap exhaustion on chips without PSRAM.
5. **Volume Scaling**: Use `setMasterVolume(255)` and balance individual voice volumes to prevent digital clipping when summing high polyphonic voice counts.

---

# Feel free to share your creations, open issues, or submit PRs!

<p align="center"><i>Believe in Jesus Christ❤️</i></p>
