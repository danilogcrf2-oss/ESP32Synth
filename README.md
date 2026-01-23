# ESP32Synth v2.2.0 — Guia Completo

![Version](https://img.shields.io/badge/version-2.0.5-green.svg) ![Platform](https://img.shields.io/badge/platform-ESP32-orange.svg) ![License](https://img.shields.io/badge/license-MIT-blue.svg)

**[Português]** Biblioteca de síntese de áudio profissional e de alto desempenho para ESP32. Suporta até 80 vozes polifônicas, Wavetables, Sampler com Loops, ADSR, e Efeitos em tempo real.

**[English]** Professional, high-performance audio synthesis library for the ESP32. Supports up to 80 polyphonic voices, Wavetables, a Sampler with Loops, ADSR, and real-time Effects.

---

# 🇧🇷 Documentação Completa (Português)

## Índice
1.  **Visão Geral**
    *   Filosofia da Biblioteca
    *   Recursos Principais
2.  **Instalação**
3.  **Arquitetura e Conceitos Fundamentais**
    *   O Pipeline de Áudio (Task e DMA)
    *   Vozes e Polifonia
    *   Aritmética de Ponto Fixo (Por que não `float`?)
    *   A Taxa de Controle (`controlRate`)
4.  **Guia Detalhado da API**
    *   Inicialização e Saída de Áudio
    *   Controle de Notas (O Ciclo de Vida de uma Voz)
    *   Módulo de Osciladores
    *   Módulo de Envelopes (ADSR)
    *   Módulo de Sampler
    *   Módulo de Instrumentos (Estilo Tracker)
    *   Módulo de Efeitos e Modulação
5.  **Gerenciamento de Vozes para Polifonia**
6.  **Performance e Otimização**
7.  **Exemplos Práticos**
    *   Exemplo 1: Sintetizador Polifônico com Alocador de Voz
    *   Exemplo 2: Toccando um Sample de Bateria
    *   Exemplo 3: Criando um Instrumento Customizado
8.  **Solução de Problemas**

---

## 1. Visão Geral

O **ESP32Synth** transforma o ESP32 em um sintetizador polifônico de nível profissional. Diferente de bibliotecas simples de "tone", esta engine processa áudio em tempo real com mixagem de até **80 vozes**, envelopes ADSR completos, e reprodução de samples PCM com alta precisão.

### Filosofia da Biblioteca
O ESP32Synth foi projetado com três objetivos principais:
1.  **Performance Extrema:** Todo o caminho crítico de áudio é executado a partir da IRAM usando aritmética de ponto fixo, minimizando a latência e o jitter para permitir uma polifonia massiva sem sobrecarregar a CPU.
2.  **Controle de Baixo Nível:** A biblioteca fornece controle direto sobre cada voz, permitindo que o desenvolvedor implemente qualquer lógica de gerenciamento de notas, desde um simples sintetizador monofônico até um complexo MPE (MIDI Polyphonic Expression).
3.  **Flexibilidade:** Com suporte a múltiplos tipos de osciladores, samples, wavetables e instrumentos, a biblioteca serve como uma base poderosa para criar uma vasta gama de sons.

### Recursos Principais
*   **Polifonia Massiva:** Até 80 vozes simultâneas com mixagem dinâmica.
*   **Saída de Áudio:** Suporte nativo a I2S (DAC externo, 8/16/32-bit), PDM (Delta-Sigma), e DAC interno do ESP32.
*   **Osciladores Versáteis:** Senoidal, Triangular, Dente de Serra, Quadrada (PWM variável), Ruído e Wavetables customizáveis.
*   **Sampler Engine Avançado:** Reprodução de samples PCM com suporte a Loops (Forward, PingPong, Reverse) e Zonas de Mapeamento (Multisample).
*   **Efeitos em Tempo Real:** Vibrato, Tremolo, e Pitch Slide (Glissando/Portamento).
*   **Arpeggiator Integrado:** Sequenciador de notas por voz com suporte a acordes e padrões.
*   **Alta Performance:** Otimizado para IRAM, utilizando aritmética de ponto fixo para evitar latência de FPU.
*   **Motor de Instrumentos:** Suporte para instrumentos complexos no estilo "Tracker", que automatizam a modulação de volume e a troca de formas de onda.

---

## 2. Instalação
1.  Baixe o repositório ou a release mais recente.
2.  Na IDE do Arduino, vá em `Sketch` > `Include Library` > `Add .ZIP Library...` e selecione o arquivo que você baixou.
3.  Ou, descompacte o ZIP e mova a pasta `ESP32Synth` para o seu diretório de bibliotecas do Arduino (`Documentos/Arduino/libraries/`).
4.  Reinicie a IDE do Arduino.

**Dependências:**
*   **Hardware:** Qualquer placa ESP32, ESP32-S2, ESP32-S3 ou ESP32-C3.
*   **Core:** `esp32` by Espressif Systems (v2.0.14+ ou v3.0.0+ recomendado).

---

## 3. Arquitetura e Conceitos Fundamentais

### O Pipeline de Áudio (Task e DMA)
O ESP32Synth opera de forma **não-bloqueante**. Ao chamar `synth.begin()`, uma tarefa de alta prioridade (`audioTask`) é criada e fixada no Core 1 do ESP32 (se disponível).

1.  **`audioTask`:** Este é o coração da engine. Ele executa um loop infinito que renderiza pequenos blocos de áudio.
2.  **Buffer de Mixagem:** Dentro do loop, todas as 80 vozes ativas são processadas e somadas (mixadas) em um buffer temporário.
3.  **Transferência DMA:** Esse buffer de áudio é então enviado para o periférico I2S do ESP32. O DMA (Direct Memory Access) cuida da transferência para os pinos de saída sem precisar da intervenção da CPU.

Isso garante que, uma vez iniciado, o áudio é gerado continuamente em background, e seu `loop()` principal fica livre para cuidar de lógica de controle, como ler botões, MIDI ou atualizar um display.

### Vozes e Polifonia
A biblioteca gerencia um array de 80 `Voice` structs. Uma "voz" é uma cadeia de sinal completa: um oscilador, um envelope, LFOs, etc.

**Importante:** O ESP32Synth **não** possui um alocador de vozes automático. Você tem controle total sobre qual voz usar. Isso é uma decisão de design para dar máxima flexibilidade. Você é responsável por decidir qual voz tocará qual nota. Veja a seção **Gerenciamento de Vozes para Polifonia** para um exemplo de como implementar isso.

### Aritmética de Ponto Fixo (Por que não `float`?)
Operações de ponto flutuante (`float`, `double`) no ESP32 podem ser lentas e introduzir latência, especialmente quando a FPU (Floating-Point Unit) está ocupada. Para garantir a performance em tempo real, o ESP32Synth usa **aritmética de ponto fixo**.

*   **Frequência:** Em vez de `440.0`, usamos `44000` (CentiHz). Isso permite representar frações de Hertz com precisão de dois decimais usando apenas inteiros.
*   **Fase do Oscilador:** A fase de cada oscilador é um inteiro de 32 bits (`uint32_t`) que representa um ciclo de 0 a 360 graus. A cada amostra de áudio, um "incremento de fase" é adicionado. Esse incremento é calculado a partir da frequência da nota, garantindo uma afinação perfeita.
*   **Envelopes:** Os níveis de envelope também são inteiros de 32 bits, permitindo uma resolução muito alta para fades suaves sem "degraus" audíveis.

### A Taxa de Controle (`controlRate`)
Enquanto o áudio é renderizado na frequência de amostragem (ex: 48000Hz), os parâmetros de controle como envelopes e LFOs não precisam ser atualizados com tanta frequência. A `controlRate` (padrão 100Hz) define quantas vezes por segundo esses parâmetros são recalculados.

*   **Vantagem:** Reduz drasticamente a carga na CPU. Em vez de calcular 48000 vezes por segundo o novo nível do envelope, calculamos apenas 100 vezes. A transição entre esses pontos é interpolada linearmente.
*   **Customização:** Você pode mudar essa taxa com `synth.setControlRateHz()`. Uma taxa maior (ex: 200Hz) resulta em LFOs e envelopes mais suaves, ao custo de mais CPU. Uma taxa menor economiza CPU, mas pode tornar modulações rápidas menos precisas.

---

## 4. Guia Detalhado da API

### Inicialização e Saída de Áudio
A função `begin()` prepara o hardware. Use o `overload` que melhor se adapta ao seu projeto.

*   `synth.begin(DAC_PIN);`
    *   **Uso:** DAC interno do ESP32 (pino 25 ou 26).
    *   **Qualidade:** A mais baixa, boa para bipes e sons simples sem hardware externo.
    *   **Conexão:** Conecte um fone de ouvido ou amplificador diretamente ao pino DAC e ao GND.

*   `synth.begin(BCK_PIN, WS_PIN, DATA_PIN);`
    *   **Uso:** DACs I2S externos (ex: PCM5102A, MAX98357A). Padrão de 16-bit.
    *   **Qualidade:** Excelente. O padrão para projetos de áudio de alta fidelidade.
    *   **Conexão:** Conecte os pinos BCK, WS (LRCK) e DATA (DIN) do DAC aos pinos correspondentes do ESP32.

*   `synth.begin(BCK_PIN, WS_PIN, DATA_PIN, I2S_32BIT);`
    *   **Uso:** DACs I2S de alta resolução que suportam áudio de 32-bit.
    *   **Qualidade:** Profissional. O áudio é enviado nos 16 bits mais significativos de uma palavra de 32 bits.

*   `synth.begin(DATA_PIN, SMODE_PDM, -1, -1);`
    *   **Uso:** Saída PDM (Pulse-Density Modulation) em um único pino digital.
    *   **Qualidade:** Razoável, mas requer um filtro RC (resistor e capacitor) no pino de saída para converter o sinal digital em analógico.

### Controle de Notas (O Ciclo de Vida de uma Voz)
O controle de uma voz é baseado no conceito de "gate" (portão), similar a sintetizadores analógicos.

1.  `noteOn(voice, freq, vol)`: Abre o "gate". Isso dispara o envelope, que entra na fase de **Attack**. O som começa a subir de volume. Após o ataque, ele passa para o **Decay** até atingir o nível de **Sustain**.
2.  A nota permanece no nível de Sustain enquanto o gate estiver aberto. Você pode alterar a frequência com `setFrequency()` ou o volume com `setVolume()` nesse período.
3.  `noteOff(voice)`: Fecha o "gate". Isso dispara a fase de **Release** do envelope. O som não para abruptamente, mas decai suavemente até o silêncio, conforme o tempo de release definido. A voz só se torna totalmente inativa (`vo.active = false`) após o término da fase de release.

### Módulo de Osciladores

#### `void setWave(uint8_t voice, WaveType type)`
Define a forma de onda do oscilador.
*   `WAVE_SINE`: Onda senoidal pura, som suave. Gerada por uma tabela (LUT) para performance.
*   `WAVE_TRIANGLE`: Onda triangular. Som similar ao seno mas com mais harmônicos.
*   `WAVE_SAW`: Dente de serra. Som brilhante e rico, clássico de sintetizadores.
*   `WAVE_PULSE`: Onda quadrada. Perfeita para sons de chiptune e baixos. Use `setPulseWidth()` para mudar o timbre.
*   `WAVE_NOISE`: Ruído branco. Essencial para percussão e efeitos.
*   `WAVE_WAVETABLE`: Usa uma tabela de onda customizada (veja abaixo).
*   `WAVE_SAMPLE`: Usa o motor de sampler (veja abaixo).

#### `void setPulseWidth(uint8_t voice, uint8_t width)`
Controla a largura de pulso da `WAVE_PULSE`.
*   `width`: Um valor de 0 a 255. `128` (padrão) gera uma onda quadrada perfeita (50% de duty cycle). Valores diferentes criam ondas retangulares, alterando drasticamente o timbre.

#### Wavetables (`WAVE_WAVETABLE`)
Wavetables permitem criar timbres complexos. Uma wavetable é simplesmente um array que armazena um único ciclo de uma forma de onda.

1.  **Crie sua Wavetable:**
Pode ser um array de `int16_t` ou `uint8_t`. O tamanho pode ser qualquer um, mas potências de 2 são eficientes.
    ```cpp
    // Uma wavetable simples com 4 passos para um som "quadrado-suave"
    const int16_t myWave[] = {32767, 32767, -32768, -32768};
    ```

2.  **Registre na Biblioteca:**
Dê um ID único (0-999) para sua wavetable.
    ```cpp
    // Registra a wavetable com ID 0
    synth.registerWavetable(0, myWave, 4, BITS_16); 
    ```

3.  **Use em uma Voz:**
    ```cpp
    // Atribui a wavetable à voz 5
    synth.setWave(5, WAVE_WAVETABLE);
    // Diz qual wavetable usar (pelo ID)
    synth.setWavetable(5, myWave, 4, BITS_16);

    synth.noteOn(5, NOTE_C4, 255);
    ```

### Módulo de Envelopes (ADSR)

#### `void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r)`
Modela a amplitude (volume) de uma nota ao longo do tempo.
*   `a` (Attack): Tempo em `ms` para o som ir do silêncio ao volume máximo. (Ex: `50`)
*   `d` (Decay): Tempo em `ms` para o som decair do volume máximo até o nível de sustain. (Ex: `100`)
*   `s` (Sustain): Nível de volume (0-255) que a nota mantém após o decay. (Ex: `200`)
*   `r` (Release): Tempo em `ms` para o som decair do nível de sustain até o silêncio após `noteOff()` ser chamada. (Ex: `500`)

**Exemplo de uso:**
*   **Som de Piano:** Ataque rápido, decay longo, sustain baixo, release médio. `setEnv(v, 5, 800, 50, 300);`
*   **Som de Pad/String:** Ataque lento, release lento. `setEnv(v, 400, 200, 220, 600);`
*   **Som de Percussão:** Sem sustain. `setEnv(v, 5, 200, 0, 50);`

### Módulo de Sampler
Permite tocar amostras de áudio pré-gravadas.

1.  **Converta e Inclua o Sample:**
    Use o script Python `tools/Samples/WavToEsp32SynthConverter.py` para converter um arquivo `.wav` em um header `.h`.
    ```bash
    python WavToEsp32SynthConverter.py meu_kick.wav KickSample
    ```
    Isso gerará `KickSample.h`, que você deve incluir no seu projeto (`#include "KickSample.h"`).

2.  **Registre o Sample:**
    No `setup()`, registre os dados do sample na biblioteca.
    ```cpp
    // ID 1, dados do header, tamanho, sample rate original, frequência raiz
    synth.registerSample(1, KickSample_data, KickSample_len, KickSample_rate, NOTE_C4);
    ```

3.  **Crie um Instrumento de Sample:**
    O sampler funciona através de `Instrument_Sample`. Você precisa definir pelo menos uma zona (`SampleZone`).
    ```cpp
    // Esta zona mapeia todas as notas para o nosso sample de Kick (ID 1)
    const SampleZone kickZone[] = {
      {0, 12700, 1, 0} // De freq 0 a 12700 (MIDI 0-127), use o sample ID 1
    };

    // Cria o instrumento
    Instrument_Sample kickInstrument = {
      .zones = kickZone,
      .numZones = 1,
      .loopMode = LOOP_OFF // Bateria não precisa de loop
    };
    ```

4.  **Atribua e Toque:**
    ```cpp
    // Atribui o instrumento à voz 0
    synth.setInstrument(0, &kickInstrument);
    
    // Toca a nota. A frequência aqui (NOTE_C4) será usada para o pitch.
    // Se a nota for diferente da frequência raiz do sample, o pitch será ajustado.
    synth.noteOn(0, NOTE_C4, 255);
    ```

### Módulo de Instrumentos (Estilo Tracker)
Este é um recurso avançado que permite criar sons que mudam de timbre e volume ao longo do tempo, como nos trackers de música antigos (FastTracker, ProTracker).

A struct `Instrument` define "passos" de uma sequência para o ataque e para o release.
```cpp
struct Instrument {
    const uint8_t* seqVolumes;    // Array com os volumes de cada passo do ataque
    const int16_t* seqWaves;      // Array com as formas de onda de cada passo
    uint8_t        seqLen;        // Número de passos na sequência de ataque
    uint16_t       seqSpeedMs;    // Duração de cada passo em ms

    uint8_t        susVol;        // Volume durante a fase de sustain
    int16_t        susWave;       // Forma de onda durante o sustain

    // ... campos similares para a sequência de Release ...
};
```
**Como usar:**
1.  **Defina os Arrays da Sequência:**
    ```cpp
    // Sequência de ataque: começa com ruído, depois muda para pulso
    const int16_t attack_waves[] = {W_NOISE, W_PULSE, W_PULSE};
    const uint8_t attack_vols[] = {255, 200, 150};

    // Sequência de release: um fade out simples com onda senoidal
    const int16_t release_waves[] = {W_SINE};
    const uint8_t release_vols[] = {0};
    ```

2.  **Defina o Instrumento:**
    ```cpp
    Instrument myLead = {
      .seqVolumes = attack_vols,
      .seqWaves = attack_waves,
      .seqLen = 3,
      .seqSpeedMs = 30, // Cada passo dura 30ms

      .susVol = 180,
      .susWave = W_SAW, // No sustain, vira uma onda dente de serra

      .relVolumes = release_vols,
      .relWaves = release_waves,
      .relLen = 1,
      .relSpeedMs = 500 // Release dura 500ms
    };
    ```

3.  **Atribua e Toque:**
    ```cpp
    synth.setInstrument(2, &myLead);
    synth.noteOn(2, NOTE_A4, 255); // O volume do noteOn é ignorado
    ```
    Quando a nota tocar, ela passará pelos 3 passos da sequência de ataque, depois ficará em `W_SAW` no sustain, e ao chamar `noteOff()`, ela passará pela sequência de release.

### Módulo de Efeitos e Modulação

*   `setVibrato(voice, rate, depth)`: Adiciona um LFO de seno à frequência.
    *   `rate`: Velocidade do vibrato em CentiHz. `1000` (10Hz) é um bom valor.
    *   `depth`: Intensidade do vibrato em CentiHz. `5000` (50Hz) causa uma variação de +/- 50Hz.

*   `setTremolo(voice, rate, depth)`: Adiciona um LFO de seno ao volume.
    *   `rate`: Velocidade em CentiHz.
    *   `depth`: Intensidade (0-255).

*   `slideTo(voice, endFreq, durationMs)`: Inicia um portamento da frequência atual da voz até `endFreq`, com duração de `durationMs`.

*   `setArpeggio(voice, durationMs, ...)`: Transforma uma única nota `noteOn` em um arpejo.
    ```cpp
    // Cria um arpejo de acorde de Dó maior na voz 1
    // A cada 100ms, a nota muda: C4 -> E4 -> G4 -> C5 -> repete
    synth.setArpeggio(1, 100, NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5);
    synth.setWave(1, WAVE_PULSE);
    synth.noteOn(1, NOTE_C4, 255); // A frequência do noteOn serve como base, mas é sobrescrita pelo arpejador
    ```

---

## 5. Gerenciamento de Vozes para Polifonia
Para tocar múltiplas notas, você precisa de uma função que encontre uma voz livre.

Aqui está um exemplo de um alocador simples que encontra a primeira voz inativa ou, se todas estiverem ocupadas, a que está com o volume mais baixo (provavelmente em fase de release).
```cpp
int findFreeVoice() {
  uint32_t lowestVol = 0xFFFFFFFF;
  int quietestVoice = 0;

  // 1. Procura por uma voz completamente inativa
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!synth.isVoiceActive(i)) {
      return i;
    }
    // Enquanto isso, encontra a voz mais silenciosa
    uint32_t vol = synth.getOutputRaw(i);
    if (vol < lowestVol) {
      lowestVol = vol;
      quietestVoice = i;
    }
  }

  // 2. Se não achou, "rouba" a voz mais silenciosa
  return quietestVoice;
}

void playNote(uint32_t freq, uint8_t vol) {
  int voice = findFreeVoice();
  synth.noteOn(voice, freq, vol);
}
```

---

## 6. Performance e Otimização

*   **Uso de CPU:** A carga da CPU é diretamente proporcional ao número de vozes ativas e à `controlRate`. O ESP32Synth é muito eficiente, mas em projetos complexos, monitore o uso da CPU. Se a Task de áudio não tiver tempo suficiente, você ouvirá falhas (glitches).
*   **Memória (RAM):** O maior consumidor de RAM são os samples e wavetables. Armazene-os como `const` para que fiquem na memória Flash (`PROGMEM`) em vez de serem copiados para a RAM na inicialização.
*   **IRAM:** O código crítico da biblioteca já está marcado com `IRAM_ATTR`. Isso garante que ele seja executado da RAM interna de alta velocidade, evitando atrasos de cache da Flash.

---

## 7. Exemplos Práticos

<details><summary>Exemplo 1: Sintetizador Polifônico com Alocador de Voz</summary>

```cpp
#include <ESP32Synth.h>

ESP32Synth synth;

// Array para guardar o estado de 4 "teclas" 
bool key_is_pressed[4] = {false};
int key_voices[4] = {-1, -1, -1, -1};
const uint32_t freqs[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5};

// Alocador simples
int findFreeVoice() {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!synth.isVoiceActive(i)) return i;
  }
  return 0; // Rouba a voz 0 se nenhuma estiver livre
}

void setup() {
  Serial.begin(115200);
  synth.begin(22, 25, 26); // DAC I2S Externo (BCK, WS, DATA)

  for(int i=0; i<MAX_VOICES; i++){
    synth.setWave(i, WAVE_SAW);
    synth.setEnv(i, 10, 200, 180, 400);
  }
}

void loop() {
  // Simula apertar e soltar 4 teclas em sequência
  for (int i = 0; i < 4; i++) {
    if (!key_is_pressed[i]) {
      Serial.printf("Key %d ON\n", i);
      key_voices[i] = findFreeVoice();
      synth.noteOn(key_voices[i], freqs[i], 255);
      key_is_pressed[i] = true;
    }
    delay(500);
  }

  delay(1000);

  for (int i = 0; i < 4; i++) {
    if (key_is_pressed[i]) {
      Serial.printf("Key %d OFF\n", i);
      synth.noteOff(key_voices[i]);
      key_is_pressed[i] = false;
    }
    delay(500);
  }
  delay(2000);
}
```
</details>

<details><summary>Exemplo 2: Toccando um Sample de Bateria</summary>

```cpp
// 1. Crie o arquivo "KickSample.h" com a ferramenta de conversão.
// python WavToEsp32SynthConverter.py kick.wav KickSample

#include <ESP32Synth.h>
#include "KickSample.h" // Arquivo gerado

ESP32Synth synth;

// Definição do instrumento de sample
const SampleZone kickZone[] = {{0, 12700, 0, 0}}; // Mapeia todas as notas para o sample ID 0
Instrument_Sample kickInstrument = {
  .zones = kickZone,
  .numZones = 1,
  .loopMode = LOOP_OFF
};

void setup() {
  synth.begin(25); // DAC Interno

  // Registra o sample com ID 0
  synth.registerSample(0, KickSample_data, KickSample_len, KickSample_rate, NOTE_C4);

  // Atribui o instrumento à voz 0
  synth.setInstrument(0, &kickInstrument);
}

void loop() {
  synth.noteOn(0, NOTE_C4, 255);
  delay(500);
}
```
</details>

---

## 8. Solução de Problemas
*   **Ruído/Estalos:**
    *   **I2S:** Verifique se os cabos são curtos e se o GND está bem conectado.
    *   **PDM:** Certifique-se de que o pino de saída tem um filtro RC adequado (ex: Resistor 1k + Capacitor 10nF a 100nF) para suavizar o sinal.
    *   **Alimentação:** Use uma fonte de alimentação limpa de 3.3V. Ruído na alimentação pode vazar para o áudio.
*   **Watchdog Reset:** Isso geralmente acontece se o `loop()` ou algum callback de interrupção bloquear por muito tempo. O `audioTask` do ESP32Synth tem alta prioridade, mas se a CPU 0 estiver travada, pode causar resets. Evite `delay()` longos e use `vTaskDelay()` em projetos FreeRTOS.
*   **Volume Baixo:** A saída do ESP32 (DAC, I2S, PDM) é em nível de linha. Ela não tem potência para acionar alto-falantes diretamente. Use um amplificador (ex: PAM8403, TDA2030) entre a saída do synth e o alto-falante.

---
<br>
# 🇺🇸 Complete Documentation (English)
## Table of Contents
1.  **Overview**
    *   Library Philosophy
    *   Key Features
2.  **Installation**
3.  **Architecture and Core Concepts**
    *   The Audio Pipeline (Task and DMA)
    *   Voices and Polyphony
    *   Fixed-Point Arithmetic (Why not `float`?)
    *   The Control Rate (`controlRate`)
4.  **Detailed API Guide**
    *   Initialization and Audio Output
    *   Note Control (The Lifecycle of a Voice)
    *   Oscillator Module
    *   Envelope Module (ADSR)
    *   Sampler Module
    *   Instrument Module (Tracker-Style)
    *   Effects and Modulation Module
5.  **Voice Management for Polyphony**
6.  **Performance and Optimization**
7.  **Practical Examples**
    *   Example 1: Polyphonic Synthesizer with Voice Allocator
    *   Example 2: Playing a Drum Sample
    *   Example 3: Creating a Custom Instrument
8.  **Troubleshooting**

---

# 🇺🇸 Complete Documentation (English)

## 1. Overview

**ESP32Synth** turns your ESP32 into a professional-grade polyphonic synthesizer. Unlike simple "tone" libraries, this engine processes audio in real-time with mixing for up to **80 voices**, full ADSR envelopes, and high-precision PCM sample playback.

### Library Philosophy
ESP32Synth was designed with three main goals:
1.  **Extreme Performance:** The entire critical audio path runs from IRAM using fixed-point arithmetic, minimizing latency and jitter to allow for massive polyphony without overloading the CPU.
2.  **Low-Level Control:** The library provides direct control over each voice, allowing the developer to implement any note management logic, from a simple monophonic synth to a complex MPE (MIDI Polyphonic Expression) instrument.
3.  **Flexibility:** With support for multiple oscillator types, samples, wavetables, and instruments, the library serves as a powerful foundation for creating a vast range of sounds.

### Key Features
*   **Massive Polyphony:** Up to 80 simultaneous voices with dynamic mixing.
*   **Audio Output:** Native support for I2S (external DAC, 8/16/32-bit), PDM (Delta-Sigma), and the ESP32's internal DAC.
*   **Versatile Oscillators:** Sine, Triangle, Sawtooth, Pulse (variable PWM), Noise, and custom Wavetables.
*   **Advanced Sampler Engine:** PCM sample playback with support for Loops (Forward, PingPong, Reverse) and Key-mapping Zones (Multisample).
*   **Real-Time Effects:** Vibrato, Tremolo, and Pitch Slide (Glissando/Portamento).
*   **Integrated Arpeggiator:** A per-voice note sequencer with support for chords and patterns.
*   **High Performance:** Optimized for IRAM, using fixed-point arithmetic to avoid FPU latency.
*   **Instrument Engine:** Support for complex "Tracker-style" instruments that automate volume modulation and waveform switching.

---

## 2. Installation
1.  Download the repository or the latest release.
2.  In the Arduino IDE, go to `Sketch` > `Include Library` > `Add .ZIP Library...` and select the file you downloaded.
3.  Alternatively, unzip the file and move the `ESP32Synth` folder to your Arduino libraries directory (`Documents/Arduino/libraries/`).
4.  Restart the Arduino IDE.

**Dependencies:**
*   **Hardware:** Any ESP32, ESP32-S2, ESP32-S3, or ESP32-C3 board.
*   **Core:** `esp32` by Espressif Systems (v2.0.14+ or v3.0.0+ recommended).

---

## 3. Architecture and Core Concepts

### The Audio Pipeline (Task and DMA)
ESP32Synth operates in a **non-blocking** manner. When you call `synth.begin()`, a high-priority task (`audioTask`) is created and pinned to Core 1 of the ESP32 (if available).

1.  **`audioTask`:** This is the heart of the engine. It runs an infinite loop that renders small blocks of audio.
2.  **Mixing Buffer:** Inside the loop, all 80 active voices are processed and summed (mixed) into a temporary buffer.
3.  **DMA Transfer:** This audio buffer is then sent to the ESP32's I2S peripheral. The DMA (Direct Memory Access) handles the transfer to the output pins without requiring CPU intervention.

This ensures that once started, audio is generated continuously in the background, leaving your main `loop()` free to handle control logic, such as reading buttons, MIDI, or updating a display.

### Voices and Polyphony
The library manages an array of 80 `Voice` structs. A "voice" is a complete signal chain: an oscillator, an envelope, LFOs, etc.

**Important:** ESP32Synth **does not** have an automatic voice allocator. You have full control over which voice to use. This is a design decision to provide maximum flexibility. You are responsible for deciding which voice plays which note. See the **Voice Management for Polyphony** section for an example of how to implement this.

### Fixed-Point Arithmetic (Why not `float`?)
Floating-point operations (`float`, `double`) on the ESP32 can be slow and introduce latency, especially when the FPU (Floating-Point Unit) is busy. To ensure real-time performance, ESP32Synth uses **fixed-point arithmetic**.

*   **Frequency:** Instead of `440.0`, we use `44000` (CentiHz). This allows representing fractions of a Hertz with two decimal places of precision using only integers.
*   **Oscillator Phase:** The phase of each oscillator is a 32-bit unsigned integer (`uint32_t`) that represents a cycle from 0 to 360 degrees. With each audio sample, a "phase increment" is added. This increment is calculated from the note's frequency, ensuring perfect tuning.
*   **Envelopes:** Envelope levels are also 32-bit integers, allowing for very high resolution for smooth fades without audible "steps".

### The Control Rate (`controlRate`)
While audio is rendered at the sample rate (e.g., 48000Hz), control parameters like envelopes and LFOs do not need to be updated as frequently. The `controlRate` (default 100Hz) defines how many times per second these parameters are recalculated.

*   **Advantage:** Drastically reduces the CPU load. Instead of calculating the new envelope level 48,000 times per second, we only calculate it 100 times. The transition between these points is linearly interpolated.
*   **Customization:** You can change this rate with `synth.setControlRateHz()`. A higher rate (e.g., 200Hz) results in smoother LFOs and envelopes at the cost of more CPU. A lower rate saves CPU but can make fast modulations less precise.

---

## 4. Detailed API Guide

### Initialization and Audio Output
The `begin()` function prepares the hardware. Use the overload that best suits your project.

*   `synth.begin(DAC_PIN);`
    *   **Usage:** ESP32's internal DAC (pin 25 or 26).
    *   **Quality:** The lowest, good for beeps and simple sounds without external hardware.
    *   **Connection:** Connect a headphone or amplifier directly to the DAC pin and GND.

*   `synth.begin(BCK_PIN, WS_PIN, DATA_PIN);`
    *   **Usage:** External I2S DACs (e.g., PCM5102A, MAX98357A). Defaults to 16-bit.
    *   **Quality:** Excellent. The standard for high-fidelity audio projects.
    *   **Connection:** Connect the BCK, WS (LRCK), and DATA (DIN) pins of the DAC to the corresponding pins on the ESP32.

*   `synth.begin(BCK_PIN, WS_PIN, DATA_PIN, I2S_32BIT);`
    *   **Usage:** High-resolution I2S DACs that support 32-bit audio.
    *   **Quality:** Professional. The audio is sent in the 16 most significant bits of a 32-bit word.

*   `synth.begin(DATA_PIN, SMODE_PDM, -1, -1);`
    *   **Usage:** PDM (Pulse-Density Modulation) output on a single digital pin.
    *   **Quality:** Reasonable, but requires an RC (resistor and capacitor) filter on the output pin to smooth the signal.

### Note Control (The Lifecycle of a Voice)
Voice control is based on the "gate" concept, similar to analog synthesizers.

1.  `noteOn(voice, freq, vol)`: Opens the "gate". This triggers the envelope, which enters the **Attack** phase. The sound begins to rise in volume. After the attack, it moves to the **Decay** phase until it reaches the **Sustain** level.
2.  The note remains at the Sustain level as long as the gate is open. You can change the frequency with `setFrequency()` or the volume with `setVolume()` during this time.
3.  `noteOff(voice)`: Closes the "gate". This triggers the **Release** phase of the envelope. The sound does not stop abruptly but fades out smoothly according to the defined release time. The voice only becomes fully inactive (`vo.active = false`) after the release phase is complete.

### Oscillator Module

#### `void setWave(uint8_t voice, WaveType type)`
Sets the oscillator's waveform.
*   `WAVE_SINE`: Pure sine wave, smooth sound. Generated from a Look-Up Table (LUT) for performance.
*   `WAVE_TRIANGLE`: Triangle wave, similar to sine but with more harmonics.
*   `WAVE_SAW`: Sawtooth wave, bright and rich, a classic synth sound.
*   `WAVE_PULSE`: Pulse wave. Perfect for chiptune sounds and basses. Use `setPulseWidth()` to change its timbre.
*   `WAVE_NOISE`: White noise. Essential for percussion and effects.
*   `WAVE_WAVETABLE`: Uses a custom wavetable (see below).
*   `WAVE_SAMPLE`: Uses the sampler engine (see below).

#### `void setPulseWidth(uint8_t voice, uint8_t width)`
Controls the pulse width of the `WAVE_PULSE`.
*   `width`: A value from 0 to 255. `128` (default) generates a perfect square wave (50% duty cycle). Different values create rectangular waves, drastically altering the timbre.

#### Wavetables (`WAVE_WAVETABLE`)
Wavetables allow you to create complex timbres. A wavetable is simply an array that stores a single cycle of a waveform.

1.  **Create Your Wavetable:**
It can be an array of `int16_t` or `uint8_t`. The size can be anything, but powers of 2 are efficient.
    ```cpp
    // A simple 4-step wavetable for a "soft-square" sound
    const int16_t myWave[] = {32767, 32767, -32768, -32768};
    ```

2.  **Register it with the Library:**
Give your wavetable a unique ID (0-999).
    ```cpp
    // Register the wavetable with ID 0
    synth.registerWavetable(0, myWave, 4, BITS_16); 
    ```

3.  **Use it in a Voice:**
    ```cpp
    // Assign the wavetable to voice 5
    synth.setWave(5, WAVE_WAVETABLE);
    // Tell it which wavetable to use (by ID)
    synth.setWavetable(5, myWave, 4, BITS_16);

    synth.noteOn(5, NOTE_C4, 255);
    ```

### Envelope Module (ADSR)

#### `void setEnv(uint8_t voice, uint16_t a, uint16_t d, uint8_t s, uint16_t r)`
Models the amplitude (volume) of a note over time.
*   `a` (Attack): Time in `ms` for the sound to go from silence to full volume. (e.g., `50`)
*   `d` (Decay): Time in `ms` for the sound to decay from full volume to the sustain level. (e.g., `100`)
*   `s` (Sustain): Volume level (0-255) that the note holds after the decay. (e.g., `200`)
*   `r` (Release): Time in `ms` for the sound to fade from the sustain level to silence after `noteOff()` is called. (e.g., `500`)

**Usage Examples:**
*   **Piano Sound:** Fast attack, long decay, low sustain, medium release. `setEnv(v, 5, 800, 50, 300);`
*   **Pad/String Sound:** Slow attack, slow release. `setEnv(v, 400, 200, 220, 600);`
*   **Percussive Sound:** No sustain. `setEnv(v, 5, 200, 0, 50);`

### Sampler Module
Allows you to play pre-recorded audio samples.

1.  **Convert and Include the Sample:**
Use the Python script `tools/Samples/WavToEsp32SynthConverter.py` to convert a `.wav` file into a `.h` header.
    ```bash
    python WavToEsp32SynthConverter.py kick.wav KickSample
    ```
    This will generate `KickSample.h`, which you should include in your project (`#include "KickSample.h"`).

2.  **Register the Sample:**
In `setup()`, register the sample data with the library.
    ```cpp
    // ID 1, data from header, length, original sample rate, root frequency
    synth.registerSample(1, KickSample_data, KickSample_len, KickSample_rate, NOTE_C4);
    ```

3.  **Create a Sample Instrument:**
The sampler works through `Instrument_Sample`. You need to define at least one zone (`SampleZone`).
    ```cpp
    // This zone maps all notes to our Kick sample (ID 1)
    const SampleZone kickZone[] = {{0, 12700, 1, 0}}; // Maps all notes to sample ID 0
    Instrument_Sample kickInstrument = {
      .zones = kickZone,
      .numZones = 1,
      .loopMode = LOOP_OFF
    };
    ```

4.  **Assign and Play:**
    ```cpp
    // Assign the instrument to voice 0
    synth.setInstrument(0, &kickInstrument);
    
    // Play the note. The frequency here (NOTE_C4) will be used for pitching.
    // If the note is different from the sample's root frequency, the pitch will be adjusted.
    synth.noteOn(0, NOTE_C4, 255);
    ```

### Instrument Module (Tracker-Style)
This is an advanced feature that lets you create sounds that change timbre and volume over time, like in old-school music trackers (FastTracker, ProTracker).

The `Instrument` struct defines "steps" of a sequence for the attack and release phases.
```cpp
struct Instrument {
    const uint8_t* seqVolumes;    // Array of volumes for each attack step
    const int16_t* seqWaves;      // Array of waveforms for each step
    uint8_t        seqLen;        // Number of steps in the attack sequence
    uint16_t       seqSpeedMs;    // Duration of each step in ms

    uint8_t        susVol;        // Volume during the sustain phase
    int16_t        susWave;       // Waveform during sustain

    // ... similar fields for the Release sequence ...
};
```
**How to use:**
1.  **Define the Sequence Arrays:**
    ```cpp
    // Attack sequence: starts with noise, then changes to a pulse wave
    const int16_t attack_waves[] = {W_NOISE, W_PULSE, W_PULSE};
    const uint8_t attack_vols[] = {255, 200, 150};

    // Release sequence: a simple fade-out with a sine wave
    const int16_t release_waves[] = {W_SINE};
    const uint8_t release_vols[] = {0};
    ```

2.  **Define the Instrument:**
    ```cpp
    Instrument myLead = {
      .seqVolumes = attack_vols,
      .seqWaves = attack_waves,
      .seqLen = 3,
      .seqSpeedMs = 30, // Each step lasts 30ms

      .susVol = 180,
      .susWave = W_SAW, // Becomes a sawtooth wave during sustain

      .relVolumes = release_vols,
      .relWaves = release_waves,
      .relLen = 1,
      .relSpeedMs = 500 // Release lasts 500ms
    };
    ```

3.  **Assign and Play:**
    ```cpp
    synth.setInstrument(2, &myLead);
    synth.noteOn(2, NOTE_A4, 255); // The volume from noteOn is ignored
    ```
    When the note plays, it will go through the 3 steps of the attack sequence, then hold at `W_SAW` during sustain, and when `noteOff()` is called, it will go through the release sequence.

### Effects and Modulation Module

*   `setVibrato(voice, rate, depth)`: Adds a sine LFO to the frequency.
    *   `rate`: Speed of the vibrato in CentiHz. `1000` (10Hz) is a good value.
    *   `depth`: Intensity of the vibrato in CentiHz. `5000` (50Hz) causes a pitch variation of +/- 50Hz.

*   `setTremolo(voice, rate, depth)`: Adds a sine LFO to the volume.
    *   `rate`: Speed in CentiHz.
    *   `depth`: Intensity (0-255).

*   `slideTo(voice, endFreq, durationMs)`: Starts a portamento from the voice's current frequency to `endFreq`, with a duration of `durationMs`.

*   `setArpeggio(voice, durationMs, ...)`: Turns a single `noteOn` into an arpeggio.
    ```cpp
    // Creates a C-major chord arpeggio on voice 1
    // Every 100ms, the note changes: C4 -> E4 -> G4 -> C5 -> repeats
    synth.setArpeggio(1, 100, NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5);
    synth.setWave(1, WAVE_PULSE);
    synth.noteOn(1, NOTE_C4, 255); // The noteOn frequency is used as a base but is overridden by the arpeggiator
    ```

---

## 5. Voice Management for Polyphony
To play multiple notes, you need a function to find a free voice.

Here is an example of a simple allocator that finds the first inactive voice, or if all are busy, the one with the lowest volume (likely in its release phase).
```cpp
int findFreeVoice() {
  uint32_t lowestVol = 0xFFFFFFFF;
  int quietestVoice = 0;

  // 1. Look for a completely inactive voice
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!synth.isVoiceActive(i)) return i;
    // Meanwhile, find the quietest voice
    uint32_t vol = synth.getOutputRaw(i);
    if (vol < lowestVol) {
      lowestVol = vol;
      quietestVoice = i;
    }
  }

  // 2. If none found, "steal" the quietest voice
  return quietestVoice;
}

void playNote(uint32_t freq, uint8_t vol) {
  int voice = findFreeVoice();
  synth.noteOn(voice, freq, vol);
}
```

---

## 6. Performance and Optimization

*   **CPU Usage:** The CPU load is directly proportional to the number of active voices and the `controlRate`. ESP32Synth is very efficient, but in complex projects, monitor CPU usage. If the audio Task doesn't get enough time, you will hear glitches.
*   **Memory (RAM):** The biggest consumers of RAM are samples and wavetables. Store them as `const` so they stay in Flash memory (`PROGMEM`) instead of being copied to RAM on startup.
*   **IRAM:** The library's critical code is already marked with `IRAM_ATTR`. This ensures it runs from the high-speed internal RAM, avoiding delays from Flash cache misses.

---

## 7. Practical Examples

<details><summary>Example 1: Polyphonic Synthesizer with Voice Allocator</summary>

```cpp
#include <ESP32Synth.h>

ESP32Synth synth;

// Array to hold the state of 4 "keys" 
bool key_is_pressed[4] = {false};
int key_voices[4] = {-1, -1, -1, -1};
const uint32_t freqs[] = {NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5};

// Simple allocator
int findFreeVoice() {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!synth.isVoiceActive(i)) return i;
  }
  return 0; // Steal voice 0 if none are free
}

void setup() {
  Serial.begin(115200);
  synth.begin(22, 25, 26); // External I2S DAC (BCK, WS, DATA)

  for(int i=0; i<MAX_VOICES; i++){
    synth.setWave(i, WAVE_SAW);
    synth.setEnv(i, 10, 200, 180, 400);
  }
}

void loop() {
  // Simulate pressing and releasing 4 keys in sequence
  for (int i = 0; i < 4; i++) {
    if (!key_is_pressed[i]) {
      Serial.printf("Key %d ON\n", i);
      key_voices[i] = findFreeVoice();
      synth.noteOn(key_voices[i], freqs[i], 255);
      key_is_pressed[i] = true;
    }
    delay(500);
  }

  delay(1000);

  for (int i = 0; i < 4; i++) {
    if (key_is_pressed[i]) {
      Serial.printf("Key %d OFF\n", i);
      synth.noteOff(key_voices[i]);
      key_is_pressed[i] = false;
    }
    delay(500);
  }
  delay(2000);
}
```
</details>

<details><summary>Example 2: Playing a Drum Sample</summary>

```cpp
// 1. Create the "KickSample.h" file using the conversion tool.
// python WavToEsp32SynthConverter.py kick.wav KickSample

#include <ESP32Synth.h>
#include "KickSample.h" // Generated file

ESP32Synth synth;

// Definition of the sample instrument
const SampleZone kickZone[] = {{0, 12700, 0, 0}}; // Maps all notes to sample ID 0
Instrument_Sample kickInstrument = {
  .zones = kickZone,
  .numZones = 1,
  .loopMode = LOOP_OFF
};

void setup() {
  synth.begin(25); // Internal DAC

  // Register the sample with ID 0
  synth.registerSample(0, KickSample_data, KickSample_len, KickSample_rate, NOTE_C4);

  // Assign the instrument to voice 0
  synth.setInstrument(0, &kickInstrument);
}

void loop() {
  synth.noteOn(0, NOTE_C4, 255);
  delay(500);
}
```
</details>

---

## 8. Troubleshooting
*   **Noise/Popping:**
    *   **I2S:** Ensure your wires are short and that GND is well-connected.
    *   **PDM:** Make sure the output pin has a proper RC filter (e.g., 1k Resistor + 10nF to 100nF Capacitor) to smooth the signal.
    *   **Power:** Use a clean 3.3V power supply. Noise on the power rail can leak into the audio.
*   **Watchdog Reset:** This usually happens if your `loop()` or an interrupt callback blocks for too long. ESP32Synth's `audioTask` has a high priority, but if CPU 0 is stuck, it can cause resets. Avoid long `delay()` calls and use `vTaskDelay()` in FreeRTOS projects.
*   **Low Volume:** The ESP32's output (DAC, I2S, PDM) is at line level. It does not have the power to drive speakers directly. Use an amplifier (e.g., PAM8403, TDA2030) between the synth's output and the speaker.

---
