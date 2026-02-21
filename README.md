# ESP32Synth v2.3.1 — Professional Audio Synthesis Library

![Version](https://img.shields.io/badge/version-2.3.1-green.svg) ![Platform](https://img.shields.io/badge/platform-ESP32-orange.svg) ![License](https://img.shields.io/badge/license-MIT-blue.svg)

**[English]** A high-performance, polyphonic audio synthesis library for the ESP32. It is engineered for professional applications and hobbyist projects that require low-latency, high-polyphony audio synthesis with a rich and flexible feature set.

**[Português]** Uma biblioteca de síntese de áudio polifônica de alta performance para o ESP32. Foi projetada para aplicações profissionais e projetos que exigem baixa latência, alta polifonia e um conjunto de recursos rico e flexível.

---

# 🇺🇸 English Documentation

## Table of Contents
1.  [**Overview & Key Features**](#1-overview--key-features)
2.  [**Hardware Requirements (Crucial!)**](#2-hardware-requirements-crucial)
3.  [**How It Works: Internal Architecture**](#3-how-it-works-internal-architecture)
    *   [The Dual-Core Design](#the-dual-core-design)
    *   [The Non-Blocking Audio Pipeline](#the-non-blocking-audio-pipeline)
    *   [Control Rate vs. Audio Rate](#control-rate-vs-audio-rate)
    *   [Fixed-Point Math & IRAM Optimization](#fixed-point-math--iram-optimization)
4.  [**Installation**](#4-installation)
5.  [**API Guide**](#5-api-guide)
    *   [Initialization: `begin()`](#initialization-begin)
    *   [Note Control: `noteOn()`, `noteOff()`, `setFrequency()`](#note-control-noteon-noteoff-setfrequency)
    *   [Oscillators: `setWave()`, `setPulseWidth()`](#oscillators-setwave-setpulsewidth)
    *   [Wavetables: `setWavetable()`, `registerWavetable()`](#wavetables-setwavetable-registerwavetable)
    *   [Envelopes (ADSR): `setEnv()`](#envelopes-adsr-setenv)
    *   [Sampler Engine: `registerSample()`, `Instrument_Sample`](#sampler-engine-registersample-instrument_sample)
    *   [Tracker-Style Instruments: `Instrument`, `detachInstrumentAndSetWave()`](#tracker-style-instruments-instrument-detachinstrumentandsetwave)
    *   [Modulation & Effects: `setVibrato()`, `setTremolo()`, `slideTo()`, `setArpeggio()`](#modulation--effects)
    *   [Getters & Status: `isVoiceActive()`, `getOutput8Bit()`](#getters--status)
    *   [Utilities: `ESP32SynthNotes.h`](#utilities-esp32synthnotesh)
6.  [**Voice Management for Polyphony**](#6-voice-management-for-polyphony)
7.  [**Included Tools**](#7-included-tools)
8.  [**Troubleshooting**](#8-troubleshooting)

---

## 1. Overview & Key Features

**ESP32Synth** transforms an ESP32 board into a powerful and versatile polyphonic synthesizer. It was built from the ground up for performance, providing low-level control and enabling everything from simple tone generators to complex, evolving, multi-sampled instruments.

*   **Massive Polyphony:** A high-performance mixing engine handles up to **80 simultaneous voices**.
*   **Flexible Audio Output:** Native support for the **internal DAC** (ESP32 classic only), external **I2S DACs** (with 8, 16, and 32-bit depth), and **PDM** digital audio output.
*   **Rich Oscillators:** Core waveforms include Sine, Triangle, Sawtooth, Pulse (with variable width), and Noise.
*   **Wavetable Synthesis:** Use custom wavetables for unique, complex timbres. The engine supports both direct assignment and a memory-efficient registry system for tracker instruments.
*   **Advanced Sampler Engine:** Play PCM audio samples with features like multi-sampling (keyboard zones), pitch shifting, and various loop modes (forward, reverse, ping-pong).
*   **ADSR Envelopes:** A per-voice ADSR (Attack, Decay, Sustain, Release) envelope generator to shape the volume of each note.
*   **Tracker-Style Instruments:** A powerful feature to create intricate, evolving sounds by sequencing different waveforms and volume levels for a note's attack, sustain, and release phases.
*   **Modulation & Effects:** Per-voice Vibrato (pitch LFO), Tremolo (amplitude LFO), Portamento/Slide, and a flexible Arpeggiator.

---

## 2. Hardware Requirements (Crucial!)

This library was engineered to extract maximum performance from specific ESP32 hardware. Failure to meet these requirements will result in incorrect behavior, audio glitches, or crashes.

*   **CPU: Dual-Core ESP32 Required**
    *   The library's core architecture dedicates an entire CPU core to the real-time audio synthesis task (`audioTask`). This design is fundamental to its glitch-free, non-blocking operation.
    *   **Supported:** **ESP32** (classic) and **ESP32-S3**. Both are dual-core and fully supported for I2S and PDM output.
    *   **Unsupported:** Single-core chips like **ESP32-S2** and **ESP32-C3** are **NOT SUPPORTED**.
    *   **Note on Internal DAC:** The low-fi internal DAC output is **only available on the classic ESP32**, not on the ESP32-S3.

*   **CPU Frequency: 240 MHz Recommended**
    *   **Supported:** The library is tested and performs correctly on ESP32s running at **240 MHz**.
    *   **Unsupported:** Chips running at lower frequencies (e.g., 160 MHz) have **not been tested and are expected to fail**. The real-time audio rendering is CPU-intensive, and a 240 MHz clock speed is required to process the maximum number of voices without missing deadlines, which would cause audible clicks and distortion.

---

## 3. How It Works: Internal Architecture

Understanding the library's design is key to using it effectively.

### The Dual-Core Design
The ESP32's dual-core architecture is the foundation of the synthesizer.
*   **Core 1 (The "Real-Time" Core):** The high-priority `audioTask` is pinned to Core 1. By default, your `setup()` and `loop()` code also run on this core. The audio task's high priority ensures it always runs on time, preempting the `loop()` to prevent any audio glitches. This core handles all time-critical operations.
*   **Core 0 (The "Application" Core):** This core is typically free to run less time-sensitive tasks, most notably the WiFi and Bluetooth stacks. This separation is critical: heavy network activity on Core 0 will not interfere with the audio generation happening on Core 1.

### The Non-Blocking Audio Pipeline
The entire process is non-blocking and managed by DMA.

1.  **`audioTask` Loop:** The task on Core 1 enters an infinite loop.
2.  **`render()` Call:** In each iteration, the loop calls the main `render()` function, requesting a small block of audio samples (e.g., 512 samples).
3.  **Voice Mixing:** The `render()` function iterates through all 80 `Voice` structs. For each active voice, it calculates the next block of samples based on its current frequency, waveform, envelope, and modulators. All active voices are summed into a single **mix buffer**.
4.  **DMA Transfer:** The final mix buffer is handed to the **I2S peripheral** (or DAC driver). A **DMA (Direct Memory Access)** controller then automatically transfers the audio data from the buffer to the physical output pins, sample by sample, without any further CPU involvement.
5.  **Continuous Cycle:** While the DMA is busy sending audio, the `audioTask` is already ahead, working on rendering the *next* block of audio. This producer-consumer model ensures the DMA never runs out of data to send.

### Control Rate vs. Audio Rate
Updating every parameter at the audio sample rate (e.g., 48,000 Hz) would be incredibly inefficient. The library separates these updates:

*   **Audio Rate (e.g., 48,000 Hz):** The core oscillator phase and sample-level mixing happen at the full sample rate inside specialized `renderBlock` functions.
*   **Control Rate (default 100 Hz):** Slower-moving parameters are updated in the `processControl()` function. This includes **LFOs (Vibrato/Tremolo), ADSR envelopes, Arpeggios, and Instrument sequences**. This function runs only 100 times per second by default. The audio rendering functions then interpolate the values between these control points.
    *   You can adjust this with `setControlRateHz()`. A higher rate means smoother modulation at the cost of higher CPU usage.

### Fixed-Point Math & IRAM Optimization
*   **Fixed-Point Arithmetic:** Instead of using slow floating-point numbers (`float`), the entire real-time signal path uses **32-bit fixed-point integers**. For example, frequency is handled in "CentiHz" and phase is managed with a 32-bit accumulator. This avoids the overhead and potential non-determinism of the FPU, which is critical for real-time safety.
*   **`IRAM_ATTR`:** All time-critical functions (`render`, `processControl`, and the various `renderBlock` DSP routines) are marked with `IRAM_ATTR`. This forces the compiler to place them in the ESP32's fast **Internal RAM (IRAM)** instead of slower external Flash memory. This completely eliminates latency from flash cache misses, a common source of audio glitches.

---

## 4. Installation

**1. Using the Arduino IDE Library Manager (Recommended)**
1.  In the Arduino IDE, navigate to `Sketch` -> `Include Library` -> `Manage Libraries...`.
2.  Search for `ESP32Synth`.
3.  Click the "Install" button.

**2. Manual Installation (.zip)**
1.  Download the repository as a `.zip` file from the GitHub page.
2.  In the Arduino IDE, navigate to `Sketch` -> `Include Library` -> `Add .ZIP Library...` and select the downloaded file.
3.  Restart the Arduino IDE.

**Dependencies:**
*   **ESP32 Board Core:** Requires version **3.0.0 or newer** of the `esp32` by Espressif Systems board core. Older versions are not supported.

---

## 5. API Guide

### Initialization: `begin()`
This function configures the hardware and starts the audio engine. Use the overload that matches your hardware.

*   `bool begin(int dacPin);`
    *   **Use Case:** For the **internal DAC** on the classic ESP32 (pin 25 or 26).
    *   **Quality:** Low-fi, but requires no external components. Good for simple beeps and testing.

*   `bool begin(int bckPin, int wsPin, int dataPin);`
    *   **Use Case:** For external **I2S DACs** like the PCM5102A or MAX98357A. Defaults to 16-bit audio.
    *   **Quality:** High-fidelity. The standard for most audio projects.

*   `bool begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth);`
    *   **Use Case:** For high-resolution I2S DACs. `i2sDepth` can be `I2S_8BIT`, `I2S_16BIT`, or `I2S_32BIT`.
    *   **Quality:** `I2S_32BIT` provides the highest resolution output format.

*   `bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth);`
    *   The main, fully-featured initialization function. This is used for all **I2S** and **PDM** (`SMODE_PDM`) outputs.

### Note Control: `noteOn()`, `noteOff()`
This is the primary way to play and stop sounds. The system uses a "gate" model, just like analog synthesizers.

*   `void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume);`
    *   Triggers a note on a specific `voice` (0-79). This "opens the gate" and starts the **Attack** phase of the ADSR envelope.
    *   `freqCentiHz`: The frequency in hundredths of a Hertz (e.g., `44000` for 440.00 Hz). The `ESP32SynthNotes.h` file provides pre-defined constants like `c4`.
    *   `volume`: The note's velocity or base volume (0-255).

*   `void noteOff(uint8_t voice);`
    *   "Closes the gate" for the specified voice. This **does not stop the sound immediately**. It triggers the **Release** phase of the ADSR envelope, causing the note to fade out smoothly. The voice becomes inactive only after the release phase is complete.

*   `void setFrequency(uint8_t voice, uint32_t freqCentiHz);`
    *   Changes the frequency of an already-playing voice.

*   `void setVolume(uint8_t voice, uint8_t volume);`
    *   Changes the base volume of an already-playing voice.

### Oscillators: `setWave()`, `setPulseWidth()`

*   `void setWave(uint8_t voice, WaveType type);`
    *   Sets the fundamental waveform for a voice's oscillator.
    *   `WaveType`: `WAVE_SINE`, `WAVE_TRIANGLE`, `WAVE_SAW`, `WAVE_PULSE`, `WAVE_NOISE`, `WAVE_WAVETABLE`, `WAVE_SAMPLE`.

*   `void setPulseWidth(uint8_t voice, uint8_t width);`
    *   For `WAVE_PULSE` only. Controls the duty cycle of the pulse wave.
    *   `width`: 0-255. A value of `128` is a 50% duty cycle (a perfect square wave). Changing this value alters the harmonic content, a technique known as Pulse Width Modulation (PWM).

### Wavetables: `setWavetable()`, `registerWavetable()`
Wavetables are arrays containing a single cycle of a custom waveform.

*   `void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth);`
    *   Assigns a wavetable directly to a single voice.
    *   `data`: Pointer to the array of sample data.
    *   `size`: The number of samples in the array.
    *   `depth`: The bit depth of the data (`BITS_8` or `BITS_16`).

*   `void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth);`
    *   Registers a wavetable in a global lookup table with a unique `id` (0-999). This is the required method for using wavetables with **Tracker-Style Instruments**. It is highly memory-efficient as the wavetable is stored only once and referenced by its ID.

### Envelopes (ADSR): `setEnv()`

*   `void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);`
    *   Defines the volume contour for a voice.
    *   `a` (Attack): Time in milliseconds for the volume to go from 0 to max.
    *   `d` (Decay): Time in ms for the volume to fall from max to the sustain level.
    *   `s` (Sustain): The volume level (0-255) held as long as the note is on.
    *   `r` (Release): Time in ms for the volume to fall from the sustain level to 0 after `noteOff()` is called.

### Sampler Engine: `registerSample()`, `Instrument_Sample`
The sampler can play back pre-recorded PCM audio.

**Workflow:**
1.  **Convert Audio:** Use the `WavToEsp32SynthConverter.py` script in the `tools` folder to convert a `.wav` file into a `.h` header file.
2.  **Register Sample:**
    `void registerSample(uint16_t id, const int16_t* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz);`
    *   In your `setup()`, call this to load the sample data from the header file into the synth's registry.
    *   `id`: A unique ID (0-99) for this sample.
    *   `rootFreqCentiHz`: The original pitch of the sample (e.g., `c4`). This is crucial for correct pitch shifting.
3.  **Define Zones:**
    An `Instrument_Sample` uses an array of `SampleZone` structs to map frequency ranges to specific samples. This allows for creating multi-sampled instruments (e.g., a different piano sample for every octave).
    ```cpp
    // This zone maps all frequencies to sample ID 0.
    const SampleZone singleZone[] = {{ .lowFreq = 0, .highFreq = 1300000, .sampleId = 0 }};
    ```
4.  **Create Instrument:**
    ```cpp
    Instrument_Sample mySampler = {
      .zones = singleZone,
      .numZones = 1,
      .loopMode = LOOP_FORWARD, // LOOP_OFF, LOOP_PINGPONG, LOOP_REVERSE
      .loopStart = 1000,
      .loopEnd = 5000
    };
    ```
5.  **Assign and Play:**
    `void setInstrument(uint8_t voice, Instrument_Sample* inst);`
    *   Use this function to attach the sampler instrument to a voice, then use `noteOn()` to play it. The frequency passed to `noteOn()` will determine the playback pitch.

### Tracker-Style Instruments: `Instrument`
This advanced feature allows a single note to trigger a sequence of different timbres and volumes. This is perfect for creating percussive hits, laser zaps, or evolving pads.

The `Instrument` struct contains pointers to arrays that define the sequences for the attack and release phases.

```cpp
// Example: A kick drum sound
// Attack: Starts with a noisy "click", then a quick sine wave pitch drop.
const int16_t kick_attack_waves[] = { W_NOISE, W_SINE };
const uint8_t kick_attack_vols[]  = { 255, 127 };

Instrument kickInstrument = {
  .seqVolumes = kick_attack_vols,
  .seqWaves = kick_attack_waves,
  .seqLen = 2,
  .seqSpeedMs = 20, // Each step is 20ms

  .susVol = 0,         // No sound during sustain
  .susWave = W_SINE,

  .relLen = 0 // No release phase
};
```
To use it, assign it with `setInstrument(voice, &kickInstrument)` and trigger it with `noteOn()`.

*   `void detachInstrumentAndSetWave(uint8_t voice, WaveType type);`
    *   A utility function to remove any assigned Instrument (Tracker or Sampler) from a voice and set it back to a basic waveform. This is essential for safely re-using voices in a polyphonic setup.

### Modulation & Effects

*   `void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);`
    *   Adds a pitch LFO (vibrato). `depthCentiHz` controls the amount of pitch deviation.

*   `void setTremolo(uint8_t voice, uint32_t rateCentiHz, uint16_t depth);`
    *   Adds an amplitude LFO (tremolo). `depth` is the intensity from 0-255.

*   `void slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs);`
    *   Slides the pitch of a voice from its current frequency to a target frequency over a set duration.

*   `void setArpeggio(uint8_t voice, uint16_t durationMs, Args... freqs);`
    *   A template function to create a sequence of notes. When you call `noteOn()` on this voice, it will automatically cycle through the `freqs` provided, with each note lasting `durationMs`.
    ```cpp
    // Create a C-minor arpeggio
    synth.setArpeggio(0, 150, c4, ds4, g4, c5);
    synth.noteOn(0, c4, 255); // Trigger the arpeggio
    ```

### Getters & Status

*   `bool isVoiceActive(uint8_t voice);`
    *   Returns `true` if the voice is currently playing (i.e., not in the `ENV_IDLE` state). Useful for finding a free voice.

*   `uint8_t getOutput8Bit(uint8_t voice);`
    *   Returns the current final output level of a voice (0-255), after volume and envelope have been applied. Useful for "voice stealing" logic or creating visualizations.

### Utilities: `ESP32SynthNotes.h`
The library includes `ESP32SynthNotes.h`, which provides:
*   Pre-defined constants for note frequencies in CentiHz (e.g., `c4`, `fs5`, `a4`).
*   An inline helper function `midiToFreq(uint8_t note)` to convert a standard MIDI note number (0-127) to the corresponding frequency in CentiHz.

---

## 6. Voice Management for Polyphony

The library gives you full control over the 80 voices but does **not** automatically allocate them. You must implement your own logic to find a free voice to play a new note. A common strategy is "voice stealing."

```cpp
int findFreeVoice() {
  // 1. Find the first completely inactive voice.
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!synth.isVoiceActive(i)) {
      return i;
    }
  }

  // 2. If all voices are active, find the quietest one to "steal".
  // This is likely a voice in its release phase.
  uint8_t lowestVol = 255;
  int quietestVoice = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    uint8_t vol = synth.getOutput8Bit(i);
    if (vol < lowestVol) {
      lowestVol = vol;
      quietestVoice = i;
    }
  }
  return quietestVoice;
}
```

---

## 7. Included Tools

The `tools` directory contains helpful Python scripts:

*   `tools/Wavetables/WavetableMaker.py`: Generates wavetables from various sources.
*   `tools/Samples/WavToEsp32SynthConverter.py`: Converts standard `.wav` files into `.h` header files compatible with the sampler engine.

---

## 8. Troubleshooting

*   **Audio has clicks, pops, or distortion:**
    1.  **Check Hardware Requirements:** Ensure you are using a supported dual-core, 240 MHz ESP32.
    2.  **Power Supply:** Use a clean 3.3V power source. Noise on the power rail is a very common cause of bad audio.
    3.  **I2S Wiring:** Keep I2S wires (BCK, WS, DATA) as short as possible and ensure a solid GND connection between the ESP32 and your DAC.
    4.  **Amplifier:** The output of a DAC is line-level. **Do not connect it directly to a speaker.** You need an external amplifier.

*   **ESP32 crashes or reboots (Watchdog Timer) and using `delay()`:**
    *   Because the audio synthesis runs in a separate, high-priority task (`audioTask`) pinned to Core 1, using the standard Arduino `delay()` function in your `loop()` **will not cause audio glitches or stuttering**. The FreeRTOS scheduler will ensure the `audioTask` always gets CPU time.
    *   You can use `delay()` without problems for simple applications. However, be aware that very long delays (many seconds) can still potentially trigger the ESP32's Task Watchdog Timer (TWDT), which can cause a reboot. For complex applications that need to remain responsive, it is still good practice to use non-blocking timers with `millis()`.

*   **Compilation errors about `i2s` or `dac` functions:**
    *   Your ESP32 board support package in the Arduino IDE is likely outdated. Update it via the Boards Manager.

---

## Advanced Usage: Wavetable Memory Management

A critical detail when working with wavetables is understanding the difference between the number of samples and the size of the data array in bytes. The `setWavetable()` and `registerWavetable()` functions expect the `size` parameter to be the **number of individual samples** in the waveform, not the byte size of the array.

When using C++'s `sizeof()` operator, you get the total size of the array in bytes. To get the correct number of samples, you must adjust this value based on the wavetable's bit depth.

### `BITS_16` (16-bit wavetables)
Each sample is an `int16_t`, which takes up 2 bytes. To get the number of samples, you must **divide by 2**.

```cpp
// A 16-bit wavetable with 256 samples occupies 512 bytes.
const int16_t my_wave_16bit[256] = { ... };

// Correct usage:
synth.setWavetable(0, my_wave_16bit, sizeof(my_wave_16bit) / 2, BITS_16);
```

### `BITS_8` (8-bit wavetables)
Each sample is a `uint8_t`, which takes up 1 byte. The number of samples is equal to the size in bytes.

```cpp
// An 8-bit wavetable with 256 samples occupies 256 bytes.
const uint8_t my_wave_8bit[256] = { ... };

// Correct usage:
synth.setWavetable(0, my_wave_8bit, sizeof(my_wave_8bit), BITS_8);
```

### `BITS_4` (4-bit wavetables)
4-bit samples are packed, with **two samples stored in a single `uint8_t`**. To get the total number of samples, you must **multiply by 2**.

```cpp
// A 4-bit wavetable with 256 samples occupies 128 bytes.
const uint8_t my_wave_4bit[128] = { ... };

// Correct usage:
synth.setWavetable(0, my_wave_4bit, sizeof(my_wave_4bit) * 2, BITS_4);
```
The `WavetableMaker.py` tool now automatically generates example code that uses the correct `sizeof()` logic.

<br>
<hr>
<br>

# 🇧🇷 Documentação em Português

## Índice
1.  [**Visão Geral e Recursos Principais**](#1-visão-geral-e-recursos-principais)
2.  [**Requisitos de Hardware (Crucial!)**](#2-requisitos-de-hardware-crucial)
3.  [**Como Funciona: Arquitetura Interna**](#3-como-funciona-arquitetura-interna)
    *   [O Design Dual-Core](#o-design-dual-core-1)
    *   [O Pipeline de Áudio Não-Bloqueante](#o-pipeline-de-áudio-não-bloqueante)
    *   [Taxa de Controle vs. Taxa de Áudio](#taxa-de-controle-vs-taxa-de-áudio)
    *   [Aritmética de Ponto Fixo e Otimização de IRAM](#aritmética-de-ponto-fixo-e-otimização-de-iram)
4.  [**Instalação**](#4-instalação-1)
5.  [**Guia da API**](#5-guia-da-api-1)
    *   [Inicialização: `begin()`](#inicialização-begin-1)
    *   [Controle de Notas: `noteOn()`, `noteOff()`, `setFrequency()`](#controle-de-notas-noteon-noteoff-setfrequency)
    *   [Osciladores: `setWave()`, `setPulseWidth()`](#osciladores-setwave-setpulsewidth-1)
    *   [Wavetables: `setWavetable()`, `registerWavetable()`](#wavetables-setwavetable-registerwavetable-1)
    *   [Envelopes (ADSR): `setEnv()`](#envelopes-adsr-setenv-1)
    *   [Motor de Sampler: `registerSample()`, `Instrument_Sample`](#motor-de-sampler-registersample-instrument_sample)
    *   [Instrumentos Estilo Tracker: `Instrument`, `detachInstrumentAndSetWave()`](#instrumentos-estilo-tracker-instrument-detachinstrumentandsetwave-1)
    *   [Modulação e Efeitos: `setVibrato()`, `setTremolo()`, `slideTo()`, `setArpeggio()`](#modulação-e-efeitos-1)
    *   [Getters e Status: `isVoiceActive()`, `getOutput8Bit()`](#getters-e-status-1)
    *   [Utilitários: `ESP32SynthNotes.h`](#utilitários-esp32synthnotesh-1)
6.  [**Gerenciamento de Vozes para Polifonia**](#6-gerenciamento-de-vozes-para-polifonia-1)
7.  [**Ferramentas Inclusas**](#7-ferramentas-inclusas-1)
8.  [**Solução de Problemas**](#8-solução-de-problemas-1)

---

## Uso Avançado: Gerenciamento de Memória de Wavetables

Um detalhe crítico ao trabalhar com wavetables é entender a diferença entre o número de amostras (samples) e o tamanho do array de dados em bytes. As funções `setWavetable()` e `registerWavetable()` esperam que o parâmetro `size` seja o **número de amostras individuais** na forma de onda, e não o tamanho do array em bytes.

Ao usar o operador `sizeof()` do C++, você obtém o tamanho total do array em bytes. Para obter o número correto de amostras, você deve ajustar esse valor com base na profundidade de bits (bit depth) da wavetable.

### `BITS_16` (wavetables de 16-bit)
Cada amostra é um `int16_t`, que ocupa 2 bytes. Para obter o número de amostras, você deve **dividir por 2**.

```cpp
// Uma wavetable de 16-bit com 256 amostras ocupa 512 bytes.
const int16_t my_wave_16bit[256] = { ... };

// Uso correto:
synth.setWavetable(0, my_wave_16bit, sizeof(my_wave_16bit) / 2, BITS_16);
```

### `BITS_8` (wavetables de 8-bit)
Cada amostra é um `uint8_t`, que ocupa 1 byte. O número de amostras é igual ao tamanho em bytes.

```cpp
// Uma wavetable de 8-bit com 256 amostras ocupa 256 bytes.
const uint8_t my_wave_8bit[256] = { ... };

// Uso correto:
synth.setWavetable(0, my_wave_8bit, sizeof(my_wave_8bit), BITS_8);
```

### `BITS_4` (wavetables de 4-bit)
As amostras de 4-bit são compactadas, com **duas amostras armazenadas em um único `uint8_t`**. Para obter o número total de amostras, você deve **multiplicar por 2**.

```cpp
// Uma wavetable de 4-bit com 256 amostras ocupa 128 bytes.
const uint8_t my_wave_4bit[128] = { ... };

// Uso correto:
synth.setWavetable(0, my_wave_4bit, sizeof(my_wave_4bit) * 2, BITS_4);
```
A ferramenta `WavetableMaker.py` agora gera automaticamente o código de exemplo que utiliza a lógica `sizeof()` correta.


---

## 1. Visão Geral e Recursos Principais

O **ESP32Synth** transforma uma placa ESP32 em um sintetizador polifônico poderoso e versátil. Ele foi construído do zero para performance, fornecendo controle de baixo nível e permitindo desde simples geradores de tom até instrumentos complexos, multi-sampleados e em evolução.

*   **Polifonia Massiva:** Um motor de mixagem de alta performance gerencia até **80 vozes simultâneas**.
*   **Saída de Áudio Flexível:** Suporte nativo para o **DAC interno** (apenas ESP32 clássico), **DACs I2S** externos (com profundidade de 8, 16 e 32 bits) e saída de áudio digital **PDM**.
*   **Osciladores Ricos:** Formas de onda principais incluem Senoidal, Triangular, Dente de Serra, Pulso (com largura variável) e Ruído.
*   **Síntese Wavetable:** Use wavetables customizadas para timbres únicos e complexos. O motor suporta tanto atribuição direta quanto um sistema de registro eficiente em memória para instrumentos de tracker.
*   **Motor de Sampler Avançado:** Reproduza amostras de áudio PCM com recursos como multi-sampling (zonas de teclado), pitch shifting e vários modos de loop (forward, reverse, ping-pong).
*   **Envelopes ADSR:** Um gerador de envelope ADSR (Attack, Decay, Sustain, Release) por voz para modelar o volume de cada nota.
*   **Instrumentos Estilo Tracker:** Um recurso poderoso para criar sons intrincados e evolutivos, sequenciando diferentes formas de onda e níveis de volume para as fases de ataque, sustentação e relaxamento de uma nota.
*   **Modulação e Efeitos:** Vibrato (LFO de pitch), Tremolo (LFO de amplitude), Portamento/Slide e um Arpejador flexível por voz.

---

## 2. Requisitos de Hardware (Crucial!)

Esta biblioteca foi projetada para extrair o máximo de performance de hardware ESP32 específico. O não cumprimento desses requisitos resultará em comportamento incorreto, falhas de áudio ou travamentos.

*   **CPU: Requer ESP32 Dual-Core**
    *   A arquitetura central da biblioteca dedica um núcleo de CPU inteiro à tarefa de síntese de áudio em tempo real (`audioTask`). Este design é fundamental para sua operação sem falhas e não-bloqueante.
    *   **Suportado:** **ESP32** (clássico) e **ESP32-S3**. Ambos são dual-core e totalmente suportados para saída I2S e PDM.
    *   **Não Suportado:** Chips single-core como **ESP32-S2** e **ESP32-C3** **NÃO SÃO SUPORTADOS**.
    *   **Nota sobre o DAC Interno:** A saída de baixa fidelidade do DAC interno está **disponível apenas no ESP32 clássico**, não no ESP32-S3.

*   **Frequência da CPU: 240 MHz Recomendado**
    *   **Suportado:** A biblioteca é testada e funciona corretamente em ESP32s operando a **240 MHz**.
    *   **Não Suportado:** Chips operando em frequências mais baixas (ex: 160 MHz) **não foram testados e a expectativa é que falhem**. A renderização de áudio em tempo real é intensiva em CPU, e uma velocidade de clock de 240 MHz é necessária para processar o número máximo de vozes sem perder prazos, o que causaria cliques e distorção audíveis.

---

## 3. Como Funciona: Arquitetura Interna

Entender o design da biblioteca é fundamental para usá-la de forma eficaz.

### O Design Dual-Core
A arquitetura dual-core do ESP32 é a base do sintetizador.
*   **Core 1 (O Núcleo de "Tempo Real"):** A tarefa de alta prioridade `audioTask` é fixada no Core 1. Por padrão, seu código `setup()` e `loop()` também roda neste núcleo. A alta prioridade da tarefa de áudio garante que ela sempre execute no tempo certo, tomando a frente do `loop()` para prevenir falhas no áudio. Este núcleo gerencia todas as operações críticas em tempo.
*   **Core 0 (O Núcleo de "Aplicação"):** Este núcleo geralmente fica livre para executar tarefas menos sensíveis ao tempo, principalmente as pilhas de WiFi e Bluetooth. Essa separação é crítica: atividades de rede pesadas no Core 0 não interferirão na geração de áudio que acontece no Core 1.

### O Pipeline de Áudio Não-Bloqueante
Todo o processo é não-bloqueante e gerenciado por DMA.

1.  **Loop da `audioTask`:** A tarefa no Core 1 entra em um loop infinito.
2.  **Chamada de `render()`:** Em cada iteração, o loop chama a função principal `render()`, solicitando um pequeno bloco de amostras de áudio (ex: 512 amostras).
3.  **Mixagem de Vozes:** A função `render()` itera por todas as 80 estruturas `Voice`. Para cada voz ativa, ela calcula o próximo bloco de amostras com base em sua frequência, forma de onda, envelope e moduladores atuais. Todas as vozes ativas são somadas em um único **buffer de mixagem**.
4.  **Transferência DMA:** O buffer de mixagem final é entregue ao **periférico I2S** (ou driver DAC). Um controlador **DMA (Direct Memory Access)** então transfere automaticamente os dados de áudio do buffer para os pinos de saída físicos, amostra por amostra, sem qualquer envolvimento adicional da CPU.
5.  **Ciclo Contínuo:** Enquanto o DMA está ocupado enviando o áudio, a `audioTask` já está à frente, trabalhando na renderização do *próximo* bloco de áudio. Este modelo produtor-consumidor garante que o DMA nunca fique sem dados para enviar.

### Taxa de Controle vs. Taxa de Áudio
Atualizar todos os parâmetros na taxa de amostragem de áudio (ex: 48.000 Hz) seria incrivelmente ineficiente. A biblioteca separa essas atualizações:

*   **Taxa de Áudio (ex: 48.000 Hz):** A fase do oscilador principal e a mixagem em nível de amostra acontecem na taxa de amostragem completa dentro de funções `renderBlock` especializadas.
*   **Taxa de Controle (padrão 100 Hz):** Parâmetros de movimento mais lento são atualizados na função `processControl()`. Isso inclui **LFOs (Vibrato/Tremolo), envelopes ADSR, Arpejos e sequências de Instrumentos**. Esta função roda apenas 100 vezes por segundo por padrão. As funções de renderização de áudio então interpolam os valores entre esses pontos de controle.
    *   Você pode ajustar isso com `setControlRateHz()`. Uma taxa maior significa modulação mais suave ao custo de maior uso da CPU.

### Aritmética de Ponto Fixo e Otimização de IRAM
*   **Aritmética de Ponto Fixo:** Em vez de usar números de ponto flutuante lentos (`float`), todo o caminho do sinal em tempo real usa **inteiros de ponto fixo de 32 bits**. Por exemplo, a frequência é tratada em "CentiHz" e a fase é gerenciada com um acumulador de 32 bits. Isso evita a sobrecarga e a potencial não-determinismo da FPU, que é crítico para a segurança em tempo real.
*   **`IRAM_ATTR`:** Todas as funções críticas em tempo (`render`, `processControl` e as várias rotinas DSP `renderBlock`) são marcadas com `IRAM_ATTR`. Isso força o compilador a colocá-las na rápida **RAM Interna (IRAM)** do ESP32, em vez da memória Flash externa mais lenta. Isso elimina completamente a latência de cache misses da flash, uma fonte comum de falhas de áudio.

---

## 4. Instalação

**1. Usando o Gerenciador de Bibliotecas da IDE do Arduino (Recomendado)**
1.  Na IDE do Arduino, navegue até `Sketch` -> `Incluir Biblioteca` -> `Gerenciar Bibliotecas...`.
2.  Procure por `ESP32Synth`.
3.  Clique no botão "Instalar".

**2. Instalação Manual (.zip)**
1.  Baixe o repositório como um arquivo `.zip` da página do GitHub.
2.  Na IDE do Arduino, navegue para `Sketch` -> `Incluir Biblioteca` -> `Adicionar Biblioteca .ZIP...` e selecione o arquivo baixado.
3.  Reinicie a IDE do Arduino.

**Dependências:**
*   **Core da Placa ESP32:** Requer a versão **3.0.0 ou mais recente** do core `esp32` da Espressif Systems. Versões mais antigas não são suportadas.

---

## 5. Guia da API

### Inicialização: `begin()`
Esta função configura o hardware e inicia o motor de áudio. Use a sobrecarga que corresponde ao seu hardware.

*   `bool begin(int dacPin);`
    *   **Caso de Uso:** Para o **DAC interno** no ESP32 clássico (pino 25 ou 26).
    *   **Qualidade:** Baixa-fidelidade, mas não requer componentes externos. Bom para bipes simples e testes.

*   `bool begin(int bckPin, int wsPin, int dataPin);`
    *   **Caso de Uso:** Para **DACs I2S** externos como o PCM5102A ou MAX98357A. O padrão é áudio de 16 bits.
    *   **Qualidade:** Alta-fidelidade. O padrão para a maioria dos projetos de áudio.

*   `bool begin(int bckPin, int wsPin, int dataPin, I2S_Depth i2sDepth);`
    *   **Caso de Uso:** Para DACs I2S de alta resolução. `i2sDepth` pode ser `I2S_8BIT`, `I2S_16BIT` ou `I2S_32BIT`.
    *   **Qualidade:** `I2S_32BIT` fornece o formato de saída de maior resolução.

*   `bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin, I2S_Depth i2sDepth);`
    *   A função de inicialização principal e completa. É usada para todas as saídas **I2S** e **PDM** (`SMODE_PDM`).

### Controle de Notas: `noteOn()`, `noteOff()`
Esta é a maneira principal de tocar e parar sons. O sistema usa um modelo de "gate", assim como os sintetizadores analógicos.

*   `void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume);`
    *   Dispara uma nota em uma `voice` específica (0-79). Isso "abre o gate" e inicia a fase de **Attack** do envelope ADSR.
    *   `freqCentiHz`: A frequência em centésimos de Hertz (ex: `44000` para 440.00 Hz). O arquivo `ESP32SynthNotes.h` fornece constantes pré-definidas como `c4`.
    *   `volume`: A velocidade ou volume base da nota (0-255).

*   `void noteOff(uint8_t voice);`
    *   "Fecha o gate" para a voz especificada. Isso **não para o som imediatamente**. Ele dispara a fase de **Release** do envelope ADSR, fazendo com que a nota desapareça suavemente. A voz só se torna inativa após a conclusão da fase de release.

*   `void setFrequency(uint8_t voice, uint32_t freqCentiHz);`
    *   Altera a frequência de uma voz que já está tocando.

*   `void setVolume(uint8_t voice, uint8_t volume);`
    *   Altera o volume base de uma voz que já está tocando.

### Osciladores: `setWave()`, `setPulseWidth()`

*   `void setWave(uint8_t voice, WaveType type);`
    *   Define a forma de onda fundamental para o oscilador de uma voz.
    *   `WaveType`: `WAVE_SINE`, `WAVE_TRIANGLE`, `WAVE_SAW`, `WAVE_PULSE`, `WAVE_NOISE`, `WAVE_WAVETABLE`, `WAVE_SAMPLE`.

*   `void setPulseWidth(uint8_t voice, uint8_t width);`
    *   Apenas para `WAVE_PULSE`. Controla o ciclo de trabalho da onda de pulso.
    *   `width`: 0-255. Um valor de `128` é um ciclo de trabalho de 50% (uma onda quadrada perfeita). Alterar este valor modifica o conteúdo harmônico, uma técnica conhecida como Modulação por Largura de Pulso (PWM).

### Wavetables: `setWavetable()`, `registerWavetable()`
Wavetables são arrays contendo um único ciclo de uma forma de onda personalizada.

*   `void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth);`
    *   Atribui uma wavetable diretamente a uma única voz.
    *   `data`: Ponteiro para o array de dados da amostra.
    *   `size`: O número de amostras no array.
    *   `depth`: A profundidade de bits dos dados (`BITS_8` ou `BITS_16`).

*   `void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth);`
    *   Registra uma wavetable em uma tabela de pesquisa global com um `id` único (0-999). Este é o método necessário para usar wavetables com **Instrumentos Estilo Tracker**. É altamente eficiente em memória, pois a wavetable é armazenada apenas uma vez e referenciada pelo seu ID.

### Envelopes (ADSR): `setEnv()`

*   `void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r);`
    *   Define o contorno de volume para uma voz.
    *   `a` (Attack): Tempo em milissegundos para o volume ir de 0 ao máximo.
    *   `d` (Decay): Tempo em ms para o volume cair do máximo até o nível de sustain.
    *   `s` (Sustain): O nível de volume (0-255) mantido enquanto a nota está ligada.
    *   `r` (Release): Tempo em ms para o volume cair do nível de sustain para 0 após `noteOff()` ser chamado.

### Motor de Sampler: `registerSample()`, `Instrument_Sample`
O sampler pode reproduzir áudio PCM pré-gravado.

**Fluxo de trabalho:**
1.  **Converter Áudio:** Use o script `WavToEsp32SynthConverter.py` na pasta `tools` para converter um arquivo `.wav` em um arquivo de cabeçalho `.h`.
2.  **Registrar Amostra:**
    `void registerSample(uint16_t id, const int16_t* data, uint32_t length, uint32_t sampleRate, uint32_t rootFreqCentiHz);`
    *   Em seu `setup()`, chame isso para carregar os dados da amostra do arquivo de cabeçalho no registro do sintetizador.
    *   `id`: Um ID único (0-99) para esta amostra.
    *   `rootFreqCentiHz`: O tom original da amostra (ex: `c4`). Isso é crucial para o pitch shifting correto.
3.  **Definir Zonas:**
    Um `Instrument_Sample` usa um array de `SampleZone` para mapear faixas de frequência para amostras específicas. Isso permite a criação de instrumentos multi-sampleados (ex: uma amostra de piano diferente para cada oitava).
    ```cpp
    // Esta zona mapeia todas as frequências para a amostra ID 0.
    const SampleZone singleZone[] = {{ .lowFreq = 0, .highFreq = 1300000, .sampleId = 0 }};
    ```
4.  **Criar Instrumento:**
    ```cpp
    Instrument_Sample mySampler = {
      .zones = singleZone,
      .numZones = 1,
      .loopMode = LOOP_FORWARD, // LOOP_OFF, LOOP_PINGPONG, LOOP_REVERSE
      .loopStart = 1000,
      .loopEnd = 5000
    };
    ```
5.  **Atribuir e Tocar:**
    `void setInstrument(uint8_t voice, Instrument_Sample* inst);`
    *   Use esta função para anexar o instrumento sampler a uma voz, depois use `noteOn()` para tocá-lo. A frequência passada para `noteOn()` determinará o tom de reprodução.

### Instrumentos Estilo Tracker: `Instrument`
Este recurso avançado permite que uma única nota dispare uma sequência de diferentes timbres e volumes. É perfeito para criar batidas percussivas, sons de laser ou pads evolutivos.

A estrutura `Instrument` contém ponteiros para arrays que definem as sequências para as fases de ataque e release.

```cpp
// Exemplo: Um som de bumbo
// Ataque: Começa com um "clique" ruidoso, depois uma queda rápida de tom com uma onda senoidal.
const int16_t kick_attack_waves[] = { W_NOISE, W_SINE };
const uint8_t kick_attack_vols[]  = { 255, 127 };

Instrument kickInstrument = {
  .seqVolumes = kick_attack_vols,
  .seqWaves = kick_attack_waves,
  .seqLen = 2,
  .seqSpeedMs = 20, // Cada passo dura 20ms

  .susVol = 0,         // Sem som durante a sustentação
  .susWave = W_SINE,

  .relLen = 0 // Sem fase de release
};
```
Para usar, atribua-o com `setInstrument(voice, &kickInstrument)` e dispare-o com `noteOn()`.

*   `void detachInstrumentAndSetWave(uint8_t voice, WaveType type);`
    *   Uma função utilitária para remover qualquer Instrumento (Tracker ou Sampler) de uma voz e redefini-la para uma forma de onda básica. Essencial para reutilizar vozes com segurança em um setup polifônico.

### Modulação e Efeitos

*   `void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz);`
    *   Adiciona um LFO de pitch (vibrato). `depthCentiHz` controla a quantidade de desvio de tom.

*   `void setTremolo(uint8_t voice, uint32_t rateCentiHz, uint16_t depth);`
    *   Adiciona um LFO de amplitude (tremolo). `depth` é a intensidade de 0-255.

*   `void slideTo(uint8_t voice, uint32_t endFreqCentiHz, uint32_t durationMs);`
    *   Desliza o tom de uma voz de sua frequência atual para uma frequência alvo ao longo de uma duração definida.

*   `void setArpeggio(uint8_t voice, uint16_t durationMs, Args... freqs);`
    *   Uma função de template para criar uma sequência de notas. Quando você chama `noteOn()` nesta voz, ela irá ciclar automaticamente através das `freqs` fornecidas, com cada nota durando `durationMs`.
    ```cpp
    // Cria um arpejo de Dó menor
    synth.setArpeggio(0, 150, c4, ds4, g4, c5);
    synth.noteOn(0, c4, 255); // Dispara o arpejo
    ```

### Getters e Status

*   `bool isVoiceActive(uint8_t voice);`
    *   Retorna `true` se a voz estiver tocando atualmente (ou seja, não no estado `ENV_IDLE`). Útil para encontrar uma voz livre.

*   `uint8_t getOutput8Bit(uint8_t voice);`
    *   Retorna o nível de saída final atual de uma voz (0-255), após a aplicação de volume e envelope. Útil para lógica de "roubo de voz" ou para criar visualizações.

### Utilitários: `ESP32SynthNotes.h`
A biblioteca inclui `ESP32SynthNotes.h`, que fornece:
*   Constantes pré-definidas para frequências de notas em CentiHz (ex: `c4`, `fs5`, `a4`).
*   Uma função de ajuda inline `midiToFreq(uint8_t note)` para converter um número de nota MIDI padrão (0-127) para a frequência correspondente em CentiHz.

---

## 6. Gerenciamento de Vozes para Polifonia

A biblioteca lhe dá controle total sobre as 80 vozes, mas **não** as aloca automaticamente. Você deve implementar sua própria lógica para encontrar uma voz livre para tocar uma nova nota. Uma estratégia comum é o "roubo de voz".

```cpp
int findFreeVoice() {
  // 1. Encontra a primeira voz completamente inativa.
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!synth.isVoiceActive(i)) {
      return i;
    }
  }

  // 2. Se todas as vozes estiverem ativas, encontre a mais silenciosa para "roubar".
  // Provavelmente será uma voz em sua fase de release.
  uint8_t lowestVol = 255;
  int quietestVoice = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    uint8_t vol = synth.getOutput8Bit(i);
    if (vol < lowestVol) {
      lowestVol = vol;
      quietestVoice = i;
    }
  }
  return quietestVoice;
}
```

---

## 7. Ferramentas Inclusas

O diretório `tools` contém scripts Python úteis:

*   `tools/Wavetables/WavetableMaker.py`: Gera wavetables a partir de várias fontes.
*   `tools/Samples/WavToEsp32SynthConverter.py`: Converte arquivos `.wav` padrão em arquivos de cabeçalho `.h` compatíveis com o motor de sampler.

---

## 8. Solução de Problemas

*   **Áudio tem cliques, estalos ou distorção:**
    1.  **Verifique os Requisitos de Hardware:** Certifique-se de que está usando um ESP32 dual-core de 240 MHz suportado.
    2.  **Fonte de Alimentação:** Use uma fonte de alimentação limpa de 3.3V. Ruído na linha de alimentação é uma causa muito comum de áudio ruim.
    3.  **Fiação I2S:** Mantenha os fios I2S (BCK, WS, DATA) o mais curtos possível e garanta uma conexão GND sólida entre o ESP32 e seu DAC.
    4.  **Amplificador:** A saída de um DAC é de nível de linha. **Não a conecte diretamente a um alto-falante.** Você precisa de um amplificador externo.

*   **ESP32 trava ou reinicia (Watchdog Timer) e uso de `delay()`:**
    *   Como a síntese de áudio roda em uma tarefa separada de alta prioridade (`audioTask`) fixada no Core 1, usar a função `delay()` padrão do Arduino no seu `loop()` **não causará falhas ou engasgos no áudio**. O escalonador do FreeRTOS garantirá que a `audioTask` sempre receba tempo de CPU.
    *   Você pode usar `delay()` tranquilamente para aplicações simples. No entanto, saiba que delays muito longos (muitos segundos) ainda podem potencialmente disparar o Task Watchdog Timer (TWDT) do ESP32, o que pode causar uma reinicialização. Para aplicações complexas que precisam se manter responsivas, ainda é uma boa prática usar temporizadores não-bloqueantes com `millis()`.

*   **Erros de compilação sobre funções `i2s` ou `dac`:**
    *   O pacote de suporte à placa ESP32 na sua IDE do Arduino provavelmente está desatualizado. Atualize-o através do Gerenciador de Placas.
