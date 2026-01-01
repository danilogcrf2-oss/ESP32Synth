# ESP32Synth — Referência Completa (Documentação Detalhada)

**ESP32Synth** é uma biblioteca de síntese de alto desempenho para ESP32, oferecendo até **32 vozes** polifônicas, wavetables dinâmicas, ADSR tradicional e um sistema opcional de **Instruments** (arrays de volumes e wavetables por estágio).

---

## Índice

- Recursos principais
- Instalação e inicialização (PDM / I2S) ✅ (nota PDM: use -1 nos pinos extras)
- Conceitos-chave (vozes, envelopes, wavetables, instruments)
- API detalhada (assinaturas + descrição de cada função)
- Exemplos práticos (wavetables, instruments, ondas básicas)
- Detalhes de implementação (IRAM_ATTR, fixed-point, morph)
- Boas práticas e concorrência
- Calibração de afinação
- Contribuição & Licença

---

## 🔧 Recursos principais

- Polifonia: até 32 vozes independentes
- Saída: I2S / PDM (I2S padrão a 48 kHz, SYNTH_RATE ajustável)
- Wavetables: suporte 4 / 8 / 16 bits
- Engine híbrida: ADSR padrão + Instrument (A/D/S/R com arrays de vol/wave)
- Sem floats no caminho crítico de áudio; operações em fixed-point
- Funções sensíveis ao tempo colocadas em IRAM (`IRAM_ATTR`) para baixa latência

---

## 🚀 Instalação e inicialização

1. Coloque a pasta `ESP32Synth` em `Arduino/libraries/`.
2. Inclua a biblioteca:

```cpp
#include <ESP32Synth.h>
ESP32Synth synth;
```

3. Inicialização (PDM ou I2S):

- Exemplo PDM (note: **use -1 nos pinos não necessários**):

```cpp
// dataPin é o pino de saída PDM; clkPin/wsPin podem ser -1
synth.begin(5, SMODE_PDM, -1, -1);
```

- Exemplo I2S:

```cpp
synth.begin(5, SMODE_I2S, 18, 19); // dataPin, SMODE_I2S, clkPin=BCLK, wsPin=LRCLK
```

> ⚠️ Observação: Para PDM, a biblioteca aceita passar `-1` para `clkPin` e `wsPin` — isso indica que não há pinos adicionais (somente `dataPin` é necessário).

---

## ✨ Conceitos essenciais

- `Voice` — cada voz contém: fase, incrementador de fase (`phaseInc`), tipo de onda, ADSR, filtro, vibrato, e estado de *Instrument* (opcional).
- `Instrument` — struct que define arrays de **volumes** e **wavetable IDs** para Attack, Decay, Sustain e Release. Se `inst == nullptr`, a voz usa o ADSR legado.
- `Wavetable` — bloco de amostras carregado em RAM; é referenciado por um `id` global via `registerWavetable`.
- `Control-rate` — taxa com que `processControl()` é chamada (padrão 100 Hz). Usada para avançar `Instrument` arrays.

---

## 📘 API Detalhada

Abaixo cada função pública com assinatura e explicação breve de comportamento e parâmetros.

### Inicialização

- `bool begin(int dataPin = 5, SynthOutputMode mode = SMODE_PDM, int clkPin = -1, int wsPin = -1)`
  - Inicializa driver PDM ou I2S. Para PDM, passe `-1` em `clkPin`/`wsPin` quando não aplicável.
  - Retorna `true` se a criação do canal I2S/PDM e a tarefa de áudio for bem-sucedida.

### Vozes e notas

- `void noteOn(uint8_t voice, uint32_t freqCentiHz, uint8_t volume)`
  - Liga uma voz (0..31). `freqCentiHz` é em centi-Hz (ex.: A4 = 44000). `volume` 0..255.
  - Calcula `phaseInc` com aritmética 64-bit para alta precisão.
  - Se a voz tiver `inst != nullptr`, reinicializa o estado do Instrument (stageIdx, controlTick, morph).

- `void noteOff(uint8_t voice)`
  - Coloca a voz em `ENV_RELEASE` (ou encerra se em Instrument e lenR == 0).

- `void setFrequency(uint8_t voice, uint32_t freqCentiHz)`
  - Atualiza `phaseInc` sem reiniciar o estado da voz.

- `void setVolume(uint8_t voice, uint8_t volume)`
  - Define o ganho (0..255) multiplicado pela saída antes do envelope.

### Ondas e wavetables

- `void setWave(uint8_t voice, WaveType type)`
  - Define `WaveType` (SINE, TRIANGLE, SAW, PULSE, WAVETABLE, NOISE) para a voz. Se `WAVE_WAVETABLE`, `wtData` deve estar definido.

- `void setPulseWidth(uint8_t voice, uint8_t width)`
  - Define largura de pulso (PWM) para `WAVE_PULSE` (0..255).

- `void setWavetable(uint8_t voice, const void* data, uint32_t size, BitDepth depth)`
  - Define uma wavetable diretamente para uma voz (local). `depth` indica 4/8/16 bits.

- `void setWavetable(const void* data, uint32_t size, BitDepth depth)`
  - Aplica a mesma wavetable a todas as vozes (rápido atalho).

- `void registerWavetable(uint16_t id, const void* data, uint32_t size, BitDepth depth)`
  - Registra uma wavetable global referenciável por `Instrument` via `waveId` (0..MAX_WAVETABLES-1).
  - **Valide** que `id < MAX_WAVETABLES` e que os dados permanecem válidos em RAM durante a reprodução.

### Envelopes e Instrument

- `void setEnv(uint8_t voice, uint16_t a_ms, uint16_t d_ms, uint8_t s_level, uint16_t r_ms)`
  - Define ADSR clássico. `a_ms`, `d_ms`, `r_ms` em milissegundos; `s_level` 0..255.
  - Valores também são guardados como `attackMs`, `decayMs`, `releaseMs` para uso do modo `Instrument`.

- `void setInstrument(uint8_t voice, Instrument* inst)`
  - Associa (ou remove com `nullptr`) um `Instrument` à voz. Ao associar, o estado do Instrument é resetado (stageIdx=0, controlTick=0...).
  - Se `inst == nullptr`, a voz volta ao ADSR legado.

- `void setControlRateHz(uint16_t hz)`
  - Altera a taxa de controle (ex.: 100 Hz por padrão). Usada para calcular ticks por elemento do Instrument.

### Filtro e vibrato

- `void setFilter(uint8_t voice, FilterType type, uint8_t cutoff, uint8_t resonance)`
  - Define filtro do tipo `FilterType` (LP/HP/BP). `cutoff` e `resonance` são mapeados internamente para coeficientes fixos.

- `void setVibrato(uint8_t voice, uint32_t rateCentiHz, uint32_t depthCentiHz)`
  - Define LFO para pitch-mod (rate/depth em centi-Hz * constantes internas para converção fixa).

---

## 🧪 Exemplos práticos (mais completos)

### 1) PDM simples (apenas data pin)

```cpp
#include <ESP32Synth.h>
ESP32Synth synth;

void setup() {
  synth.begin(5, SMODE_PDM, -1, -1); // -1 indica pinos não usados
  synth.setWave(0, WAVE_SINE);
  synth.noteOn(0, 44000, 200);
}
```

### 2) Criar e usar uma wavetable a partir de uma onda básica

```cpp
// Gerar 256 samples seno (8-bit)
const uint32_t WT_SIZE = 256;
static uint8_t sineWT[WT_SIZE];
for (uint32_t i = 0; i < WT_SIZE; ++i) {
  sineWT[i] = (uint8_t)(128 + sin((2.0 * M_PI * i) / WT_SIZE) * 127.0);
}
// registrar globalmente
synth.registerWavetable(10, sineWT, WT_SIZE, BITS_8);
// usar em instrumento
const uint8_t volA[] = {255, 200};
const uint16_t waveA[] = {10, 10};
Instrument inst = { volA, nullptr, 220, nullptr, waveA, nullptr, 2, 0, 0, 0 };
synth.setInstrument(1, &inst);
synth.noteOn(1, 66000, 220);
```

### 3) Instrument usando ondas básicas diretamente (agora suportado)

- A partir desta versão, **cada elemento do Instrument pode referenciar uma onda básica** em vez de uma wavetable.
- Para isso, forneça arrays `waveTypeA`, `waveTypeD`, `waveTypeR` (e `waveTypeS` como um único valor) com valores:
  - `0` = usar `waveA/waveD/waveR` (wavetable id)
  - `1` = SINE
  - `2` = TRIANGLE
  - `3` = SAW
  - `4` = PULSE
  - `5` = NOISE

Exemplo: instrumento que intercala entre SINE e TRIANGLE no ataque

```cpp
const uint8_t volA[] = {255, 200};
// waveA can be unused when using waveTypeA
const uint8_t waveTypeA[] = {1, 2}; // 1=sine, 2=triangle
// Instrument struct order: volA, volD, volS, volR,
//                         waveA, waveD, waveS, waveR,
//                         waveTypeA, waveTypeD, waveTypeS, waveTypeR,
//                         lenA, lenD, lenR
Instrument inst = { volA, nullptr, 220, nullptr, NULL, NULL, 0, 0, waveTypeA, NULL, 0, NULL, 2, 0, 0 };
synth.setInstrument(0, &inst);
```

- Na renderização, o `morph` agora interpola entre as duas formas (se ambas forem básicas) usando uma interpolação linear inteira muito leve, com impacto mínimo na CPU.

- Para sair do Instrument e voltar ao modo de onda básico (por exemplo, trocar para TRIANGLE imediatamente), há uma função de conveniência:

```cpp
synth.detachWave(0, WAVE_TRIANGLE); // função curta
```

Isto remove o `Instrument` da voz 0, inicia o ADSR (attack) e define o `WaveType` para `WAVE_TRIANGLE`.

- CPU & 32 vozes: o morph entre ondas básicas (sine/tri/saw/pulse/noise) é suportado e implementado com interpolação inteira por amostra, o que tem custo de CPU muito baixo. Em geral um ESP32 moderno (especialmente ESP32-S3) consegue lidar com **32 vozes** com esta implementação, desde que **nem todas** as vozes usem filtros pesados, wavetables enormes ou efeitos de vibrato intenso ao mesmo tempo. Se notar saturação, reduza `controlRateHz`, desative recursos não essenciais, ou diminua a taxa de amostragem para aliviar CPU.


---

## 🔬 Como `Instrument` avança e calcula morph

- A cada tick de control-rate, `processControl()` é chamado. Para cada voz com `inst != nullptr`:
  - Computa `ticks_total = stageMs * controlRateHz / 1000` e `ticksPer = max(1, ticks_total / len)`.
  - Define `currWaveId`, `nextWaveId`, `vol` e calcula `morph` com base em `elapsed / ticksPer` (0..255).
- No `render()`, o sample final entre `curr` e `next` é interpolado por:

```
combined = ((int32_t)sampleA * (256 - morph) + (int32_t)sampleB * morph) >> 8;
```

---

## ⚠️ Boas práticas e concorrência

- Proteger chamadas que atualizam wavetables em runtime (`registerWavetable`) se feitas de tarefas externas: `portENTER_CRITICAL()` / `portEXIT_CRITICAL()`.
- Garanta que buffers de wavetables registrados permaneçam válidos na RAM durante a reprodução.
- IDs de wavetable devem estar em `0..(MAX_WAVETABLES-1)`.

---

## 🔧 Depuração e calibração de afinação

- Teste com A4 = 440 Hz; meça com afinador e, se necessário, ajuste `SYNTH_RATE` conforme:

```
Nova_Taxa = Taxa_Atual * (Freq_Medida / Freq_Esperada)
```

- Atualize `#define SYNTH_RATE` em `ESP32Synth.h` se precisar compensar variação do cristal.

---

## 📝 Licença

Consulte `LICENSE` no repositório.

---
## ⚠️⚠️⚠️⚠️ AVISO !

O modo SMODE_I2S `NÃO` foi testado, irei testar e aprimorar esse modo em futuras atualizações. Por enquanto recomendo que use o SMODE_PDM.

---
