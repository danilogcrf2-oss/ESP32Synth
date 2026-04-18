# ESP32Synth v2.4.0 — Professional Audio Synthesis Engine

<p align="center">
  <img src="https://raw.githubusercontent.com/danilogcrf2-oss/ESP32Synth/main/banner.jpg" alt="ESP32Synth banner" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-2.4.0-green.svg" alt="Version">
  <img src="https://img.shields.io/badge/platform-ESP32-orange.svg" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License">
  <img src="https://img.shields.io/badge/performance-extreme-red.svg" alt="Performance">
</p>

**[English]** A high-performance, polyphonic audio synthesis library for the ESP32. Engineered for professional applications requiring extreme optimization, zero-latency audio, massive polyphony (up to 350+ voices), custom DSP hooks, and direct SD card audio streaming.

**[Português]** Uma biblioteca de síntese de áudio polifônica de extrema performance para o ESP32. Projetada para aplicações profissionais que exigem otimização brutal, zero latência, polifonia massiva (até 350+ vozes), injeção de efeitos DSP customizados e streaming direto do cartão SD.

> *"Se Deus não existisse, esse projeto também não existiria. Tudo só foi possível por causa d'Ele."*

---

### 📝 Developer's Note / Nota do Desenvolvedor

> Muito obrigado por usar o ESP32Synth! 
>
> O ESP32Synth foi concebido e exaustivamente testado em um ESP32 DevKit V1 (ESP32-D0WD-V3) a 240MHz e em um ESP32-S3 Zero da Waveshare. O objetivo sempre foi um só: transformar um microcontrolador barato em um sintetizador absurdamente rápido, polifônico e de alta fidelidade.
> 
> Com ele, você pode criar músicas, efeitos sonoros complexos e trilhas gerativas. Pode emular o som clássico de Chiptune (usando nossa engine de bitcrush e redução de bits nativa) ou reproduzir arquivos WAV pesados de um cartão SD em segundo plano sem engasgar o processador. Deixe a criatividade fluir!
>
> **Um aviso sobre a Arquitetura Aberta (DSP e Custom Waves):** Nesta versão, abri o núcleo da biblioteca. Agora você pode escrever suas próprias funções de ondas matemáticas ou injetar algoritmos de Reverb e Delay direto no buffer de áudio master. No entanto, lembre-se: o código roda a 48.000 vezes por segundo. Use matemática puramente otimizada (como os exemplos que deixei neste README). Evite *floats* e divisões no loop de DSP para não sacrificar a polifonia ou roubar tempo das tasks do FreeRTOS.
>
> Qualquer bug, problema de compilação ou ideia de melhoria, abra uma issue no GitHub. A comunidade agradece!

---

## 📖 Table of Contents / Índice

1. [🇺🇸 English Documentation](#-english-documentation)
   - [1. Overview & Key Features](#1-overview--key-features)
   -[2. Under the Hood (Architecture & Limits)](#2-how-it-works-under-the-hood-internal-architecture)
   - [3. Requirements & Installation](#3-requirements--installation)
   -[4. Definitive API Guide](#4-definitive-api-guide)
   -[5. Advanced DSP & Custom Waves](#5-creating-effects-reverb--custom-waves)
2. [🇧🇷 Documentação em Português](#-documentação-em-português) *(Abaixo da seção em Inglês)*
3.[🛠 Tools / Ferramentas](#-tools--ferramentas)
4. [⚠️ Troubleshooting](#-common-troubleshooting)

---

# 🇺🇸 English Documentation

## 1. Overview & Key Features

**ESP32Synth** is not just a simple beep generator; it's a full-fledged, studio-grade mixing and synthesis engine written bare-metal over the ESP-IDF.

* **Extreme Polyphony:** Comfortably supports **80 simultaneous voices** out of the box, with an engine capable of pushing up to **350 voices** if needed.
* **Low-Level Access (NEW):** A powerful *Hooks* system (`setCustomDSP`, `setCustomWave`, `setCustomControl`) allowing you to inject your own effect algorithms (Reverb, Delays) and waveforms directly into the I2S render loop.
* **Lo-Fi Engine (NEW):** Native *Bitcrush* control and bit-depth volume reduction for dirty, retro, Chiptune-style sounds.
* **Flexible Oscillators:** Sine, Triangle, Sawtooth, Pulse (with adjustable PWM), Noise (fast LCG), *Wavetables*, RAM Samplers, and *Custom Waves*. Seamlessly switch between any wave type on the fly in `O(1)` time.
* **Decoupled SD Streaming:** Play up to 4 heavy WAV files simultaneously. The SD card is managed by a background Ring Buffer task, guaranteeing the main audio thread never stutters.
* **Full Modulation:** Independent ADSR Envelopes, LFOs (Vibrato, Tremolo), Portamento (Absolute pitch and volume slides), and a built-in Arpeggiator.

---

## 2. How It Works: Under the Hood (Internal Architecture)

To achieve massive polyphony on an embedded MCU, slow operations like `float` math, divisions (`/`), and branching (complex `if/else` chains) have been eradicated from the audio path. 

### A. The Limits of Silicon (Polyphony Scale)
The library allows you to configure `MAX_VOICES` in the header file. Here is how the ESP32 behaves across the spectrum:
* **80 Voices (Default):** The sweet spot. Audio is crystal clear, RAM usage is low, and the CPU has plenty of idle time to handle Wi-Fi, displays (LVGL), and sensor reading on Core 0.
* **140 Voices (RAM Safe Max):** For heavy multi-track midi playback. It consumes a larger chunk of the Heap for voice data structures but maintains absolute stability.
* **350 Voices (Engine Limit):** Pushing the ESP32 to its absolute limits. The audio renders flawlessly, but Core 1 is heavily occupied.
* **364+ Voices (The Abyss/Starvation):** At this threshold, the render loop takes longer to compute a block of audio than it takes to play it. FreeRTOS has no CPU ticks left to manage basic system tasks. The result? Audio jitter, RTOS starvation, and eventual Watchdog Timer (WDT) panics. *It proves the sheer brute force of the library—but respect the limits of physics!*

### B. 16.16 Fixed-Point Math & 32-bit Accumulators
We *never* use `float` or `double` during audio rendering. The synth maps frequencies using 32-bit phase accumulators. To control pitch and read speeds, we use **16.16 Fixed-Point** arithmetic: 16 bits for the integer part, 16 bits for the fractional. Volume math utilizes heavy `int64_t` bit-shifts (`>> 16`) for brutal, instantaneous precision.

### C. Dedicated Core 1 & IRAM_ATTR
The `renderLoop()` is a FreeRTOS Task pinned to **Core 1** with maximum priority. Critical rendering functions use the `IRAM_ATTR` flag. This forces the ESP32 to load the code into the ultra-fast internal RAM, bypassing the massive latency of fetching instructions from Flash memory (Cache Misses).

### D. Separated Rates (Audio vs. Control)
It is inefficient to calculate the ADSR envelope or Vibrato LFO 48,000 times per second. 
1.  **Audio Rate (48kHz):** Processes only oscillators, phase increments, and buffer sums.
2.  **Control Rate (Default 100Hz):** Wakes up only every ~480 audio samples to recalculate pitch slides, advance LFO phases, and update ADSR state machines. 

### E. Safe Mixing (Headroom)
Summing dozens of 16-bit voices would instantly clip (distort) the signal. To prevent this, the internal `mixBuffer` uses **32-bit integers**. Voices are summed freely with massive headroom, and only at the very final output stage is the signal scaled by the Master Volume, bitcrushed (if enabled), and cleanly down-sampled to fit the 16-bit or 32-bit I2S hardware.

---

## 3. Requirements & Installation

* **Hardware:** ESP32 Classic or ESP32-S3 (Dual Core @ 240MHz). *Single-Core variants (S2, C3) are not recommended due to RTOS task collision.*
* **External I2S DAC (Highly Recommended):** Modules like the **PCM5102A** or **UDA1334A** guarantee studio-quality audio. The ESP32's internal DAC has a high noise floor and is only 8-bit native.
* **Installation:** 
  1. Download this repository as a `.ZIP` or search for "ESP32Synth" in the Arduino IDE Library Manager.
  2. *Requires ESP32 Board Core version 3.0.0 or newer.*
## 4. Definitive API Guide

Here we explain how to wield the absolute power of the ESP32Synth engine.

### Initialization & Global Configuration
```cpp
ESP32Synth synth;

void setup() {
    // Initialize I2S in 32-bit format. 
    // Recommended DAC Pins (e.g., PCM5102A): BCK=4, WS=15, DATA=2
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    
    // Set the calculation rate for envelopes and LFOs (default 100Hz)
    // Higher = smoother slides, but uses slightly more CPU.
    synth.setControlRateHz(200);

    // [NEW] Reduces master audio quality to 8-bits, creating a gritty Lo-Fi effect
    synth.setMasterBitcrush(8);

    // [NEW] Reduces the base resolution for dynamic internal volume calculations
    synth.setVolDepthBase(8);

    // Volume is a uint16_t. If VolDepthBase is 8 (default), the maximum is 255.
    synth.setMasterVolume(255); 
}
```

### Triggering Notes & Seamless Wave Switching
The library handles notes using "CentiHz" (Hz * 100). Include `ESP32SynthNotes.h` to use standard constants like `c4` (Middle C = 261.63 Hz). Thanks to the `O(1)` Jump Table architecture, you can switch waveforms *instantly*, even while a note is currently sounding, without audio glitches or memory leaks.
```cpp
// Voice 0, C4 (c4), Volume 255
synth.noteOn(0, c4, 255);

// Change the wave of Voice 0 to a Square/Pulse wave LIVE!
synth.setWave(0, WAVE_PULSE);

// Change the Pulse Width (PWM) of the square wave (0 to 255)
synth.setPulseWidth(0, 128); // 128 = 50% = Perfect square wave

// Release the key (starts the Release phase of the ADSR envelope)
synth.noteOff(0);
```

### Pitch & Volume Sliding (Portamento)
Slides use Bresenham's line algorithm for absolute integer precision, moving gracefully over time without expensive float mathematics.
```cpp
synth.noteOn(0, c4, 255);
// Slide from C4 to C5 taking exactly 1000 milliseconds
synth.slideFreqTo(0, c5, 1000);

// Gradually fade the volume to zero over 2 seconds
synth.slideVolTo(0, 0, 2000);
```

### Configuring the ADSR (Envelope)
Attack, Decay, and Release are defined in milliseconds. Sustain is an amplitude level.
```cpp
// Attack = 10ms (Fast punch)
// Decay = 300ms
// Sustain = 127 (Half volume)
// Release = 1500ms (Long, fading tail)
synth.setEnv(0, 10, 300, 127, 1500);
```

### Direct WAV File Streaming (SD Card)
Streaming heavy WAV files won't stutter your synth, but it requires your SD module to be initialized at high speeds (16MHz to 20MHz) on standard hardware SPI pins.
```cpp
#include <SD.h>
#include <SPI.h>

void setup() {
    SPI.begin(18, 19, 23, 5);
    SD.begin(5, SPI, 16000000); // 16MHz is VITAL for audio stability!

    // Playback: Voice 1, FileSystem, Path, Volume (255), Pitch (c4), Loop (true)
    synth.playStream(1, SD, "/drum_loop.wav", 255, c4, true);

    // Jump to the 5-second mark (5000 ms) of the audio track
    synth.seekStreamMs(1, 5000);
}
```

---

## 5. Advanced DSP & Custom Waves

With version 2.4.0, **ESP32Synth** has opened its core engine for you to inject high-performance code.

### A. Master Global Effects (Reverb, Delay)
By using `setCustomDSP`, you intercept the final 32-bit mix buffer BEFORE it goes to the DAC. 
**Warning:** This code runs 48,000 times per second. It must be brutally optimized. Do not use modulo (`%`) for buffer wrapping, and avoid floats. 

Here is an example of a **Studio-Grade Tape Reverb with Analog Saturation and a DC Blocker**:

```cpp
#define TAPE_LEN 20000 
int32_t* reverbTape;
int writeHead = 0;

int32_t dcBlockerPrevWet = 0;
int32_t dcBlockerState = 0;
int32_t lpState = 0; 

// IRAM_ATTR forces this function into ultra-fast RAM!
void IRAM_ATTR reverbDSP(int32_t* mixBuffer, int numSamples) {
    if (!reverbTape) return; 

    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];

        // 1. Safe Circular Buffer Reads (No slow '%' operator)
        // We choose prime numbers smaller than TAPE_LEN for the delay taps to avoid resonance buildup.
        int tap1 = writeHead - 4327;  if (tap1 < 0) tap1 += TAPE_LEN;
        int tap2 = writeHead - 11003; if (tap2 < 0) tap2 += TAPE_LEN;
        int tap3 = writeHead - 19013; if (tap3 < 0) tap3 += TAPE_LEN;

        // Sum the 3 heads and divide by 4 (>> 2) to prevent clipping
        int32_t wet = (reverbTape[tap1] >> 2) + (reverbTape[tap2] >> 2) + (reverbTape[tap3] >> 2);

        // 2. DC Blocker (Crucial High-Pass Filter!)
        // Kills any standing low-frequency energy that would cause an infinite noise loop.
        int32_t dcBlocked = wet - dcBlockerPrevWet + ((dcBlockerState * 253) >> 8);
        dcBlockerPrevWet = wet;
        dcBlockerState = dcBlocked;

        // 3. Low-Pass Filter (Dampens the echoes over time, creating warmth)
        lpState = ((dcBlocked * 50) + (lpState * 206)) >> 8; 

        // 4. Calculate Feedback to record back to the tape (~78% feedback)
        int32_t feedback = (dry >> 1) + ((lpState * 200) >> 8);

        // 5. ANALOG SATURATION (Safety Soft-Clipping)
        // If the math tries to explode, we crush it cleanly at the 16-bit limit.
        if (feedback > 32767) feedback = 32767;
        else if (feedback < -32768) feedback = -32768;

        // Write to tape and advance the head
        reverbTape[writeHead] = feedback;
        writeHead++;
        if (writeHead >= TAPE_LEN) writeHead = 0;

        // Master Mix: Dry signal + Processed Reverb tail
        mixBuffer[i] = dry + lpState;
    }
}

void setup() {
    // Safely allocate memory in RAM (Heap)
    reverbTape = (int32_t*)heap_caps_calloc(TAPE_LEN, sizeof(int32_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    
    // INJECT THE REVERB INTO THE ENGINE!
    if (reverbTape) synth.setCustomDSP(reverbDSP);
}
```

### B. Writing Your Own Oscillator (Custom Waves)
If the standard waves aren't enough, create your own sonic math using `setCustomWave()`. And because of our Jump Table design, **you can switch back to a normal wave at any moment** using `setWave(voice, WAVE_SINE)`. The engine handles the transition flawlessly.

Here is an example of an **FM Feedback Sine Wave**. The wave modulates its own phase based on its previous output, creating a rich, metallic resonance commonly found in classic FM synthesizers (like the Yamaha DX7).

```cpp
void IRAM_ATTR waveFMSine(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    
    // We repurpose 'noiseSample' variable to store our previous output state
    int16_t prevOut = vo->noiseSample; 

    for (int i = 0; i < samples; i++) {
        // THE MAGIC: Phase modulation via feedback.
        // By shifting by 15, the feedback twists the wave by ~12%. 
        // This is the "sweet spot" for musical resonance before it collapses into noise.
        uint32_t modPh = ph + ((int32_t)prevOut << 15); 
        
        // Fetch from the engine's built-in Sine LUT
        int32_t s = sineLUT[(modPh >> SINE_SHIFT) & SINE_LUT_MASK] >> 16;
        prevOut = (int16_t)s;

        // Apply Envelope and Volume, then sum into the final mix
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        mixBuffer[i] += (s * finalVol) >> 16;
        
        // Advance phase and envelope
        ph += inc;
        currentEnv += envStep;
    }
    
    // Save state for the next render block
    vo->phase = ph;
    vo->noiseSample = prevOut; 
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_16BIT);
    
    // Bind Voice 0 to use your custom FM Sine function
    synth.setCustomWave(0, waveFMSine);
    synth.noteOn(0, c4, 255);
    
    // If you want to switch back to a standard saw wave later, just do:
    // synth.setWave(0, WAVE_SAW);
}
```

### C. The Maestro: Generative Algorithms (Custom Control)
Need something to control the synth but don't want to block your main `loop()` or deal with millis() timers? `setCustomControl` runs integrated tightly into the Synth's "Control Rate" (default 100Hz). Great for sequencers and generative music.

```cpp
uint32_t ticks = 0;

void IRAM_ATTR theMaestroControl() {
    ticks++;
    
    // An LFO controlling Voice 1's PulseWidth (PWM) automatically over time
    uint8_t breathPWM = 128 + (sin(ticks * 0.05) * 110);
    synth.setPulseWidth(1, breathPWM);

    // Random generative Arpeggiator every 12 ticks
    if (ticks % 12 == 0) {
        if (random(0, 3) == 0) { 
            synth.noteOn(0, c5, random(50, 150)); 
        }
    }
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    // Bind the generative function to the control loop
    synth.setCustomControl(theMaestroControl);
}
```
<br><hr><br>

# 🇧🇷 Documentação em Português

## 1. Visão Geral e Recursos Principais

O **ESP32Synth** não é apenas um gerador de bipes; é uma engine completa de mixagem e síntese, construída "bare-metal" sobre o ESP-IDF com qualidade de estúdio e extrema eficiência de CPU.

* **Polifonia Extrema:** Suporta confortavelmente **80 vozes simultâneas** de fábrica, com uma engine capaz de empurrar até **350+ vozes** se necessário.
* **Acesso de Baixo Nível (NOVO):** Um poderoso sistema de *Hooks* (`setCustomDSP`, `setCustomWave`, `setCustomControl`) que permite injetar seus próprios algoritmos de efeitos (como Reverb, Delays) e geradores de ondas diretamente no loop de renderização I2S.
* **Lo-Fi Engine (NOVO):** Controle nativo de *Bitcrush* e redução de profundidade de volume (Bit-depth) para criar timbres sujos, retrôs e no mais puro estilo Chiptune.
* **Osciladores Flexíveis:** Senoidal, Triangular, Dente de Serra, Pulso (com PWM ajustável), Ruído (LCG rápido), *Wavetables*, Samplers de RAM e *Custom Waves*. Alterne entre qualquer tipo de onda instantaneamente em tempo de execução (`O(1)`).
* **Streaming SD Desacoplado:** Toque até 4 arquivos WAV pesados simultaneamente. O cartão SD é gerenciado por uma task em segundo plano usando Ring Buffers, garantindo que o processamento de áudio principal nunca engasgue.
* **Modulação Completa:** Envelopes ADSR independentes, LFOs (Vibrato, Tremolo), Portamento (Slides de pitch e volume absolutos) e um Arpejador integrado.

---

## 2. Como Funciona: Sob o Capô (Arquitetura Interna)

Para alcançar uma polifonia massiva em um microcontrolador embarcado, funções lentas como matemática `float`, divisões (`/`) e ramificações complexas (cadeias de `if/else`) foram completamente extirpadas do caminho de áudio. Veja a mágica operando nos bastidores:

### A. Os Limites do Silício (Escala de Polifonia)
A biblioteca permite configurar `MAX_VOICES` direto no header. Veja como o hardware do ESP32 se comporta nessa escala:
* **80 Vozes (Padrão):** O "Sweet Spot". O áudio é cristalino, o uso de RAM é baixo e a CPU tem muito tempo ocioso para gerenciar Wi-Fi, displays (LVGL) e sensores no Núcleo 0.
* **140 Vozes (RAM Safe Max):** Ideal para reprodução pesada de trilhas MIDI completas. Consome uma boa fatia do Heap para as estruturas das vozes, mas mantém estabilidade absoluta.
* **350 Vozes (Limite da Engine):** Empurrando o ESP32 ao limite absoluto da matemática. O áudio ainda renderiza sem falhas, mas o Núcleo 1 fica totalmente ocupado.
* **364+ Vozes (O Abismo / Starvation):** Neste limite, o loop de renderização demora mais tempo para calcular um bloco de áudio do que o tempo físico para tocá-lo. O FreeRTOS fica sem "ticks" de CPU para gerenciar tarefas básicas do sistema. O resultado? Jitter no áudio, interrupção das tasks (Starvation) e pânicos no Watchdog Timer (WDT). *Isso prova a força bruta assustadora da biblioteca — mas respeite os limites da física!*

### B. Matemática de Ponto Fixo (Fixed-Point 16.16) e Acumuladores 32-bits
*Nunca* usamos `float` ou `double` durante a renderização. O sintetizador mapeia as frequências usando acumuladores de fase de 32-bits. Para controlar pitch e velocidades de leitura da memória, usamos aritmética **16.16 Fixed-Point**: 16 bits para a parte inteira e 16 bits para a fracionária. Multiplicações de volume utilizam castings pesados para `int64_t` temporários seguidos de bit-shifts (`>> 16`). O resultado é uma precisão matemática brutal e instantânea.

### C. Core 1 Dedicado e IRAM_ATTR
O `renderLoop()` roda em uma **FreeRTOS Task fixada no Núcleo 1** com prioridade máxima. As funções críticas de renderização possuem a flag `IRAM_ATTR`. Isso força o ESP32 a carregar o código na memória RAM interna ultra-rápida, contornando a latência absurda de buscar instruções na memória Flash (Cache Misses).

### D. A Separação: Audio Rate vs Control Rate
Não faz sentido calcular o envelope (ADSR) ou o LFO (Vibrato) 48.000 vezes por segundo. 
1.  **Audio Rate (48kHz):** Processa apenas osciladores, incrementos de fase e somas de buffer.
2.  **Control Rate (100Hz Padrão):** Acorda apenas a cada ~480 amostras de áudio para recalcular os slides de pitch, avançar as fases dos LFOs e atualizar a máquina de estados dos Envelopes.

### E. Mixagem Segura (Headroom)
Somar dezenas de vozes de 16-bits causaria clipagem (distorção) imediata. Para evitar isso, o buffer interno de mixagem (`mixBuffer`) usa números inteiros de **32-bits**. As vozes são somadas livremente com um Headroom gigantesco. Apenas no último estágio de saída, o sinal é escalado pelo Volume Master, processado pelo Bitcrusher (se ativo) e rebaixado limpidamente para caber na saída 16-bit ou 32-bit do hardware I2S.

---

## 3. Requisitos e Instalação

* **Hardware:** ESP32 Clássico ou ESP32-S3 (Dual Core a 240MHz). *Variantes Single-Core (S2, C3) não são recomendadas devido à colisão de tasks no RTOS.*
* **DAC I2S Externo (Altamente Recomendado):** Módulos como o **PCM5102A** garantem qualidade de estúdio (16 ou 32-bit). O DAC interno do ESP32 tem muito ruído de fundo e é limitado a 8 bits.
* **Instalação:** 
  1. Baixe como arquivo `.ZIP` ou pesquise por "ESP32Synth" no Library Manager do Arduino IDE.
  2. *Requer ESP32 Board Core versão 3.0.0 ou superior.*

---

## 4. Guia Definitivo da API

Aqui explicaremos como dominar o poder absoluto da engine do ESP32Synth.

### Inicialização e Configuração Global
```cpp
ESP32Synth synth;

void setup() {
    // Inicializa I2S no formato 32-bit. 
    // Pinos recomendados (ex: PCM5102A): BCK=4, WS=15, DATA=2
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    
    // Taxa de recálculo dos envelopes e LFOs (padrão 100Hz)
    // Valores maiores deixam slides mais suaves, mas usam um pouco mais de CPU.
    synth.setControlRateHz(200);

    // [NOVO] Reduz a qualidade do áudio master para 8-bits (Efeito Lo-Fi rasgado)
    synth.setMasterBitcrush(8);

    // [NOVO] Reduz a resolução base de cálculos de volume dinâmico interno
    synth.setVolDepthBase(8);

    // O volume é uint16_t. Com VolDepthBase em 8 (padrão), o máximo é 255.
    synth.setMasterVolume(255); 
}
```

### Acionando Notas e Alternância de Ondas Nativa
A biblioteca entende notas usando "CentiHz" (Hz * 100). Inclua `ESP32SynthNotes.h` para usar constantes como `c4` (Dó 4 = 261.63 Hz). Graças à arquitetura de *Jump Tables* `O(1)`, você pode **alternar os tipos de onda instantaneamente**, inclusive enquanto a nota está tocando, sem estalos ou vazamento de memória!

```cpp
// Voz 0, Dó 4 (c4), Volume 255
synth.noteOn(0, c4, 255);

// Muda a onda da Voz 0 para Quadrada/Pulso AO VIVO!
synth.setWave(0, WAVE_PULSE);

// Muda a largura do pulso (PWM) da onda quadrada (0 a 255)
synth.setPulseWidth(0, 128); // 128 = 50% = Quadrada perfeita

// Solta a tecla (inicia a fase de Release do envelope ADSR)
synth.noteOff(0);
```

### Deslizando Pitch e Volume (Portamento)
Os slides usam o algoritmo de linha de Bresenham para uma precisão absoluta em números inteiros, movendo-se graciosamente sem depender de matemática pesada com floats.

```cpp
synth.noteOn(0, c4, 255);
// Desliza do Dó 4 para o Dó 5 em exatamente 1000 milissegundos
synth.slideFreqTo(0, c5, 1000);

// Reduz o volume gradualmente para zero ao longo de 2 segundos
synth.slideVolTo(0, 0, 2000);
```

### Configurando o ADSR (Envelope)
Attack, Decay e Release são definidos em milissegundos. Sustain é uma amplitude.
```cpp
// Voz 0 | Attack: 10ms | Decay: 300ms | Sustain Nível: 127 | Release: 1500ms
synth.setEnv(0, 10, 300, 127, 1500);
```

### Streaming Direto de Arquivo WAV (Cartão SD)
O streaming de arquivos WAV pesados não vai fazer o seu sintetizador travar, mas requer que o seu módulo SD seja inicializado com alta velocidade (16MHz a 20MHz) nos pinos SPI físicos.
```cpp
#include <SD.h>
#include <SPI.h>

void setup() {
    SPI.begin(18, 19, 23, 5);
    SD.begin(5, SPI, 16000000); // 16MHz é VITAL para a estabilidade do áudio!

    // Reprodução: Voz 1, Caminho, Volume (255), Pitch (c4 mantém a original), Loop (true)
    synth.playStream(1, SD, "/loop_bateria.wav", 255, c4, true);

    // Pula para a marca de 5 segundos (5000 ms) do áudio
    synth.seekStreamMs(1, 5000);
}
```
## 5. DSP Avançado e Ondas Customizadas

Com a versão 2.4.0, o **ESP32Synth** abriu seu núcleo de processamento para você injetar código de alta performance.

### A. Efeitos Globais Master (Reverb, Delay)
Usando `setCustomDSP`, você intercepta o buffer de mixagem final de 32-bits ANTES dele ser enviado para o DAC. 
**Atenção:** Este código roda 48.000 vezes por segundo. Ele precisa ser brutalmente otimizado! Não use o operador de módulo (`%`) para dar a volta em buffers e fuja dos `floats`.

Aqui está um exemplo prático de um **Reverb de Fita Profissional com Saturação Analógica e DC Blocker**:

```cpp
#define TAPE_LEN 20000 
int32_t* reverbTape;
int writeHead = 0;

int32_t dcBlockerPrevWet = 0;
int32_t dcBlockerState = 0;
int32_t lpState = 0; 

// IRAM_ATTR força esta função para a memória RAM ultra-rápida!
void IRAM_ATTR reverbDSP(int32_t* mixBuffer, int numSamples) {
    if (!reverbTape) return; 

    for (int i = 0; i < numSamples; i++) {
        int32_t dry = mixBuffer[i];

        // 1. Leituras Seguras do Buffer Circular (Sem usar o operador lento '%')
        // Escolhemos tempos primos menores que TAPE_LEN (20000) para evitar ressonâncias fixas.
        int tap1 = writeHead - 4327;  if (tap1 < 0) tap1 += TAPE_LEN;
        int tap2 = writeHead - 11003; if (tap2 < 0) tap2 += TAPE_LEN;
        int tap3 = writeHead - 19013; if (tap3 < 0) tap3 += TAPE_LEN;

        // Soma as 3 cabeças e divide por 4 (>> 2) para não clipar o sinal
        int32_t wet = (reverbTape[tap1] >> 2) + (reverbTape[tap2] >> 2) + (reverbTape[tap3] >> 2);

        // 2. DC Blocker (Filtro Passa-Alta Crucial!)
        // Isso mata qualquer energia parada de baixa frequência que causaria um loop infinito de ruído.
        int32_t dcBlocked = wet - dcBlockerPrevWet + ((dcBlockerState * 253) >> 8);
        dcBlockerPrevWet = wet;
        dcBlockerState = dcBlocked;

        // 3. Filtro Low-Pass (Deixa as repetições cada vez mais abafadas e quentes)
        lpState = ((dcBlocked * 50) + (lpState * 206)) >> 8; 

        // 4. Calcula o Feedback para gravar na fita (~78% de feedback)
        int32_t feedback = (dry >> 1) + ((lpState * 200) >> 8);

        // 5. SATURAÇÃO ANALÓGICA (Soft-Clipping de segurança)
        // Se a matemática tentar explodir (microfonias), esmagamos o som no limite do 16-bit.
        if (feedback > 32767) feedback = 32767;
        else if (feedback < -32768) feedback = -32768;

        // Grava na fita e avança a cabeça de leitura/gravação
        reverbTape[writeHead] = feedback;
        writeHead++;
        if (writeHead >= TAPE_LEN) writeHead = 0;

        // Mixagem Master: Sinal limpo (Dry) + Cauda do Reverb (Wet)
        mixBuffer[i] = dry + lpState;
    }
}

void setup() {
    // Aloca a memória da "fita" na RAM (Heap) com segurança
    reverbTape = (int32_t*)heap_caps_calloc(TAPE_LEN, sizeof(int32_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    
    // INJETA O REVERB NA ENGINE!
    if (reverbTape) synth.setCustomDSP(reverbDSP);
}
```

### B. Escrevendo seu próprio Oscilador (Custom Waves)
Se as ondas padrão não forem suficientes, crie sua própria matemática sônica usando `setCustomWave()`. E graças à nossa arquitetura de *Jump Table*, **você pode voltar a uma onda normal a qualquer instante** usando `setWave(voz, WAVE_SINE)`. A engine lidará com a transição perfeitamente, sem falhas ou estalos.

Abaixo, um exemplo de uma **Onda Senoidal com Modulação de Fase por Feedback (FM)**. A onda modula sua própria fase com base no resultado anterior, criando a mesma ressonância metálica riquíssima encontrada em sintetizadores FM clássicos (como o Yamaha DX7).

```cpp
void IRAM_ATTR waveFMSine(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * vo->trmModGain) >> 8;
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc + vo->vibOffset;
    
    // Reaproveitamos a variável 'noiseSample' da struct para armazenar nosso último output
    int16_t prevOut = vo->noiseSample; 

    for (int i = 0; i < samples; i++) {
        // A MÁGICA: Modulação de fase via feedback.
        // Com o shift em 15, o feedback torce a onda em ~12%. 
        // Esse é o Ponto de Ressonância Musical perfeito antes de virar ruído!
        uint32_t modPh = ph + ((int32_t)prevOut << 15); 
        
        // Busca o valor na Look-up Table Senoidal de alta velocidade da engine
        int32_t s = sineLUT[(modPh >> SINE_SHIFT) & SINE_LUT_MASK] >> 16;
        prevOut = (int16_t)s;

        // Aplica Envelope e Volume, e soma na mixagem final
        int32_t finalVol = (int32_t)(((uint32_t)(currentEnv >> 12) * volBase) >> 16);
        mixBuffer[i] += (s * finalVol) >> 16;
        
        // Avança fase e envelope
        ph += inc;
        currentEnv += envStep;
    }
    
    // Salva o estado para o próximo bloco de renderização
    vo->phase = ph;
    vo->noiseSample = prevOut; 
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_16BIT);
    
    // Associa a Voz 0 para usar a sua função Senoidal FM
    synth.setCustomWave(0, waveFMSine);
    synth.noteOn(0, c4, 255);
    
    // Se quiser voltar para uma onda dente de serra normal depois, basta:
    // synth.setWave(0, WAVE_SAW);
}
```

### C. O Maestro: Algoritmos Gerativos (Custom Control)
Precisa de algo controlando o sintetizador, mas não quer bloquear seu `loop()` principal nem brigar com *timers*? O `setCustomControl` roda integrado de forma inteligente ao "Control Rate" da engine (padrão 100Hz). Excelente para sequenciadores e geração procedural de música.

```cpp
uint32_t ticks = 0;

void IRAM_ATTR theMaestroControl() {
    ticks++;
    
    // Um LFO automático controlando o PulseWidth (PWM) da Voz 1 no decorrer do tempo
    uint8_t breathPWM = 128 + (sin(ticks * 0.05) * 110);
    synth.setPulseWidth(1, breathPWM);

    // Arpejador Aleatório gerativo disparando a cada 12 ticks
    if (ticks % 12 == 0) {
        if (random(0, 3) == 0) { 
            synth.noteOn(0, c5, random(50, 150)); 
        }
    }
}

void setup() {
    synth.begin(2, SMODE_I2S, 4, 15, I2S_32BIT);
    // Associa o Maestro ao loop de controle
    synth.setCustomControl(theMaestroControl);
}
```

---

## 🛠 Tools / Ferramentas

Dentro da pasta `tools/` deste repositório, você encontra scripts Python projetados para acelerar seu fluxo de trabalho:
*   `WavetableMaker.py`: Cria *wavetables* perfeitas a partir de equações matemáticas ou áudios e as converte diretamente em arrays C/C++ (`.h`).
*   `WavToEsp32SynthConverter.py`: Prepara arquivos de áudio externos, aplicando algoritmos inteligentes de downsampling e compressão. Transforma tudo em `.h` para a funcionalidade `WAVE_SAMPLE`, permitindo tocar amostras curtas na velocidade brutal da RAM, poupando a lentidão do cartão SD.

---

## ⚠️ Common Troubleshooting (Solução de Problemas Comuns)

*   **Pops, cliques, engasgos ou áudio robótico:** Tem certeza de que você não colocou um `delay()` gigantesco no seu `loop()`, asfixiando os processos básicos do Core 0? Confirme também se a placa no Arduino IDE está devidamente configurada para rodar a CPU a **240MHz**.
*   **SD Stream engasgando ou "Falha de Leitura":** O barramento SPI do seu Arduino está lento demais. Inicialize o SD Card sempre forçando a velocidade: `SD.begin(5, SPI, 16000000)`. Para streams, o formato ideal do cartão SD é **FAT32** com *cluster size* de 32kb ou 64kb. *Nota: A biblioteca nativa do ESP não lê cartões SDXC (64GB+) no formato exFAT por padrão.*
*   **Ruído estático constante sem tocar notas:** Típico do DAC interno do ESP32 (que é notoriamente ruim) ou fiação I2S mal isolada. Se usar um módulo como o PCM5102, garanta que ele possua o **GND** firmemente conectado ao terra do ESP32.
*   **Módulo reiniciando do nada (Deadlock):** Ao desligar o synth usando `synth.end()`, aguarde cerca de 50ms (via `vTaskDelay`) antes de destruir ou reiniciar instâncias pesadas. O RTOS do ESP-IDF necessita de alguns ciclos livres para soltar os *mutexes* das tasks I2S com segurança. A biblioteca trata a maior parte disso, mas seja cauteloso ao desconectar pinos em tempo de execução.

---
<p align="center"><i>Construído com paixão, muito café e matemática pesadamente otimizada.</i></p>