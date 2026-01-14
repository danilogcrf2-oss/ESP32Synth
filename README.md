# ESP32Synth v2.0.5 — Referência Completa / Full Reference

![Version](https://img.shields.io/badge/version-2.0.5-green.svg) ![Platform](https://img.shields.io/badge/platform-ESP32-orange.svg) ![License](https://img.shields.io/badge/license-MIT-blue.svg)

**[Português]** Biblioteca de síntese de áudio profissional e de alto desempenho para ESP32. Suporta 32 vozes polifônicas, Wavetables, Sampler com Loops, ADSR, Filtros Ressonantes e Efeitos em tempo real.

**[English]** Professional high-performance audio synthesis library for ESP32. Supports 32 polyphonic voices, Wavetables, Sampler with Loops, ADSR, Resonant Filters, and real-time Effects.

---

# 🇧🇷 Documentação Completa (Português)

## Índice
1. Visão Geral
2. Instalação
3. Conceitos Fundamentais
4. Referência da API
5. Estruturas de Dados
6. Exemplos
7. Detalhes de Implementação
8. Solução de Problemas

## Visão Geral
O **ESP32Synth** transforma o ESP32 em um sintetizador polifônico de nível profissional. Diferente de bibliotecas simples de "tone", esta engine processa áudio em tempo real com mixagem de até **32 vozes**, envelopes ADSR completos, filtros ressonantes e reprodução de samples PCM com alta precisão (64-bit fixed-point).

Projetada para baixa latência e uso eficiente de memória (IRAM), é ideal para instrumentos musicais digitais, jogos, instalações artísticas e experimentação sonora.

### Recursos Principais
*   **Polifonia Massiva:** Até 32 vozes simultâneas com mixagem dinâmica e gerenciamento de prioridade.
*   **Saída de Áudio:** Suporte nativo a I2S (DAC externo) e PDM (Delta-Sigma via pino digital).
*   **Osciladores Versáteis:** Senoidal, Triangular, Dente de Serra, Quadrada (PWM variável), Ruído (Híbrido) e Wavetables customizáveis.
*   **Sampler Engine Avançado:** Reprodução de samples PCM com suporte a Loops (Forward, PingPong, Reverse) e Zonas de Mapeamento (Multisample).
*   **Efeitos em Tempo Real:** Filtro de Estado Variável (LP/HP/BP) com Ressonância, Vibrato, Tremolo, Pitch Slide (Glissando/Portamento).
*   **Arpeggiator Integrado:** Sequenciador de notas por voz com suporte a acordes e padrões.
*   **Alta Performance:** Otimizado para IRAM, utilizando aritmética de ponto fixo para evitar latência de FPU.

---

## Instalação
### Via Arduino IDE
1. Baixe o repositório ou a release mais recente.
2. Mova a pasta `ESP32Synth` para o diretório de bibliotecas do Arduino:
   *   Windows: `Documentos/Arduino/libraries/`
   *   Mac/Linux: `~/Documents/Arduino/libraries/`
3. Reinicie a IDE do Arduino.

### Dependências
*   **Hardware:** ESP32 ou ESP32-S3.
*   **Core:** esp32 by Espressif Systems (Recomendado v2.0.14+ ou v3.0.0+).

---

## Conceitos Fundamentais

### Vozes (Voices)
O sintetizador gerencia um pool de vozes virtuais (0 a 31). Cada voz é um canal de áudio independente com seu próprio oscilador, envelope, filtro e LFOs.

### Frequência em CentiHz
Para garantir precisão de afinação sem o custo computacional de `float`, todas as frequências na API são expressas em **CentiHz** (Hz * 100).
*   440.00 Hz = `44000`
*   C4 (Dó central) ≈ `26163`
*   *Dica: Use a função auxiliar `midiToFreq(note)` se disponível ou multiplique Hz por 100.*

### Instrumentos
*   **Instrumento Padrão (`Instrument`):** Define sequências de wavetables e volumes para síntese (estilo Tracker).
*   **Instrumento de Sample (`Instrument_Sample`):** Define zonas de teclado (keyzones) que mapeiam notas para diferentes amostras de áudio (ex: multisample de piano) e configurações de loop complexas.

### Control Rate
O motor de áudio roda na frequência de amostragem (padrão 48kHz), mas os envelopes e LFOs são atualizados a uma taxa de controle (padrão 100Hz ou 200Hz) para economizar CPU sem sacrificar a qualidade perceptível.

---

## Referência da API

### Inicialização

#### `bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin)`
Inicializa o periférico I2S do ESP32 e aloca os buffers DMA.

| Parâmetro | Descrição |
| :--- | :--- |
| `dataPin` | Pino de saída de dados (I2S DOUT). |
| `mode` | `SMODE_I2S` (DAC externo I2S padrão) ou `SMODE_PDM` (Pino direto/filtro RC). |
| `clkPin` | Pino Bit Clock (BCLK). Use `-1` se usar modo PDM. |
| `wsPin` | Pino Word Select (LRCK). Use `-1` se usar modo PDM. |

### Controle de Notas

#### `void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume)`
Dispara uma nota (Gate On). Reinicia o envelope para a fase de Attack.
*   **voice:** Índice da voz (0-31).
*   **freqCentiHz:** Frequência da nota (ex: 44000 para 440Hz).
*   **volume:** Amplitude (0-255).

#### `void noteOff(uint8_t voice)`
Inicia a fase de **Release** do envelope ADSR (Gate Off). A nota não para imediatamente, ela decai suavemente.

#### `void setFrequency(uint8_t voice, uint32_t freqCentiHz)`
Altera a frequência de uma voz já ativa instantaneamente. Útil para vibrato manual ou pitch bend.

### Configuração de Som

#### `void setWave(uint8_t voice, WaveType type)`
Define o tipo de oscilador base.
*   `WAVE_SINE`, `WAVE_TRIANGLE`, `WAVE_SAW`
*   `WAVE_PULSE` (Onda quadrada/retangular)
*   `WAVE_NOISE` (Gerador de ruído)
*   `WAVE_WAVETABLE` (Requer registro prévio via `registerWavetable`)
*   `WAVE_SAMPLE` (Usa engine de sampler via `registerSample`)

#### `void setWavetable(uint8_t voice, uint16_t tableId)`
Seleciona a tabela de onda a ser usada (necessário se `WAVE_WAVETABLE` estiver ativo).
*   **tableId:** ID da tabela registrada anteriormente.

#### `void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r)`
Configura o envelope ADSR (Attack, Decay, Sustain, Release).
*   **a, d, r:** Tempo em milissegundos.
*   **s:** Nível de sustentação (0-255).

### Sampler e Wavetables

#### `void registerSample(uint16_t id, const void* data, uint32_t len, uint32_t rate, uint32_t rootFreq)`
Registra um sample PCM na memória global.
*   **id:** Identificador único (0-999).
*   **data:** Ponteiro para o array de bytes (signed 8-bit ou 16-bit dependendo da config).
*   **len:** Tamanho do array.
*   **rate:** Taxa de amostragem original (ex: 44100).
*   **rootFreq:** Frequência original da gravação em CentiHz.

#### `void setInstrument(uint8_t voice, void* instrument)`
Associa um objeto de instrumento complexo à voz. Pode ser um `Instrument` (síntese) ou `Instrument_Sample` (sampler).

### Efeitos

#### `void setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t res)`
Aplica filtro de estado variável (SVF) por voz.
*   **type:** `FILTER_LP` (Passa-Baixa), `FILTER_HP` (Passa-Alta), `FILTER_BP` (Passa-Banda), `FILTER_OFF`.
*   **cutoff:** Frequência de corte (0-255, escala logarítmica interna).
*   **res:** Ressonância (0-255). Cuidado com valores altos (>240) que podem causar auto-oscilação.

#### `void slide(uint8_t voice, uint32_t startFreq, uint32_t endFreq, uint32_t durationMs)`
Executa um glissando (slide) automático e linear entre duas frequências.

#### `void setArpeggio(uint8_t voice, uint16_t durationMs, ...)`
Define uma sequência de notas para tocar em loop automaticamente na voz especificada.
*   **durationMs:** Duração de cada passo.
*   **...:** Lista de frequências (CentiHz).

### Monitoramento

#### `uint8_t getOutput8Bit(uint8_t voice)`
Retorna o nível de saída atual da voz (0-255), útil para criar visualizadores (VU Meter) ou reagir ao áudio.

---

## Estruturas de Dados

### `SampleZone`
Define uma zona de mapeamento para um sample.
```cpp
    struct SampleZone {
        uint32_t lowFreq;   // Frequência mínima para este sample
        uint32_t highFreq;  // Frequência máxima para este sample
        uint16_t sampleId;  // ID do sample registrado
        uint32_t rootFreq;  // Frequência raiz (original) do sample
    };
```
### `Instrument_Sample`
Define um instrumento baseado em samples.
```cpp
    struct Instrument_Sample {
        SampleZone* zones;  // Array de zonas
        uint8_t numZones;   // Número de zonas
        LoopMode loopMode;  // LOOP_OFF, LOOP_FORWARD, LOOP_PINGPONG, LOOP_REVERSE
        uint32_t loopStart; // Ponto de início do loop (amostra)
        uint32_t loopEnd;   // Ponto de fim do loop (0 = fim do arquivo)
    };
```
---

## Exemplos

### Uso Básico (PDM)
```cpp
    #include <ESP32Synth.h>

    ESP32Synth synth;

    void setup() {
        // Inicializa PDM no pino 25
        synth.begin(25, SMODE_PDM, -1, -1);
        
        // Toca C4 (261.63 Hz) na voz 0
        synth.setWave(0, WAVE_SAW);
        synth.setEnv(0, 50, 100, 200, 500);
        synth.noteOn(0, 26163, 255);
    }

    void loop() {
        // O áudio é gerado via interrupção
        delay(1000);
        synth.noteOff(0);
        delay(1000);
        synth.noteOn(0, 26163, 255);
    }
```
---

## Detalhes de Implementação

### Engine de Sample 64-bit
Para suportar samples longos e taxas de reprodução muito lentas sem overflow de inteiros, o ponteiro de posição do sample utiliza `uint64_t` em formato fixed-point 16.16.

### Ruído Híbrido
*   **freq < ~20kHz:** Ruído afinado (sample & hold por período).
*   **freq >= 20kHz:** Ruído branco (nova amostra a cada chamada).

## Solução de Problemas
*   **Ruído/Estalos:** Verifique se o pino PDM tem um filtro RC adequado (ex: Resistor 1k + Capacitor 100nF).
*   **Watchdog Reset:** Certifique-se de não realizar operações bloqueantes longas dentro de callbacks de interrupção (se houver). A biblioteca usa DMA, logo o uso de CPU é baixo.
*   **Volume Baixo:** No modo PDM, a tensão de saída é 3.3V p-p, mas a potência depende da impedância. Use um amplificador (ex: PAM8403) para alto-falantes.

---

<br>

# 🇺🇸 Full Documentation (English)

## Table of Contents
1. Overview
2. Installation
3. Core Concepts
4. API Reference
5. Data Structures
6. Examples
7. Implementation Details
8. Troubleshooting

## Overview
**ESP32Synth** turns your ESP32 into a powerful polyphonic synthesizer. Unlike simple "tone" libraries, this engine processes audio in real-time with mixing for up to 32 voices, ADSR envelopes, resonant filters, and high-precision PCM sample playback.

### Key Features
*   **Polyphony:** 32 simultaneous voices with dynamic mixing.
*   **Audio Output:** Native support for I2S (External DAC) and PDM (Delta-Sigma via digital pin).
*   **Oscillators:** Sine, Triangle, Saw, Pulse (PWM), Noise, and custom Wavetables.
*   **Sampler Engine (v1.5.0):** Sample playback with Loop support (Forward, PingPong, Reverse) and Mapping Zones (Multisample). 64-bit precision for perfect tuning.
*   **Effects:** State Variable Filter (LP/HP/BP), Vibrato, Tremolo, Pitch Slide (Glissando).
*   **Arpeggiator:** Integrated note sequencer per voice.
*   **High Performance:** Optimized for IRAM, no floats in the critical audio path.

---

## Installation
1. Download the repository or latest release.
2. Move the `ESP32Synth` folder to your Arduino libraries directory:
   *   Windows: `Documents/Arduino/libraries/`
   *   Mac/Linux: `~/Documents/Arduino/libraries/`
3. Restart the Arduino IDE.

---

## Core Concepts

### Voices
The synthesizer manages a pool of voices (0 to 31). You can control each voice individually or implement a dynamic voice allocator on top of the API.

### Frequency in CentiHz
To ensure precision without using `float` (which is slow), all frequencies are expressed in **CentiHz** (Hz * 100).
*   440.00 Hz = `44000`
*   C4 (Middle C) ≈ `26163`

### Instruments
*   **Standard Instrument (`Instrument`):** Defines sequences of wavetables and volumes for synthesis (Tracker style).
*   **Sample Instrument (`Instrument_Sample`):** Defines keyzones that map notes to different audio samples (e.g., low piano vs high piano) and loop settings.

### Control Rate
The audio engine runs at the sampling rate (e.g., 48kHz), but envelopes and LFOs are updated at a control rate (default 100Hz) to save CPU.

---

## API Reference

### Initialization

#### `bool begin(int dataPin, SynthOutputMode mode, int clkPin, int wsPin)`
Initializes the ESP32 I2S peripheral.

| Parameter | Description |
| :--- | :--- |
| `dataPin` | Data output pin (I2S DOUT). |
| `mode` | `SMODE_I2S` (External DAC) or `SMODE_PDM` (Direct pin/RC filter). |
| `clkPin` | Bit Clock pin (BCLK). Use `-1` if using PDM mode. |
| `wsPin` | Word Select pin (LRCK). Use `-1` if using PDM mode. |

### Note Control

#### `void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume)`
Triggers a note on a specific voice.
*   **voice:** Voice index (0-31).
*   **freqCentiHz:** Note frequency (e.g., 44000 for 440Hz).
*   **volume:** Amplitude (0-255).

#### `void noteOff(uint8_t voice)`
Starts the **Release** phase of the ADSR envelope. The note does not stop immediately; it decays as configured.

#### `void setFrequency(uint8_t voice, uint32_t freqCentiHz)`
Changes the frequency of an active voice. Useful for portamento or manual effects.

### Sound Configuration

#### `void setWave(uint8_t voice, WaveType type)`
Sets the oscillator type.
*   `WAVE_SINE`, `WAVE_TRIANGLE`, `WAVE_SAW`, `WAVE_PULSE`, `WAVE_NOISE`
*   `WAVE_WAVETABLE` (Requires prior registration)
*   `WAVE_SAMPLE` (Uses sampler engine)

#### `void setWavetable(uint8_t voice, uint16_t tableId)`
Selects the wavetable to use (required if `WAVE_WAVETABLE` is active).
*   **tableId:** ID of the previously registered table.

#### `void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r)`
Configures the ADSR envelope (Attack, Decay, Sustain, Release).
*   **a, d, r:** Time in milliseconds (approx).
*   **s:** Sustain level (0-255).

### Sampler and Wavetables

#### `void registerSample(uint16_t id, const void* data, uint32_t len, uint32_t rate, uint32_t rootFreq)`
Registers a PCM sample in memory.
*   **id:** Unique identifier (0-999).
*   **data:** Pointer to byte array (signed 8-bit or 16-bit depending on config).
*   **rootFreq:** Original recording frequency in CentiHz.

#### `void setInstrument(uint8_t voice, void* instrument)`
Associates a complex instrument object with the voice. Can be an `Instrument` (synthesis) or `Instrument_Sample` (sampler).

### Effects

#### `void setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t res)`
Applies a resonant filter.
*   **type:** `FILTER_LP` (Low Pass), `FILTER_HP` (High Pass), `FILTER_BP` (Band Pass), `FILTER_OFF`.
*   **cutoff:** Cutoff frequency (0-255 logarithmically mapped).
*   **res:** Resonance (0-255).

#### `void slide(uint8_t voice, uint32_t startFreq, uint32_t endFreq, uint32_t durationMs)`
Executes an automatic glissando between two frequencies.

#### `void setArpeggio(uint8_t voice, uint16_t durationMs, ...)`
Defines a sequence of notes to play in a loop.

### Monitoring

#### `uint8_t getOutput8Bit(uint8_t voice)`
Returns the current output level of the voice (0-255), useful for visualizers (VU Meter).

---

## Data Structures

### `SampleZone`
Defines a mapping zone for a sample.
```cpp
    struct SampleZone {
        uint32_t lowFreq;   // Lowest frequency for this sample
        uint32_t highFreq;  // Highest frequency for this sample
        uint16_t sampleId;  // ID of the registered sample
        uint32_t rootFreq;  // Root frequency of the sample
    };
```
### `Instrument_Sample`
Defines a sample-based instrument.
```cpp
    struct Instrument_Sample {
        SampleZone* zones;  // Array of zones
        uint8_t numZones;   // Number of zones
        LoopMode loopMode;  // LOOP_OFF, LOOP_FORWARD, LOOP_PINGPONG, LOOP_REVERSE
        uint32_t loopStart; // Start sample index
        uint32_t loopEnd;   // End sample index (0 = end of file)
    };
```
---

## Examples

### Basic Usage (PDM)
```cpp
    #include <ESP32Synth.h>

    ESP32Synth synth;

    void setup() {
        // Initialize PDM on pin 25
        synth.begin(25, SMODE_PDM, -1, -1);
        
        // Play C4 (261.63 Hz) on voice 0
        synth.setWave(0, WAVE_SAW);
        synth.setEnv(0, 50, 100, 200, 500);
        synth.noteOn(0, 26163, 255);
    }

    void loop() {
        // Audio is handled in background interrupts
        delay(1000);
        synth.noteOff(0);
        delay(1000);
        synth.noteOn(0, 26163, 255);
    }
```
---

## Implementation Details

### 64-bit Sample Engine
To support long samples and very slow playback speeds without integer overflow, the sample position pointer uses `uint64_t` in 16.16 fixed-point format.

### Hybrid Noise
*   **freq < ~20kHz:** Tuned noise (sample & hold per period).
*   **freq >= 20kHz:** White noise (new sample every call).

## Troubleshooting
*   **Noise/Popping:** Check if the PDM pin has a proper RC filter (e.g., 1k Resistor + 100nF Capacitor).
*   **Watchdog Reset:** Ensure you don't perform long blocking operations inside interrupt callbacks (if any). The library uses DMA, so CPU usage is low.
*   **Low Volume:** In PDM mode, output voltage is 3.3V p-p, but power depends on impedance. Use an amplifier (e.g., PAM8403) for speakers.

---

## License

See `LICENSE` in the repository.

