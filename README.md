# ESP32Synth v2.4.1 — Ultra-Fast Bare-Metal Synth Engine for Maximum Polyphony

<p align="center">
  <img src="https://raw.githubusercontent.com/danilogcrf2-oss/ESP32Synth/main/banner.jpg" alt="ESP32Synth banner" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-2.4.1-green.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-ESP32-orange.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License">
  <img src="https://img.shields.io/badge/performance-extreme-red.svg" alt="Performance">
</p>

**[English]** A high-performance, polyphonic audio synthesis library for the ESP32. Engineered for applications requiring extreme optimization, zero-latency audio, massive polyphony (up to 500 voices on ESP32-S3), custom DSP hooks, Bluetooth/Wi-Fi output, and direct SD card audio streaming.

**[Português]** Uma biblioteca de síntese de áudio polifônica de extrema performance para o ESP32. Projetada para aplicações que exigem otimização brutal, zero latência, polifonia massiva (até 500 vozes no S3), injeção de efeitos DSP, saída para Bluetooth/Wi-Fi e streaming direto do cartão SD.

> *"Se Deus não existisse, esse projeto também não existiria. Tudo só foi possível por causa d'Ele."*

---

### 📝 Developer's Note / Nota do Desenvolvedor

> Muito obrigado por usar o ESP32Synth! 
>
> O ESP32Synth foi concebido e exaustivamente testado em um ESP32 DevKit V1 (ESP32-D0WD-V3) a 240MHz e em um ESP32-S3 Zero. O objetivo sempre foi um só: transformar um microcontrolador barato em um sintetizador absurdamente rápido, polifônico e de alta fidelidade.
> 
> **Novidades da v2.4.1:** Esta versão traz dois saltos gigantescos de arquitetura. Primeiro, recriamos o modo **PWM (SMODE_PWM)** em nível de silício puro. O áudio agora é gerado a 48kHz cravados via interrupções de hardware (ISR do LEDC), sem usar *GPTimers*, o que entrega um áudio 10-bit cristalino em um único pino sem o uso de DAC externo. Segundo, abrimos o motor para **Bluetooth A2DP e Wi-Fi** via `SMODE_CUSTOM`. Agora você pode puxar as amostras de áudio estéreo diretamente da memória sob demanda, sem bloquear o RTOS.
>
> Deixe a criatividade fluir! Você pode criar de chiptunes até tocar WAVs em segundo plano de um SD, tudo simultaneamente. 
> Lembre-se: no DSP customizado, *fuja de floats e divisões*. Use a matemática brutal dos shifts (`>>`) e inteiros. A comunidade e a performance do seu projeto agradecem!

---

## 📖 Table of Contents / Índice

1. [🇺🇸 English Documentation](#-english-documentation)
   - [1. Overview & Key Features](#1-overview--key-features)
   - [2. Under the Hood (Architecture & Limits)](#2-how-it-works-under-the-hood-internal-architecture)
   -[3. Definitive API Guide (How to Use EVERYTHING)](#3-definitive-api-guide)
   -[4. Custom Output: Bluetooth A2DP & Wi-Fi](#4-custom-output-bluetooth-a2dp--wi-fi-new)
   -[5. Advanced DSP & Custom Waves](#5-advanced-dsp--custom-waves)
2.[🇧🇷 Documentação em Português](#-documentação-em-português) *(Abaixo da seção em Inglês)*
3. [🛠 Tools / Ferramentas](#-tools--ferramentas)
4.[⚠️ Troubleshooting](#-common-troubleshooting)

---

# 🇺🇸 English Documentation

## 1. Overview & Key Features

**ESP32Synth** is not just a simple beep generator; it's a full-fledged, studio-grade mixing and synthesis engine written bare-metal over the ESP-IDF.

* **Extreme Polyphony:** Comfortably supports **80 simultaneous voices** out of the box, with an engine capable of pushing up to **500 voices** on the ESP32-S3.
* **Versatile Outputs (NEW SMODE_PWM):** Output crystal-clear audio via external I2S DACs, PDM, internal 8-bit DAC, or directly to a single GPIO using our custom bare-metal LEDC PWM interrupt.
* **Low-Level Access:** A powerful *Hooks* system (`setCustomDSP`, `setCustomWave`, `setCustomControl`) to inject your own algorithms (Reverb, Delays) into the render loop.
* **Lo-Fi Engine:** Native *Bitcrush* and bit-depth volume reduction for dirty, retro, Chiptune-style sounds.
* **Flexible Oscillators:** Sine, Triangle, Sawtooth, Pulse (adjustable PWM), Noise, *Wavetables*, RAM Samplers, and *Custom Waves*. Change them on the fly in `O(1)` time.
* **Decoupled SD Streaming:** Play up to 4 heavy WAV files simultaneously managed by a background Ring Buffer task.
* **Full Modulation:** Independent ADSR Envelopes, LFOs (Vibrato, Tremolo), Portamento (Pitch/Volume slides), and a built-in Arpeggiator.

---

## 2. How It Works: Under the Hood (Internal Architecture)

To achieve massive polyphony on an embedded MCU, slow operations like `float` math, divisions (`/`), and branching have been eradicated from the audio path. 

### A. The Limits of Silicon (Polyphony Scale)
Configure `MAX_VOICES` in `ESP32Synth_Config.hpp`. Here is how the hardware behaves:
* **80 Voices (Default):** The sweet spot. Audio is crystal clear, RAM usage is low, and Core 0 is idle for Wi-Fi/LVGL.
* **150 Voices (RAM Safe Max):** For heavy multi-track midi playback. Consumes a larger chunk of the Heap but maintains absolute stability.
* **340 (ESP32) / 500 (ESP32-S3) Voices (Engine Limit):** Pushing the ESP32 to its physical limits. The audio renders flawlessly, but Core 1 is fully occupied.
* **Beyond Limits (Starvation):** Render takes longer than playback. FreeRTOS panics (WDT). Respect the physics of the chip!

### B. Math & Memory
We use **16.16 Fixed-Point** arithmetic for pitch and phase calculations, and heavy `int64_t` bit-shifts (`>> 16`) for brutal, instantaneous volume precision. The `renderLoop()` runs on a dedicated **Core 1 Task** using `IRAM_ATTR` to stay in ultra-fast RAM.

---

## 3. Definitive API Guide

Here is your complete manual to wielding the absolute power of ESP32Synth.

### 1. Initialization & Audio Output Modes
ESP32Synth supports 5 modes of operation. Choose the one that fits your hardware.
```cpp
ESP32Synth synth;

void setup() {
    // MODE 1: Standard I2S (Recommended, best quality. Needs external DAC like PCM5102A)
    // Args: DataPin, OutputMode, BckPin, WsPin, BitDepth
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);

    // MODE 2: PWM (NEW! 10-bit single-pin audio, no external hardware needed!)
    // Just add a simple RC filter to pin 25.
    // synth.begin(25, SMODE_PWM, -1, -1, I2S_16BIT);

    // MODE 3: PDM (Great for ESP32-S3 single pin output)
    // synth.begin(2, SMODE_PDM, 4, -1, I2S_16BIT); 

    // MODE 4: Classic 8-bit DAC (Only on classic ESP32, pins 25 or 26)
    // synth.begin(25);

    // Set Master Volume (0-255 based on default 8-bit depth config)
    synth.setMasterVolume(255); 
}
```

### 2. Playing Notes (Pitch & Volume)
Include `ESP32SynthNotes.h` for easy note mapping. The library maps notes using "CentiHz". All musical flats (bemóis) must be converted to their previous sharp (sustenidos) equivalent (e.g., use `ds4` instead of `ef4`).

```cpp
// Voice 0, C4 (Middle C), Volume 255
synth.noteOn(0, c4, 255);

// Release note (triggers Release phase of ADSR)
synth.noteOff(0);

// Change volume or frequency while playing
synth.setVolume(0, 127);
synth.setFrequency(0, cs4); // C#4
```

### 3. Waveforms & Pulse Width (PWM)
Switch waves seamlessly in `O(1)` time without glitches.
```cpp
synth.setWave(0, WAVE_PULSE);

// Pulse Width goes from 0 to 255 (by default). 128 is a perfect 50% square.
synth.setPulseWidth(0, 128); 
```

### 4. ADSR Envelopes
Control how the sound evolves over time. Times are in milliseconds, Sustain is an amplitude level (0-255).
```cpp
// Voice 0 | Attack: 10ms | Decay: 300ms | Sustain Lvl: 127 | Release: 1500ms
synth.setEnv(0, 10, 300, 127, 1500);
```

### 5. LFOs (Vibrato & Tremolo) & Portamento (Slides)
```cpp
// Vibrato: Voice 0, Rate: 5Hz (500 CentiHz), Depth: 20Hz (2000 CentiHz)
synth.setVibrato(0, 500, 2000);

// Tremolo: Voice 0, Rate: 4Hz (400 CentiHz), Depth: 100 (out of 255)
synth.setTremolo(0, 400, 100);

// Pitch Slide (Portamento): Slide to C5 exactly over 1000 milliseconds
synth.slideFreqTo(0, c5, 1000);

// Volume Fade: Fade down to 0 over 2 seconds
synth.slideVolTo(0, 0, 2000);
```

### 6. Arpeggiator
Built-in hardware arpeggiator per voice.
```cpp
// Voice 0, speed: 150ms per step, Notes: C4, E4, G4, C5
synth.setArpeggio(0, 150, c4, e4, g4, c5);
synth.noteOn(0, c4, 255); // Trigger
// To stop: synth.detachArpeggio(0);
```

### 7. SD Card Direct WAV Streaming
Stream huge files in the background without stuttering. Ensure SPI is initialized at high speed (16MHz+).
```cpp
#include <SD.h>
#include <SPI.h>

void setup() {
    SPI.begin(18, 19, 23, 5);
    SD.begin(5, SPI, 16000000); // VITAL for audio stability!

    // Voice 1, FileSystem, Path, Volume (255), Pitch override (c4), Loop (true)
    synth.playStream(1, SD, "/drum_loop.wav", 255, c4, true);

    // Jump to 5 seconds
    synth.seekStreamMs(1, 5000);
}
```

---

## 4. Custom Output: Bluetooth A2DP & Wi-Fi (NEW!)

Version 2.4.1 introduces `SMODE_CUSTOM` and `generateSamples()`. This creates a **Pull Mode** architecture, allowing external libraries (like `ESP32-A2DP`) to request rendered audio exactly when they need it, perfectly synced.

```cpp
ESP32Synth synth;
int16_t bluetoothBuffer[512]; // Buffer for A2DP

void setup() {
    // Initialize in CUSTOM mode with your desired Sample Rate, 
    // passing 'nullptr' prevents the synth from auto-rendering.
    synth.beginCustom(44100, nullptr);
    synth.noteOn(0, c4, 255);
}

// Inside your Bluetooth A2DP or Wi-Fi audio callback:
void a2dp_data_callback(uint8_t *data, int32_t len) {
    int numSamplePairs = len / 4; // 16-bit stereo = 4 bytes per frame

    // The synth natively calculates and formats L/R stereo samples safely
    synth.generateSamplesStereo((int16_t*)data, numSamplePairs);
}
```

---

## 5. Advanced DSP & Custom Waves

### Master Global Effects (Custom DSP)
Intercept the 32-bit mix buffer before the DAC. **Warning: Runs 48,000 times/sec. Do not use floats or slow division!**

```cpp
// Simple 50% Volume Down-Mixer
void IRAM_ATTR myDSP(int32_t* mixBuffer, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        mixBuffer[i] = mixBuffer[i] >> 1; // Divide by 2 instantly
    }
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    synth.setCustomDSP(myDSP);
}
```

### Custom Waves
Create your own oscillator math and assign it to a voice. You can switch back to standard waves anytime via `setWave()`.

```cpp
// FM Feedback Sine Oscillator
void IRAM_ATTR waveFMSine(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    int16_t prevOut = vo->noiseSample; 

    for (int i = 0; i < samples; i++) {
        // Feedback twist (~12%)
        uint32_t modPh = ph + ((int32_t)prevOut << 15); 
        int32_t s = sineLUT[(modPh >> SINE_SHIFT) & SINE_LUT_MASK] >> 16;
        prevOut = (int16_t)s;

        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        mixBuffer[i] += (s * finalVol) >> 16;
        
        ph += inc;
        currentEnv += envStep;
    }
    vo->phase = ph;
    vo->noiseSample = prevOut; 
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_16BIT);
    synth.setCustomWave(0, waveFMSine);
    synth.noteOn(0, c4, 255);
}
```

<br><hr><br>

# 🇧🇷 Documentação em Português

## 1. Visão Geral e Recursos Principais

O **ESP32Synth** é uma engine completa de mixagem e síntese, construída "bare-metal" sobre o ESP-IDF com qualidade de estúdio e extrema eficiência.

* **Polifonia Extrema:** Suporta confortavelmente **80 vozes simultâneas** de fábrica, com o limite estendido para até **500 vozes** no ESP32-S3.
* **Saídas Versáteis (NOVO SMODE_PWM):** Áudio via I2S, PDM, DAC interno, ou agora diretamente em um único pino usando interrupções puras de hardware (PWM 10-bit sem *jitter*).
* **Acesso de Baixo Nível:** Sistema de *Hooks* (`setCustomDSP`, `setCustomWave`, `setCustomControl`) para injetar algoritmos como Reverb no loop mestre.
* **Saída Customizada (A2DP / Wi-Fi):** Modo `SMODE_CUSTOM` que permite que bibliotecas externas puxem o áudio renderizado sob demanda.
* **Osciladores Flexíveis:** Senoidal, Triangular, Saw, Pulso (com PWM), Ruído, *Wavetables*, Samplers e *Custom Waves*. (`O(1)` na troca).
* **Modulação Completa:** ADSR, LFOs (Vibrato/Tremolo), Slides (Portamento) e Arpejador.

---

## 2. Guia Definitivo da API

Abaixo, o manual completo de como dominar cada aspecto do sintetizador.

### 1. Inicialização e Modos de Saída
O modo de saída define como o hardware do ESP32 processa o áudio.
```cpp
ESP32Synth synth;

void setup() {
    // MODO 1: I2S Padrão (Recomendado, melhor qualidade. Usa DAC como PCM5102A)
    // Parâmetros: PinoData, Modo, PinoBck, PinoWs, Profundidade
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);

    // MODO 2: PWM (NOVO! Áudio 10-bit num único pino, direto no silício!)
    // Basta adicionar um filtro RC (Resistor+Capacitor) no pino 25.
    // synth.begin(25, SMODE_PWM, -1, -1, I2S_16BIT);

    // Volume Master vai de 0 a 255 por padrão
    synth.setMasterVolume(255); 
}
```

### 2. Tocando Notas (Sustenidos e Padrão Inglês)
A biblioteca usa a biblioteca auxiliar `ESP32SynthNotes.h` em "CentiHz". Os acidentes são mapeados APENAS em sustenidos (letra `s`). Converta qualquer "bemol" da teoria para o sustenido anterior (ex: Mi Bemol 4 vira Ré Sustenido 4 -> `ds4`).

```cpp
// Voz 0, Dó 4 (Centro do piano), Volume 255
synth.noteOn(0, c4, 255);

// Solta a nota (Aciona a fase de Release do ADSR)
synth.noteOff(0);

// Muda volume e afinação ao vivo sem engasgar
synth.setVolume(0, 127);
synth.setFrequency(0, cs4); // Dó Sustenido 4
```

### 3. Ondas e PWM (Pulse Width)
```cpp
synth.setWave(0, WAVE_PULSE);

// PWM vai de 0 a 255. 128 = Onda Quadrada Perfeita (50%)
synth.setPulseWidth(0, 128); 
```

### 4. ADSR (Envelopes), LFOs e Portamento
```cpp
// Voz 0 | Attack: 10ms | Decay: 300ms | Sustain Nível: 127 | Release: 1500ms
synth.setEnv(0, 10, 300, 127, 1500);

// Vibrato (Oscilação de Pitch): Taxa de 5Hz, Profundidade de 20Hz
synth.setVibrato(0, 500, 2000);

// Tremolo (Oscilação de Volume): Taxa de 4Hz, Profundidade 100 (de 255)
synth.setTremolo(0, 400, 100);

// Portamento: Desliza até o C5 suavemente ao longo de 1 segundo (1000ms)
synth.slideFreqTo(0, c5, 1000);
```

### 5. Arpejador Embutido
```cpp
// Voz 0, Vel: 150ms, Notas: Dó4, Mi4, Sol4, Dó5
synth.setArpeggio(0, 150, c4, e4, g4, c5);
synth.noteOn(0, c4, 255); // Dispara o arpejador
```

### 6. Streaming Direto do Cartão SD (WAV)
```cpp
#include <SD.h>
#include <SPI.h>

void setup() {
    SPI.begin(18, 19, 23, 5);
    SD.begin(5, SPI, 16000000); // 16MHz é OBRIGATÓRIO para evitar cortes!

    // Voz 1, SD, Caminho, Vol, Afinação(c4 mantém original), Loop (true)
    synth.playStream(1, SD, "/bateria.wav", 255, c4, true);
}
```

---

## 3. Saída Customizada: Bluetooth A2DP & Wi-Fi (NOVO!)

A grande novidade da v2.4.1 é o suporte a *Pull Mode*. Se você estiver criando uma caixa de som Bluetooth ou transmitindo áudio por Wi-Fi, você precisa que a engine gere os samples apenas quando o protocolo solicitar, garantindo sincronia perfeita e evitando estouros de buffer.

```cpp
ESP32Synth synth;

void setup() {
    // Inicia a Engine mas NÃO cria a Task de processamento contínuo (Passando 'nullptr')
    synth.beginCustom(44100, nullptr);
    synth.noteOn(0, c4, 255);
}

// Exemplo: Função de Callback da sua biblioteca de Bluetooth (como ESP32-A2DP)
void a2dp_data_callback(uint8_t *data, int32_t len) {
    int numSamplePairs = len / 4; // Em 16-bit estéreo, cada "frame" tem 4 bytes

    // Pede ao Synth para renderizar e já duplicar os canais L e R perfeitamente!
    synth.generateSamplesStereo((int16_t*)data, numSamplePairs);
}
```

---

## 4. DSP Avançado e Ondas Customizadas

Quer injetar código diretamente no motor? Utilize `setCustomDSP` (para interceptar o áudio Mestre antes do DAC) ou `setCustomWave` (para escrever matemática sônica numa única voz). **Regra de Ouro: Nada de `floats` e Nenhuma divisão `/` no loop.**

```cpp
// Exemplo de DSP: Diminui o volume Master em 50% via Shift de bits
void IRAM_ATTR myDSP(int32_t* mixBuffer, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
        mixBuffer[i] = mixBuffer[i] >> 1; // Rápido e instantâneo
    }
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    synth.setCustomDSP(myDSP);
}
```

---

## 🛠 Tools / Ferramentas

Na pasta `tools/` deste repositório você encontra utilitários em Python:
*   `WavetableMaker.py`: Cria *wavetables* a partir de equações ou áudios (`.h`).
*   `WavToEsp32SynthConverter.py`: Converte amostras de áudio pequenas em `.h` (`WAVE_SAMPLE`) para tocar na velocidade absurda da memória RAM, sem SD.

---

## ⚠️ Common Troubleshooting (Solução de Problemas)

*   **Pops, engasgos ou áudio robótico:** Tem certeza de que você não colocou um `delay()` gigantesco no seu `loop()`? Confirme se a placa no Arduino IDE está em **240MHz**.
*   **SD Stream engasgando:** O barramento SPI do seu Arduino está lento demais. Force a velocidade: `SD.begin(5, SPI, 16000000)`. Use **FAT32** com cluster de 32kb/64kb.
*   **PWM sem áudio:** O pino precisa suportar as saídas do hardware LEDC. Caso o som esteja sujo, use um filtro RC simples (um resistor em série e um capacitor para o terra) no pino para suavizar a forma de onda.

---
<p align="center"><i>Construído com paixão, muito café, otimização brutal e fé. ❤️</i></p>