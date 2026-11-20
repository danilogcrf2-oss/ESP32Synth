/*
   ESP32Synth - CYD Expressive Ribbon Synth
   Autor: Danilo Gabriel
   
   - Onda Morph Tri-Saw 64-bit (Imune a Overflow/DC Bias)
   - Filtro SVF Chamberlin com Trava Anti-Lockup e Gain Compensation
   - Touch Anti-Ghosting e UI Blindada sem rastros visuais
*/

#pragma GCC optimize ("O3,unroll-loops")

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <ESP32Synth.h>

// --- PINOS CYD ---
#define XPT_IRQ 36
#define XPT_MOSI 32
#define XPT_MISO 39
#define XPT_CLK 25
#define XPT_CS 33
#define BACKLIGHT_PIN 21

// --- PINOS ÁUDIO I2S ---
#define I2S_BCK 22
#define I2S_WS 27
#define I2S_DATA 3

TFT_eSPI tft = TFT_eSPI(); 
SPIClass touchSpi = SPIClass(VSPI); 
XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);
ESP32Synth synth;

// =========================================================================
//    VARIÁVEIS DE CONTROLE COMPARTILHADAS (UI -> DSP)
// =========================================================================
volatile uint32_t dsp_peak_phase = 2147483648;
volatile uint32_t dsp_span_phase = 2147483647;

volatile int32_t filter_f = 0;
volatile int32_t filter_q = 0;
volatile int32_t filter_gain = 32768; 

static int32_t f_low = 0;
static int32_t f_high = 0;
static int32_t f_band = 0;

uint8_t current_morph = 127; 
uint8_t current_res = 180;   
int base_octave = 3;         
bool note_is_on = false;

int last_tx = -1;
int last_ty = -1;
unsigned long last_touch_time = 0; 

// =========================================================================
//    DSP EXTREMAMENTE OTIMIZADO E SEGURO (Core 1)
// =========================================================================
void IRAM_ATTR myCustomWave(Voice* vo, int32_t* mixBuffer, int samples, int32_t startEnv, int32_t envStep) {
    uint32_t ph = vo->phase;
    uint32_t inc = vo->phaseInc;
    int32_t currentEnv = startEnv;
    int32_t volBase = ((uint32_t)vo->vol * 255) >> 8; 

    // Cache local super rápido
    uint32_t peak = dsp_peak_phase;
    uint32_t span = dsp_span_phase;
    int32_t f = filter_f;
    int32_t q = filter_q;
    int32_t gain = filter_gain;

    for (int i = 0; i < samples; i++) {
        int32_t raw_sample;

        // 1. Morph Tri -> Saw (64-bit Imune a Overflow!)
        // Calcula a rampa linear perfeitamente baseada na posição do pico
        if (ph < peak) {
            uint32_t frac = (uint32_t)(((uint64_t)ph * 65535ULL) / peak);
            raw_sample = (int32_t)frac - 32768; // Vai de -32768 a +32767
        } else {
            uint32_t rem = ph - peak;
            uint32_t frac = (uint32_t)(((uint64_t)rem * 65535ULL) / span);
            raw_sample = 32767 - (int32_t)frac; // Desce de +32767 a -32768
        }
        ph += inc;

        // 2. Filtro SVF com Gain Compensation
        raw_sample = (raw_sample * gain) >> 15; // Evita estourar o filtro com muita ressonância
        
        f_low += (f * f_band) >> 15;
        // HARD-CLAMP de segurança: Impede o Filtro de "travar" e apitar DC para sempre!
        if (f_low > 32767) f_low = 32767; else if (f_low < -32768) f_low = -32768;

        f_high = raw_sample - f_low - ((q * f_band) >> 15);
        if (f_high > 32767) f_high = 32767; else if (f_high < -32768) f_high = -32768;

        f_band += (f * f_high) >> 15;
        if (f_band > 32767) f_band = 32767; else if (f_band < -32768) f_band = -32768;

        // 3. Envelope Final
        int32_t envSafe = currentEnv >> 14;
        envSafe &= ~(envSafe >> 31);
        int32_t finalVol = (envSafe * volBase) >> 14;

        mixBuffer[i] += (f_low * finalVol) >> 16;
        currentEnv += envStep;
    }
    vo->phase = ph;
}

// =========================================================================
//    MATEMÁTICA E UI (Core 0)
// =========================================================================
void updateDspParams(float cutoff_hz) {
    float p = 0.01f + (current_morph / 255.0f) * 0.98f; 
    
    // Atualiza as divisões da onda base
    dsp_peak_phase = (uint32_t)(p * 4294967295.0f);
    dsp_span_phase = 4294967295U - dsp_peak_phase;
    if (dsp_peak_phase == 0) dsp_peak_phase = 1;
    if (dsp_span_phase == 0) dsp_span_phase = 1;

    // Limite rígido para o SVF não quebrar a 48kHz
    if (cutoff_hz > 6500.0f) cutoff_hz = 6500.0f;
    
    float res_damp = 1.0f - (current_res / 255.0f); 
    if (res_damp < 0.05f) res_damp = 0.05f; 

    filter_f = (int32_t)(2.0f * sinf(PI * cutoff_hz / 48000.0f) * 32768.0f);
    filter_q = (int32_t)(res_damp * 32768.0f);
    
    // Compensador de Ganho: Se a ressonância for brutal, o sinal abaixa pra não destruir as caixas
    float g = res_damp * 2.5f; 
    if (g > 1.0f) g = 1.0f;
    filter_gain = (int32_t)(g * 32768.0f);
}

// Desenha UMA única tecla perfeitamente sem bordas soltas
void drawKey(int index) {
    if (index < 0 || index > 23) return;
    float kw = 320.0f / 24.0f;
    int x = (int)(index * kw);
    int w = (int)((index + 1) * kw) - x; 
    
    int note_in_oct = index % 12;
    bool is_black = (note_in_oct==1 || note_in_oct==3 || note_in_oct==6 || note_in_oct==8 || note_in_oct==10);
    
    uint16_t color = is_black ? tft.color565(30, 30, 30) : tft.color565(200, 200, 200);
    tft.fillRect(x, 120, w, 120, color);
    tft.drawRect(x, 120, w, 120, TFT_BLACK); 
}

// Limpa apenas o local exato da bolinha
void clearParticle() {
    if (last_tx != -1) {
        int k = last_tx / (320.0f / 24.0f);
        drawKey(k - 1);
        drawKey(k);
        drawKey(k + 1);
        last_tx = -1;
        last_ty = -1;
    }
}

// Botões Limpos e Absolutos (Zera o fundo para os números não borrarem)
void drawTopButton(int x, int y, int w, int h, const char* label, int val) {
    uint16_t bg = tft.color565(20, 20, 20);
    // Desenhar a caixa apagando ela mesma limpa qualquer número fantasma!
    tft.fillRect(x, y, w, h, bg);
    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    
    tft.setTextColor(TFT_WHITE, bg);
    tft.setCursor(x + 5, y + 12);
    tft.print(label);
    tft.print(val);
}

void drawUI() {
    tft.fillScreen(TFT_BLACK);
    
    drawTopButton(5, 5, 100, 35, "Morph:", current_morph);
    drawTopButton(110, 5, 100, 35, "Reso:", current_res);
    drawTopButton(215, 5, 100, 35, "Oct:", base_octave);

    // Osciloscópio Framework
    tft.drawRect(5, 45, 310, 71, TFT_DARKGREY);
    
    // Desenha Teclas
    for (int i = 0; i < 24; i++) drawKey(i);
}

// Osciloscópio Limpo e Liso (Cálculo Fiel à onda gerada)
void updateOscilloscope() {
    tft.fillRect(6, 46, 308, 69, TFT_BLACK); 
    
    float p = 0.01f + (current_morph / 255.0f) * 0.98f;
    int last_y = -1;
    
    for (int x = 0; x < 300; x++) {
        float phase = (float)x / 150.0f; // 2 ciclos completos
        phase = phase - (int)phase; 
        
        float val;
        if (phase < p) val = (phase / p) * 2.0f - 1.0f;
        else val = 1.0f - ((phase - p) / (1.0f - p)) * 2.0f;

        // O Y mapeado não quebra as bordas do Scope!
        int draw_y = 80 - (int)(val * 30.0f); 
        draw_y = constrain(draw_y, 48, 113); 
        
        if (x > 0 && last_y != -1) {
            tft.drawLine(5 + x - 1, last_y, 5 + x, draw_y, TFT_GREEN);
        }
        last_y = draw_y;
    }
}

// =========================================================================
//    SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);

    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, HIGH); 
    
    tft.init();
    tft.setRotation(1); 
    tft.invertDisplay(true); 

    touchSpi.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS); 
    ts.begin(touchSpi);
    ts.setRotation(1); 

    synth.begin(I2S_DATA, SMODE_I2S, I2S_BCK, I2S_WS, I2S_32BIT);
    synth.setMasterVolume(255); 

    synth.setCustomWave(0, myCustomWave);
    synth.setEnv(0, 1, 0, 255, 100); 

    updateDspParams(20000.0f); 
    drawUI();
    updateOscilloscope();
}

// =========================================================================
//    LOOP PRINCIPAL
// =========================================================================
void loop() {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        
        // Z-Filter pesado para evitar toques fantasmas
        if (p.z > 300) {
            int x = constrain(map(p.x, 245, 3760, 0, 320), 0, 320);
            int y = constrain(map(p.y, 308, 3740, 0, 240), 0, 240);
            
            last_touch_time = millis(); 

            // --- ZONA DO TECLADO ---
            if (y >= 120) {
                // TRAVA DE SEGURANÇA VISUAL: Mantém a bolinha blindada dentro das teclas!
                // O Y=130 proibe que ela ou o rastro dela vaze pra cima da linha 120!
                int safe_y = constrain(y, 130, 230);

                if (last_tx == -1 || abs(last_tx - x) > 3 || abs(last_ty - safe_y) > 3) {
                    clearParticle(); 
                    
                    tft.fillCircle(x, safe_y, 4, TFT_WHITE);
                    tft.drawCircle(x, safe_y, 6, TFT_CYAN);
                    tft.drawCircle(x, safe_y, 9, TFT_BLUE);
                    last_tx = x;
                    last_ty = safe_y;
                }

                // Cálculo Analógico Ribbon 
                float note_float = x / (320.0f / 24.0f); 
                float midi_note = (note_float - 0.5f) + (base_octave * 12); 
                uint32_t freqCentiHz = (uint32_t)(44000.0f * powf(2.0f, (midi_note - 69.0f) / 12.0f));

                // Volume e Filtro Y 
                float y_ratio = 1.0f - ((y - 120) / 120.0f); 
                uint16_t vol = (uint16_t)(y_ratio * 255.0f);
                float cutoff = 150.0f + (y_ratio * 6000.0f); 

                updateDspParams(cutoff);

                if (!note_is_on) {
                    synth.noteOn(0, freqCentiHz, vol);
                    note_is_on = true;
                } else {
                    synth.setFrequency(0, freqCentiHz);
                    synth.setVolume(0, vol);
                }

            // --- ZONA DOS CONTROLES ---
            } else if (y < 45) {
                bool ui_changed = false;
                
                if (x > 5 && x < 105) {
                    current_morph = map(x, 5, 105, 0, 255);
                    drawTopButton(5, 5, 100, 35, "Morph:", current_morph);
                    ui_changed = true;
                }
                else if (x > 110 && x < 210) {
                    current_res = map(x, 110, 210, 0, 255);
                    drawTopButton(110, 5, 100, 35, "Reso:", current_res);
                    ui_changed = true;
                }
                else if (x > 215 && x < 315 && !note_is_on) { 
                    base_octave = map(x, 215, 315, 1, 7);
                    drawTopButton(215, 5, 100, 35, "Oct:", base_octave);
                    delay(150); 
                }

                if (ui_changed) {
                    updateDspParams(15000.0f); 
                    updateOscilloscope();
                }
            }
        }
    } else { 
        if (note_is_on && (millis() - last_touch_time > 40)) {
            synth.noteOff(0);
            note_is_on = false;
            clearParticle(); 
        }
    }
}